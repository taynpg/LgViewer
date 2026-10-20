#include "dropzone.h"

#include "common.h"
#include "icons.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace {

QString sizeText(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < qint64(1024) * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString elided(const QFont &font, const QString &text, qreal width)
{
    return QFontMetricsF(font).elidedText(text, Qt::ElideMiddle, width);
}

} // namespace

DropOverlay::DropOverlay(QWidget *parent)
    : QWidget(parent)
{
    // 纯视觉层：不抢焦点、不吃鼠标事件，让下面的窗口继续接收拖放
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(false);
    hide();
}

void DropOverlay::showFiles(const QStringList &paths, const QString &currentPath)
{
    if (paths.isEmpty()) {
        clear();
        return;
    }

    const QFileInfo fi(paths.first());
    const bool same = !currentPath.isEmpty()
            && QFileInfo(currentPath).absoluteFilePath() == fi.absoluteFilePath();

    m_title  = same ? QStringLiteral("松开鼠标重新加载这个文件")
                    : QStringLiteral("松开鼠标打开日志文件");
    m_detail = fi.fileName();

    m_info = sizeText(fi.size());
    if (paths.size() > 1)
        m_info += QStringLiteral("  ·  另有 %1 个文件将被忽略").arg(paths.size() - 1);
    else
        m_info += QStringLiteral("  ·  ") + QDir::toNativeSeparators(fi.absolutePath());

    update();
}

void DropOverlay::clear()
{
    m_title.clear();
    m_detail.clear();
    m_info.clear();
    hide();
}

void DropOverlay::paintEvent(QPaintEvent *)
{
    const lv::Theme &th = lv::theme();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // 淡出底下的日志，突出中间的提示卡片
    QColor veil = th.bg;
    veil.setAlpha(th.dark ? 216 : 228);
    p.fillRect(rect(), veil);

    const int cardW = qMax(240, qMin(width() - 96, 520));
    const int cardH = 190;
    const QRectF card((width() - cardW) / 2.0, (height() - cardH) / 2.0, cardW, cardH);

    QColor accent = palette().color(QPalette::Highlight);
    if (!accent.isValid())
        accent = th.searchBg;

    QPainterPath shape;
    shape.addRoundedRect(card, 14, 14);
    p.fillPath(shape, th.bgAlt);

    QPen pen(accent);
    pen.setWidthF(2.0);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({5, 4});
    p.setPen(pen);
    p.drawPath(shape);

    p.save();
    p.translate(card.center().x() - 22.0, card.top() + 28.0);
    lv::paintGlyph(lv::Icon::Open, &p, accent, 44);
    p.restore();

    const qreal inner = card.width() - 48.0;

    QFont f = font();
    f.setPixelSize(16);
    f.setBold(true);
    p.setFont(f);
    p.setPen(th.text);
    p.drawText(QRectF(card.left() + 24, card.top() + 84, inner, 24),
               Qt::AlignHCenter | Qt::AlignVCenter, elided(f, m_title, inner));

    f.setBold(false);
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(th.text);
    p.drawText(QRectF(card.left() + 24, card.top() + 116, inner, 20),
               Qt::AlignHCenter | Qt::AlignVCenter, elided(f, m_detail, inner));

    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(th.textDim);
    p.drawText(QRectF(card.left() + 24, card.top() + 142, inner, 18),
               Qt::AlignHCenter | Qt::AlignVCenter, elided(f, m_info, inner));
}
