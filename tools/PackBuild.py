#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LgViewer 一键构建 + 打包（MSYS2 UCRT64 + windeployqt + clangPack + NSIS）。

用法:
    python tools/PackBuild.py [--dry-run] [--no-color] [--yes]
                              [--mark=release|dev] [--clean|--no-clean]
                              [--pack|--no-pack] [--qt-root=DIR]

    不带参数时逐项交互询问（回车采用默认值）。
    --dry-run    只打印将执行的命令，不改动任何文件
    --yes        非交互模式，全部采用默认值（适合 CI）

流程:
    1. 选择 dev / release（release 不加版本后缀，dev 版本号带 -dev）
    2. 是否删除已有构建目录
    3. cmake 配置 + ninja 编译（Release 配置，产物在 build-<mark>/bin/）
    4. 是否打包:
         windeployqt 部署 Qt 运行库 -> clangPack 收集工具链依赖（libstdc++ / libpcre2 等）
         -> 拷贝 LICENSE / LICENSES -> makensis 生成安装包
       产物: build-<mark>/LgViewer-setup-x64-v<版本>-<mark>-<commit>.exe

环境变量:
    QT_LIB_ROOT  覆盖 Qt 根目录（默认 C:\\msys64\\ucrt64）
    NSIS_HOME    覆盖 NSIS 目录（默认 %ProgramFiles(x86)%\\NSIS）
    CLANG_PACK   覆盖 clangPack 可执行文件路径（默认从 PATH 查找）
