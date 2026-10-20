#include "icons.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace lv {

namespace {

// 所有字形都在 24x24 的逻辑坐标系里绘制，线宽 2、圆头圆角
constexpr double kCanvas = 24.0;
constexpr double kPen = 2.0;

void setPen(QPainter *p, const QColor &c, double width = kPen)
{
    QPen pen(c, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
}

void drawOpen(QPainter *p, const QColor &c)
{
    QPainterPath path;
    path.moveTo(3, 19);
    path.lineTo(3, 5.5);
    path.quadTo(3, 4.5, 4, 4.5);
    path.lineTo(9.5, 4.5);
    path.lineTo(11.5, 7);
    path.lineTo(20, 7);
    path.quadTo(21, 7, 21, 8);
    path.lineTo(21, 19);
    path.closeSubpath();
    p->drawPath(path);
    p->drawLine(QPointF(5.5, 11), QPointF(18.5, 11));
}

void drawReload(QPainter *p, const QColor &c)
{
    const QRectF arc(5, 5, 14, 14);
    // 300° 圆弧，缺口在右上
    p->drawArc(arc, 40 * 16, 280 * 16);
    // 箭头贴在弧的起点（右上角）
    QPainterPath head;
    head.moveTo(16.4, 2.6);
    head.lineTo(19.6, 6.6);
    head.lineTo(14.4, 7.4);
    head.closeSubpath();
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawPath(head);
}

void drawCloseFile(QPainter *p, const QColor &c)
{
    QPainterPath page;
    page.moveTo(6, 3.5);
    page.lineTo(15, 3.5);
    page.lineTo(18.5, 7);
    page.lineTo(18.5, 20.5);
    page.lineTo(6, 20.5);
    page.closeSubpath();
    p->drawPath(page);
    p->drawLine(QPointF(15, 3.5), QPointF(15, 7));
    p->drawLine(QPointF(15, 7), QPointF(18.5, 7));
    p->drawLine(QPointF(9.2, 12.5), QPointF(14.8, 17.5));
    p->drawLine(QPointF(14.8, 12.5), QPointF(9.2, 17.5));
    Q_UNUSED(c)
}

void drawQuit(QPainter *p, const QColor &c)
{
    p->drawRect(QRectF(4, 4, 7, 16));
    p->drawLine(QPointF(7, 4), QPointF(7, 20));
    p->drawLine(QPointF(13, 12), QPointF(20, 12));
    QPainterPath head;
    head.moveTo(17, 8.8);
    head.lineTo(20.6, 12);
    head.lineTo(17, 15.2);
    head.closeSubpath();
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawPath(head);
}

void drawCopy(QPainter *p, const QColor &c)
{
    // 后页被前页遮住一部分：先画后页轮廓，再用窗口底色填充前页
    p->drawRect(QRectF(8.5, 4, 10.5, 13));
    p->setBrush(QApplication::palette().color(QPalette::Window));
    p->setPen(Qt::NoPen);
    p->drawRect(QRectF(5.5, 8, 10.5, 13));
    setPen(p, c);
    p->setBrush(QApplication::palette().color(QPalette::Window));
    p->drawRect(QRectF(5.5, 8, 10.5, 13));
}

void drawSelectAll(QPainter *p, const QColor &c)
{
    QPen pen(c, kPen);
    pen.setCapStyle(Qt::RoundCap);
    pen.setDashPattern({2.5, 2.5});
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(QRectF(4.5, 4.5, 15, 15));
}

void drawFunnel(QPainter *p, const QColor &c, double x0, double y0, double w, double h)
{
    QPainterPath path;
    path.moveTo(x0, y0);
    path.lineTo(x0 + w, y0);
    path.lineTo(x0 + w * 0.62, y0 + h * 0.55);
    path.lineTo(x0 + w * 0.62, y0 + h);
    path.lineTo(x0 + w * 0.38, y0 + h);
    path.lineTo(x0 + w * 0.38, y0 + h * 0.55);
    path.closeSubpath();
    QColor fill = c;
    fill.setAlpha(46);
    p->setBrush(fill);
    p->drawPath(path);
}

void drawFilter(QPainter *p, const QColor &c)
{
    drawFunnel(p, c, 3.5, 4.5, 17, 15.5);
}

void drawClearFilter(QPainter *p, const QColor &c)
{
    drawFunnel(p, c, 2.5, 6, 13, 13);
    p->setPen(Qt::NoPen);
    p->setBrush(QApplication::palette().color(QPalette::Window));
    p->drawEllipse(QPointF(17.5, 7), 5, 5);
    setPen(p, c, 1.8);
    p->drawEllipse(QPointF(17.5, 7), 5, 5);
    p->drawLine(QPointF(15.6, 5.1), QPointF(19.4, 8.9));
    p->drawLine(QPointF(19.4, 5.1), QPointF(15.6, 8.9));
}

void drawGotoLine(QPainter *p, const QColor &c)
{
    p->drawLine(QPointF(4, 19), QPointF(20, 19));
    p->drawLine(QPointF(12, 4), QPointF(12, 14.5));
    QPainterPath head;
    head.moveTo(8.6, 11.4);
    head.lineTo(12, 15.4);
    head.lineTo(15.4, 11.4);
    head.closeSubpath();
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawPath(head);
}

void drawZoomLens(QPainter *p, const QColor &c)
{
    p->drawEllipse(QPointF(10.5, 10.5), 6.2, 6.2);
    p->drawLine(QPointF(15.2, 15.2), QPointF(20, 20));
}

void drawZoomIn(QPainter *p, const QColor &c)
{
    drawZoomLens(p, c);
    p->drawLine(QPointF(10.5, 7.6), QPointF(10.5, 13.4));
    p->drawLine(QPointF(7.6, 10.5), QPointF(13.4, 10.5));
}

void drawZoomOut(QPainter *p, const QColor &c)
{
    drawZoomLens(p, c);
    p->drawLine(QPointF(7.6, 10.5), QPointF(13.4, 10.5));
}

void drawZoomReset(QPainter *p, const QColor &c)
{
    setPen(p, c, 2.2);
    QFont f = p->font();
    f.setPixelSize(9);
    f.setBold(true);
    p->setFont(f);
    p->drawText(QRectF(2, 6.5, 20, 11), Qt::AlignCenter, QStringLiteral("1:1"));
}

void drawHighlight(QPainter *p, const QColor &c)
{
    // 底部荧光条（固定黄色，一眼能认出是“高亮”）
    QColor amber(0xFB, 0xBF, 0x24);
    QPen amberPen(amber, 3.4);
    amberPen.setCapStyle(Qt::RoundCap);
    p->setPen(amberPen);
    p->drawLine(QPointF(4.5, 20.5), QPointF(19.5, 20.5));

    // 荧光笔笔身
    setPen(p, c, 2.0);
    p->drawLine(QPointF(7.5, 16.5), QPointF(15.5, 6.5));
    p->drawLine(QPointF(10, 19), QPointF(18, 9));
    p->drawLine(QPointF(7.5, 16.5), QPointF(10, 19));
    p->drawLine(QPointF(15.5, 6.5), QPointF(18, 9));
}

void drawScrollTop(QPainter *p, const QColor &c)
{
    p->drawLine(QPointF(4, 6), QPointF(20, 6));
    for (double y : {9.0, 14.5}) {
        QPainterPath chevron;
        chevron.moveTo(6, y + 4);
        chevron.lineTo(12, y);
        chevron.lineTo(18, y + 4);
        p->drawPath(chevron);
    }
}

void drawScrollBottom(QPainter *p, const QColor &c)
{
    p->drawLine(QPointF(4, 18), QPointF(20, 18));
    for (double y : {5.0, 10.5}) {
        QPainterPath chevron;
        chevron.moveTo(6, y);
        chevron.lineTo(12, y + 4);
        chevron.lineTo(18, y);
        p->drawPath(chevron);
    }
}

void drawRegex(QPainter *p, const QColor &c)
{
    setPen(p, c, 2.2);
    QFont f = p->font();
    f.setPixelSize(12);
    f.setBold(true);
    p->setFont(f);
    p->drawText(QRectF(2, 4.5, 20, 15), Qt::AlignCenter, QStringLiteral(".*"));
}

void drawBook(QPainter *p, const QColor &c)
{
    QPainterPath left;
    left.moveTo(12, 6.5);
    left.quadTo(8, 4.2, 4, 5.5);
    left.lineTo(4, 18.5);
    left.quadTo(8, 17.2, 12, 19);
    left.closeSubpath();
    p->drawPath(left);

    QPainterPath right;
    right.moveTo(12, 6.5);
    right.quadTo(16, 4.2, 20, 5.5);
    right.lineTo(20, 18.5);
    right.quadTo(16, 17.2, 12, 19);
    right.closeSubpath();
    p->drawPath(right);
}

void drawNewProfile(QPainter *p, const QColor &c)
{
    p->drawRoundedRect(QRectF(4.5, 4.5, 15, 15), 2.5, 2.5);
    p->drawLine(QPointF(12, 8.5), QPointF(12, 15.5));
    p->drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
}

void drawEditProfile(QPainter *p, const QColor &c)
{
    QPainterPath body;
    body.moveTo(15.8, 4.6);
    body.lineTo(19.4, 8.2);
    body.lineTo(8.2, 19.4);
    body.lineTo(4.6, 19.4);
    body.lineTo(4.6, 15.8);
    body.closeSubpath();
    p->drawPath(body);
    p->drawLine(QPointF(13.9, 6.5), QPointF(17.5, 10.1));
    p->drawLine(QPointF(4.6, 19.4), QPointF(8.2, 19.4));
}

void drawDeleteProfile(QPainter *p, const QColor &c)
{
    p->drawLine(QPointF(4.5, 6.5), QPointF(19.5, 6.5));
    p->drawLine(QPointF(10, 6.5), QPointF(10, 4.5));
    p->drawLine(QPointF(14, 6.5), QPointF(14, 4.5));
    p->drawLine(QPointF(10, 4.5), QPointF(14, 4.5));
    QPainterPath can;
    can.moveTo(6.5, 6.5);
    can.lineTo(7.6, 20);
    can.lineTo(16.4, 20);
    can.lineTo(17.5, 6.5);
    p->drawPath(can);
    p->drawLine(QPointF(10.3, 10), QPointF(10.7, 16.5));
    p->drawLine(QPointF(13.7, 10), QPointF(13.3, 16.5));
}

void drawSpark(QPainter *p, const QColor &c)
{
    // 魔棒：斜向握柄 + 棒头，周围三点星芒，表示「AI 辅助编写」
    p->drawLine(QPointF(4.5, 19.5), QPointF(15.5, 8.5));
    QPainterPath tip;
    tip.moveTo(15.5, 8.5);
    tip.lineTo(17.2, 5.2);
    tip.lineTo(13.9, 6.9);
    tip.closeSubpath();
    p->setBrush(c);
    p->setPen(Qt::NoPen);
    p->drawPath(tip);

    setPen(p, c, 1.6);
    auto star = [&](double x, double y, double r) {
        p->drawLine(QPointF(x - r, y), QPointF(x + r, y));
        p->drawLine(QPointF(x, y - r), QPointF(x, y + r));
    };
    star(18.5, 13.5, 2.6);
    star(9.5, 4.5, 2.0);
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawEllipse(QPointF(19.5, 4.5), 1.2, 1.2);
}

void drawAbout(QPainter *p, const QColor &c)
{
    p->drawEllipse(QPointF(12, 12), 8.2, 8.2);
    p->drawLine(QPointF(12, 11), QPointF(12, 16.2));
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawEllipse(QPointF(12, 7.8), 1.15, 1.15);
}

void drawFind(QPainter *p, const QColor &c)
{
    // 与放大镜同形，但不带 +/-，专用于“查找”
    drawZoomLens(p, c);
}

void drawArrowUp(QPainter *p, const QColor &c)
{
    setPen(p, c, 2.4);
    QPainterPath path;
    path.moveTo(6, 15.5);
    path.lineTo(12, 8.5);
    path.lineTo(18, 15.5);
    p->drawPath(path);
}

void drawArrowDown(QPainter *p, const QColor &c)
{
    setPen(p, c, 2.4);
    QPainterPath path;
    path.moveTo(6, 8.5);
    path.lineTo(12, 15.5);
    path.lineTo(18, 8.5);
    p->drawPath(path);
}

void drawCloseMark(QPainter *p, const QColor &c)
{
    setPen(p, c, 2.2);
    p->drawLine(QPointF(6.8, 6.8), QPointF(17.2, 17.2));
    p->drawLine(QPointF(17.2, 6.8), QPointF(6.8, 17.2));
}

QPixmap renderPixmap(Icon id, int px)
{
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = QApplication::palette().color(QPalette::WindowText);
    paintGlyph(id, &p, color, px);
    return pm;
}

} // namespace

void paintGlyph(Icon id, QPainter *painter, const QColor &color, int px)
{
    painter->save();
    painter->scale(px / kCanvas, px / kCanvas);
    setPen(painter, color);

    switch (id) {
    case Icon::Open:          drawOpen(painter, color); break;
    case Icon::Reload:        drawReload(painter, color); break;
    case Icon::CloseFile:     drawCloseFile(painter, color); break;
    case Icon::Quit:          drawQuit(painter, color); break;
    case Icon::Copy:          drawCopy(painter, color); break;
    case Icon::SelectAll:     drawSelectAll(painter, color); break;
    case Icon::Filter:        drawFilter(painter, color); break;
    case Icon::ClearFilter:   drawClearFilter(painter, color); break;
    case Icon::GotoLine:      drawGotoLine(painter, color); break;
    case Icon::ZoomIn:        drawZoomIn(painter, color); break;
    case Icon::ZoomOut:       drawZoomOut(painter, color); break;
    case Icon::ZoomReset:     drawZoomReset(painter, color); break;
    case Icon::Highlight:     drawHighlight(painter, color); break;
    case Icon::ScrollTop:     drawScrollTop(painter, color); break;
    case Icon::ScrollBottom:  drawScrollBottom(painter, color); break;
    case Icon::Regex:         drawRegex(painter, color); break;
    case Icon::Book:          drawBook(painter, color); break;
    case Icon::Spark:         drawSpark(painter, color); break;
    case Icon::NewProfile:    drawNewProfile(painter, color); break;
    case Icon::EditProfile:   drawEditProfile(painter, color); break;
    case Icon::DeleteProfile: drawDeleteProfile(painter, color); break;
    case Icon::About:         drawAbout(painter, color); break;
    case Icon::Find:          drawFind(painter, color); break;
    case Icon::ArrowUp:       drawArrowUp(painter, color); break;
    case Icon::ArrowDown:     drawArrowDown(painter, color); break;
    case Icon::CloseMark:     drawCloseMark(painter, color); break;
    }
    painter->restore();
}

QIcon icon(Icon id)
{
    static QHash<int, QIcon> cache;
    const int key = int(id);
    auto it = cache.find(key);
    if (it != cache.end())
        return it.value();

    QIcon ic;
    for (int px : {16, 20, 24, 32, 48})
        ic.addPixmap(renderPixmap(id, px));
    cache.insert(key, ic);
    return ic;
}

} // namespace lv
