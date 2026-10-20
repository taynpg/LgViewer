; ============================================================
; LgViewer NSIS 打包脚本（NSIS 3.x, Unicode）
;
; 用法（参数由 tools/PackBuild.py 传入，也可手工调用）：
;   makensis.exe /DSRC_BIN_DIR="<exe所在目录>" /DOUT_DIR="<安装包输出目录>" \
;                /DPRODUCT_VERSION="<版本号>" /DBUILD_MARK="<release|dev>" \
;                /DCOMMIT_ID="<commit短哈希>" [/DLICENSE_FILE="<MIT LICENSE 路径>"] \
;                [/DAPP_ICON="<ico 路径>"] LgViewer.nsi
;
; 产物: OUT_DIR\LgViewer-setup-x64-v<版本>-<release|dev>-<commit>.exe
;
; 安装行为：
;   - 用户级安装，不需要管理员权限（RequestExecutionLevel user）
;   - 默认安装到 %USERPROFILE%\LgViewer
;   - 桌面 + 开始菜单快捷方式，HKCU 卸载信息（可在「应用和功能」中卸载）
; ============================================================

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
; 下列文件按目录整体打包，遇到被占用的旧文件时直接覆盖
SetOverwrite on

; ---------------- 参数（命令行 /D 传入，缺失即报错） ----------------
!ifndef SRC_BIN_DIR
  !define SRC_BIN_DIR ""
!endif
!ifndef OUT_DIR
  !define OUT_DIR ""
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION ""
!endif
!ifndef BUILD_MARK
  !define BUILD_MARK ""
!endif
!ifndef COMMIT_ID
  !define COMMIT_ID ""
!endif
!ifndef LICENSE_FILE
  !define LICENSE_FILE ""
!endif
!ifndef APP_ICON
  !define APP_ICON ""
!endif

!if "${SRC_BIN_DIR}" == ""
  !error "缺少 SRC_BIN_DIR（LgViewer.exe 所在目录）"
!endif
!if "${OUT_DIR}" == ""
  !error "缺少 OUT_DIR（安装包输出目录）"
!endif
!if "${PRODUCT_VERSION}" == ""
  !error "缺少 PRODUCT_VERSION（版本号）"
!endif
!if "${BUILD_MARK}" == ""
  !error "缺少 BUILD_MARK（release 或 dev）"
!endif
!if "${COMMIT_ID}" == ""
  !error "缺少 COMMIT_ID（commit 短哈希）"
!endif

!define PRODUCT_NAME "LgViewer"
!define PRODUCT_DESC "大文本日志查看器"
!define APP_EXE "LgViewer.exe"
!define APP_KEY "Software\${PRODUCT_NAME}"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

; ---------------- 基本信息 ----------------
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
BrandingText "${PRODUCT_NAME} ${PRODUCT_VERSION} (${BUILD_MARK} ${COMMIT_ID})"
OutFile "${OUT_DIR}\${PRODUCT_NAME}-setup-x64-v${PRODUCT_VERSION}-${BUILD_MARK}-${COMMIT_ID}.exe"
InstallDir "$PROFILE\LgViewer"
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"
RequestExecutionLevel user

!include "MUI2.nsh"
!include "FileFunc.nsh"

!if "${APP_ICON}" != ""
  !define MUI_ICON "${APP_ICON}"
  !define MUI_UNICON "${APP_ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "启动 LgViewer"
!define MUI_WELCOMEPAGE_TITLE "安装 ${PRODUCT_NAME} ${PRODUCT_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "${PRODUCT_DESC}。$\r$\n$\r$\n安装到当前用户目录，不需要管理员权限。$\r$\n$\r$\n构建标记：${BUILD_MARK}    提交：${COMMIT_ID}"

!insertmacro MUI_PAGE_WELCOME
!if "${LICENSE_FILE}" != ""
  !insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!endif
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------------- 辅助宏 ----------------
; 结束正在运行的程序，避免文件被占用
!macro KillApp exeName
  nsExec::ExecToLog 'taskkill /F /IM "${exeName}"'
  Pop $0
!macroend

; ---------------- 安装 ----------------
Section "Install"
  !insertmacro KillApp "${APP_EXE}"

  SetOutPath "$INSTDIR"
  ; 连同 windeployqt / clangPack 部署的 DLL、插件目录、LICENSES 一起打包
  File /r "${SRC_BIN_DIR}\*.*"

  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\卸载 ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"

  ; 「应用和功能」卸载入口
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "LgViewer"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

; ---------------- 卸载 ----------------
Section "Uninstall"
  !insertmacro KillApp "${APP_EXE}"

  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\卸载 ${PRODUCT_NAME}.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

  ; 安装目录为本程序独占，整体清除（含用户配置之外的所有文件）
  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
SectionEnd