"""

import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
CONFIG = "Release"
DEFAULT_QT_ROOT = r"C:\msys64\ucrt64"

DRY_RUN = False
COLOR = sys.stdout.isatty()


# ============================ 通用工具 ============================

def die(msg: str) -> None:
    print(f"[ERROR] {msg}")
    sys.exit(1)


def ask_yes_no(prompt: str, default: bool) -> bool:
    d = "Y/n" if default else "y/N"
    while True:
        try:
            ans = input(f"{prompt} [{d}]：").strip().lower()
        except EOFError:
            return default
        if ans == "":
            return default
        if ans in ("y", "yes"):
            return True
        if ans in ("n", "no"):
            return False
        print("  请输入 y 或 n。")


def run(cmd: list, what: str, cwd: Path | None = None, env: dict | None = None) -> None:
    """执行外部命令（stderr 合并进 stdout）；dry-run 只打印。

    子进程输出经管道实时透传，并把进度刷新用的裸 \\r 规范成 \\n，
    避免 Makefile / windeployqt 的进度行在部分终端里出现乱码。"""
    shown = " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd)
    print(f"{'[DRY-RUN] ' if DRY_RUN else '    > '}{shown}"
          + (f"   (dir={cwd})" if cwd else ""))
    if DRY_RUN:
        return

    try:
        proc = subprocess.Popen(
            [str(c) for c in cmd],
            cwd=str(cwd) if cwd else None,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
    except FileNotFoundError:
        die(f"{what} 未找到或不在 PATH 中: {cmd[0]}")

    out = sys.stdout.buffer
    pending_cr = False
    try:
        assert proc.stdout is not None
        while True:
            chunk = proc.stdout.read(256)
            if not chunk:
                if pending_cr:
                    out.write(b"\n")
                break
            if pending_cr:
                chunk = (b"" if chunk.startswith(b"\n") else b"\n") + chunk
                pending_cr = False
            if chunk.endswith(b"\r"):
                pending_cr = True
                chunk = chunk[:-1]
            chunk = chunk.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
            if chunk:
                out.write(chunk)
                out.flush()
        code = proc.wait()
    finally:
        if proc.poll() is None:
            proc.kill()
    if code != 0:
        die(f"{what} 失败 (exit {code})")


def toolchain_env(qt_root: str) -> dict:
    """把 UCRT64 的 bin 前置进 PATH，并让 pkg-config 找到 pcre2。"""
    env = dict(os.environ)
    if COLOR:
        env["CLICOLOR_FORCE"] = "1"
    else:
        env["CLICOLOR"] = "0"

    bin_dir = str(Path(qt_root) / "bin")
    parts = env.get("PATH", "").split(os.pathsep)
    if bin_dir not in parts:
        env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    pkg_dir = str(Path(qt_root) / "lib" / "pkgconfig")
    pkg = [p for p in env.get("PKG_CONFIG_PATH", "").split(os.pathsep) if p]
    if pkg_dir not in pkg:
        env["PKG_CONFIG_PATH"] = os.pathsep.join([pkg_dir] + pkg)
    return env


def find_tool(explicit: str | None, names: list, extra_paths: list) -> str | None:
    if explicit and Path(explicit).is_file():
        return explicit
    for name in names:
        p = shutil.which(name)
        if p:
            return p
    for p in extra_paths:
        if p and Path(p).is_file():
            return str(p)
    return None


def ensure_utf8_bom(path: Path) -> None:
    """NSIS 3.x 靠 BOM 判定 UTF-8；编辑器保存时可能丢掉，这里兜底补上。"""
    if not path.is_file():
        return
    data = path.read_bytes()
    if not data.startswith(b"\xef\xbb\xbf"):
        path.write_bytes(b"\xef\xbb\xbf" + data)


def git_short_hash() -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"], cwd=str(REPO_ROOT),
            stderr=subprocess.DEVNULL)
        return out.decode("ascii", "replace").strip() or "nogit"
    except Exception:
        return "nogit"


def project_version() -> str:
    cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"project\s*\(\s*LgViewer\s+VERSION\s+([0-9][0-9.]*)", cmake)
    if not m:
        die("无法从 CMakeLists.txt 解析 project(... VERSION ...)")
    return m.group(1)


# ============================ 构建 ============================

def build(qt_root: str, mark: str, do_clean: bool) -> Path:
    build_dir = REPO_ROOT / f"build-{mark}"

    if do_clean and build_dir.exists():
        print(f"[build] 删除已有构建目录: {build_dir}")
        if not DRY_RUN:
            try:
                shutil.rmtree(build_dir)
            except OSError as exc:
                die(f"删除构建目录失败: {exc}\n"
                    f"[HINT] 先关闭正在运行的 LgViewer / 资源管理器窗口，或手工删除 {build_dir}")
    if not DRY_RUN:
        build_dir.mkdir(parents=True, exist_ok=True)

    color = "ON" if COLOR else "OFF"
    cmake_args = [
        "cmake", "-S", str(REPO_ROOT), "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={CONFIG}",
        f"-DCMAKE_PREFIX_PATH={qt_root}",
        f"-DLG_BUILD_MARK={mark}",
        f"-DCMAKE_COLOR_DIAGNOSTICS={color}",
    ]
    env = toolchain_env(qt_root)
    print("[build] cmake 配置")
    run(cmake_args, "cmake 配置", cwd=REPO_ROOT, env=env)

    print("[build] 编译")
    run(["cmake", "--build", str(build_dir), "--parallel"], "cmake 编译",
        cwd=REPO_ROOT, env=env)
    return build_dir


# ============================ 打包 ============================

def pack(build_dir: Path, qt_root: str, mark: str) -> Path:
    exe = build_dir / "bin" / "LgViewer.exe"
    if not DRY_RUN and not exe.is_file():
        die(f"未找到构建产物: {exe}")
    bin_dir = exe.parent
    print(f"[pack] 产物目录: {bin_dir}")

    qt_bin = Path(qt_root) / "bin"
    windeployqt = qt_bin / "windeployqt.exe"
    clangpack = find_tool(
        os.environ.get("CLANG_PACK"), ["clangPack", "clangPack.exe"],
        [r"D:\Program\Bin\clangPack.exe"])

    if not DRY_RUN and not windeployqt.is_file():
        die(f"windeployqt 不存在: {windeployqt}\n[HINT] 用 QT_LIB_ROOT 指定 Qt 根目录")

    # 1. windeployqt：Qt 自身运行库；没有 clangPack 时让它顺带拷编译器运行库
    print("[pack 1/4] windeployqt 部署 Qt 运行库")
    wdq_args = [str(windeployqt), "--release"]
    if not clangpack:
        wdq_args.append("--compiler-runtime")
    wdq_args.append(str(exe))
    run(wdq_args, "windeployqt")

    # 2. clangPack：补齐 libstdc++ / libwinpthread / libpcre2 等非 Qt 依赖
    print("[pack 2/4] clangPack 收集工具链依赖")
    if clangpack:
        run([clangpack, "-e", str(exe), "-r", "-c", str(qt_bin)], "clangPack")
    else:
        print("[WARN] 未找到 clangPack，已跳过（依赖 windeployqt 的 --compiler-runtime 兜底）；"
              "可用环境变量 CLANG_PACK 指定路径")

    # 3. 许可证：随安装包分发（LGPL / MIT 合规）
    print("[pack 3/4] 拷贝 LICENSE 与 LICENSES/")
    lic_dir = bin_dir / "LICENSES"
    if (REPO_ROOT / "LICENSES").is_dir():
        print(f"    copy LICENSES -> {lic_dir}")
        if not DRY_RUN:
            shutil.copytree(REPO_ROOT / "LICENSES", lic_dir, dirs_exist_ok=True)
    else:
        print("[WARN] 仓库内没有 LICENSES 目录，跳过")
    if (REPO_ROOT / "LICENSE").is_file():
        if not DRY_RUN:
            lic_dir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPO_ROOT / "LICENSE", lic_dir / "LICENSE")

    # 4. NSIS：安装包
    print("[pack 4/4] 生成安装包")
    version = project_version()
    commit = git_short_hash()
    nsis_home = os.environ.get("NSIS_HOME") or str(
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "NSIS")
    makensis = Path(nsis_home) / "makensis.exe"
    if not DRY_RUN and not makensis.is_file():
        die(f"makensis 不存在: {makensis}\n[HINT] 安装 NSIS 3.x，或用环境变量 NSIS_HOME 指定目录")

    nsi = SCRIPT_DIR / "LgViewer.nsi"
    if not nsi.is_file():
        die(f"缺少 NSIS 脚本: {nsi}")
    if not DRY_RUN:
        ensure_utf8_bom(nsi)

    run([
        str(makensis),
        f"/DSRC_BIN_DIR={bin_dir}",
        f"/DOUT_DIR={build_dir}",
        f"/DPRODUCT_VERSION={version}",
        f"/DBUILD_MARK={mark}",
        f"/DCOMMIT_ID={commit}",
        f"/DLICENSE_FILE={REPO_ROOT / 'LICENSE'}",
        str(nsi),
    ], "makensis")

    setup = build_dir / f"LgViewer-setup-x64-v{version}-{mark}-{commit}.exe"
    print(f"\n安装包: {setup}")
    return setup


# ============================ 入口 ============================

def main() -> None:
    global DRY_RUN, COLOR
    args = sys.argv[1:]
    DRY_RUN = "--dry-run" in args
    if "--no-color" in args:
        COLOR = False
    if platform.system() != "Windows":
        die("当前脚本只支持 Windows（MSYS2 UCRT64 工具链）")

    mark = next((a.split("=", 1)[1] for a in args if a.startswith("--mark=")), None)
    if mark is None:
        mark = "release" if ask_yes_no("构建 release 版本（否则 dev）", True) else "dev"
    if mark not in ("release", "dev"):
        die(f"未知构建标记: {mark}（只能是 release 或 dev）")

    if "--clean" in args:
        do_clean = True
    elif "--no-clean" in args:
        do_clean = False
    else:
        do_clean = ask_yes_no("删除已有构建目录做全新构建", True)

    if "--pack" in args:
        do_pack = True
    elif "--no-pack" in args:
        do_pack = False
    else:
        do_pack = ask_yes_no("构建完成后打包成安装包（windeployqt + clangPack + NSIS）", True)

    # 交互模式下才询问 Qt 根目录；带了 --mark= / --yes 视为无人值守
    unattended = "--yes" in args or any(a.startswith("--mark=") for a in args)
    qt_root = next((a.split("=", 1)[1] for a in args if a.startswith("--qt-root=")), None)
    if qt_root is None:
        qt_root = os.environ.get("QT_LIB_ROOT") or DEFAULT_QT_ROOT
        if not unattended:
            typed = input(f"Qt 根目录 [默认 {qt_root}]：").strip()
            if typed:
                qt_root = typed
    qt_root = qt_root.rstrip("\\/")

    # 让 cmake / ninja / g++ / pkg-config 在本进程内也能按 UCRT64 解析
    prepared = toolchain_env(qt_root)
    os.environ["PATH"] = prepared["PATH"]
    os.environ["PKG_CONFIG_PATH"] = prepared["PKG_CONFIG_PATH"]

    print("\n------------------------------------------------------------")
    print(f" 项目根目录 : {REPO_ROOT}")
    print(f" 工具链     : MSYS2 UCRT64 (GCC)  {qt_root}")
    print(f" 构建模式   : {mark}  (CMAKE_BUILD_TYPE={CONFIG})")
    print(f" 构建目录   : build-{mark}")
    print(f" 清理重建   : {do_clean}")
    print(f" 构建后打包 : {do_pack}")
    print("------------------------------------------------------------\n")

    build_dir = build(qt_root, mark, do_clean)
    if do_pack:
        pack(build_dir, qt_root, mark)
    print("\n全部完成。")


if __name__ == "__main__":
    main()
