// 图标预览：把所有字形在浅色/深色背景上渲染成一张 PNG，便于检查形状
#include "icons.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>

using namespace lv;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const Icon icons[] = {
        Icon::Open,  Icon::Reload,   Icon::CloseFile, Icon::Quit,
        Icon::Copy,  Icon::SelectAll, Icon::Filter,   Icon::ClearFilter,
        Icon::GotoLine, Icon::ZoomIn, Icon::ZoomOut,  Icon::ZoomReset,
        Icon::Highlight, Icon::ScrollTop, Icon::ScrollBottom,
        Icon::Regex, Icon::Book, Icon::Spark, Icon::NewProfile, Icon::EditProfile,
        Icon::DeleteProfile, Icon::About,
    };
    const int n = int(sizeof(icons) / sizeof(icons[0]));
    const int cell = 64;
    const int cols = 7;
    const int rows = (n + cols - 1) / cols;

    QImage img(cols * cell, rows * 2 * cell, QImage::Format_ARGB32);
    img.fill(QColor(0xF5, 0xF6, 0xF8));

    struct Band { QColor bg; QColor fg; };
    const Band bands[2] = {
        { QColor(0xFF, 0xFF, 0xFF), QColor(0x24, 0x29, 0x2F) },
        { QColor(0x21, 0x25, 0x2C), QColor(0xD9, 0xDF, 0xE8) },
    };

    QPainter p(&img);
    for (int band = 0; band < 2; ++band) {
        p.fillRect(0, band * rows * cell, img.width(), rows * cell, bands[band].bg);
        for (int i = 0; i < n; ++i) {
            const int cx = (i % cols) * cell;
            const int cy = band * rows * cell + (i / cols) * cell;
            p.save();
            p.translate(cx + (cell - 40) / 2, cy + (cell - 40) / 2);
            paintGlyph(icons[i], &p, bands[band].fg, 40);
            p.restore();
        }
    }
    p.end();

    img.save(QStringLiteral("icon_menu_preview.png"));
    return 0;
}
