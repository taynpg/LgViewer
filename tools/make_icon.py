"""生成 LgViewer 应用图标（PNG + 多尺寸 ICO）。

设计：深色圆角底 + 左侧栏 + 四条等级色日志行 + 右下放大镜，
小尺寸下仍能看出「彩色日志行 + 查看器」两件事。

用法：python tools/make_icon.py
输出：resources/app_icon.png（1024）、resources/app_icon_256.png、resources/app_icon.ico
"""

import os

from PIL import Image, ImageDraw

SS = 4                      # 超采样倍数，保证圆角/圆形边缘平滑
S = 1024                    # 设计尺寸
W = S * SS

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources")


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rounded_mask(size, radius):
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius=radius, fill=255)
    return m


def build_master():
    canvas = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    # 背景竖向渐变
    top = (0x2C, 0x36, 0x45)
    bottom = (0x14, 0x19, 0x22)
    grad = Image.new("RGB", (1, W))
    gd = ImageDraw.Draw(grad)
    for y in range(W):
        gd.point((0, y), fill=lerp(top, bottom, y / (W - 1)))
    grad = grad.resize((W, W))
    bg = grad.convert("RGBA")

    mask = rounded_mask((W, W), 224 * SS)
    canvas.paste(bg, (0, 0), mask)

    d = ImageDraw.Draw(canvas)

    # 内描边
    d.rounded_rectangle(
        [3 * SS, 3 * SS, W - 1 - 3 * SS, W - 1 - 3 * SS],
        radius=222 * SS,
        outline=(0x45, 0x52, 0x66, 255),
        width=3 * SS,
    )

    # 左侧「行号栏」
    d.rounded_rectangle(
        [150 * SS, 268 * SS, 166 * SS, 762 * SS],
        radius=8 * SS,
        fill=(0x4C, 0x5A, 0x72, 255),
    )

    # 四条等级色日志行
    bars = [
        (196, 296, 470, 54, (0x4A, 0xDE, 0x80)),   # INFO 绿
        (196, 432, 600, 54, (0x60, 0xA5, 0xFA)),   # DEBUG 蓝
        (196, 568, 400, 54, (0xFB, 0xBF, 0x24)),   # WARN 琥珀
        (196, 704, 300, 54, (0xF8, 0x71, 0x71)),   # ERROR 红
    ]
    for x, y, w, h, color in bars:
        d.rounded_rectangle(
            [x * SS, y * SS, (x + w) * SS, (y + h) * SS],
            radius=(h // 2) * SS,
            fill=color + (255,),
        )

    # 放大镜：镜片内填底色（把下面的日志行「盖住」，像一个真正的镜头）
    cx, cy, r = 700, 636, 158
    d.ellipse(
        [(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS],
        fill=(0x1A, 0x20, 0x29, 255),
    )
    ring = 46
    d.ellipse(
        [(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS],
        outline=(0xE8, 0xEE, 0xF7, 255),
        width=ring * SS,
    )
    # 镜片里两道迷你日志行（暗示「查看内容」）
    d.rounded_rectangle(
        [(cx - 78) * SS, (cy - 34) * SS, (cx + 40) * SS, (cy - 6) * SS],
        radius=14 * SS,
        fill=(0x4A, 0xDE, 0x80, 255),
    )
    d.rounded_rectangle(
        [(cx - 78) * SS, (cy + 20) * SS, (cx + 12) * SS, (cy + 48) * SS],
        radius=14 * SS,
        fill=(0xFB, 0xBF, 0x24, 255),
    )

    # 手柄
    hx0, hy0 = cx + int(r * 0.70), cy + int(r * 0.70)
    hx1, hy1 = cx + int(r * 1.55), cy + int(r * 1.55)
    d.line([hx0 * SS, hy0 * SS, hx1 * SS, hy1 * SS], fill=(0xE8, 0xEE, 0xF7, 255), width=62 * SS)
    d.ellipse(
        [(hx1 - 31) * SS, (hy1 - 31) * SS, (hx1 + 31) * SS, (hy1 + 31) * SS],
        fill=(0xE8, 0xEE, 0xF7, 255),
    )

    return canvas.resize((S, S), Image.LANCZOS)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    master = build_master()

    png_path = os.path.join(OUT_DIR, "app_icon.png")
    master.save(png_path)
    master.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT_DIR, "app_icon_256.png"))

    sizes = [256, 128, 64, 48, 32, 24, 16]
    frames = [master.resize((s, s), Image.LANCZOS) for s in sizes]
    ico_path = os.path.join(OUT_DIR, "app_icon.ico")
    frames[0].save(ico_path, format="ICO", sizes=[(s, s) for s in sizes])

    # 预览用：透明背景下排一行不同尺寸
    preview = Image.new("RGBA", (1024, 300), (0xF2, 0xF4, 0xF7, 255))
    x = 30
    for s in (256, 128, 64, 48, 32, 24, 16):
        img = master.resize((s, s), Image.LANCZOS)
        preview.alpha_composite(img, (x, 30))
        x += s + 24
    preview.save(os.path.join(OUT_DIR, "icon_preview.png"))

    print("wrote", png_path)
    print("wrote", ico_path)


if __name__ == "__main__":
    main()
