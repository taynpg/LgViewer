#include "logview.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextLayout>
#include <QTextOption>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

struct LogView::RowCache {
    QString text;
    QVector<QTextLayout::FormatRange> formats;
    std::unique_ptr<QTextLayout> layout;
    int height = 0;
    int width = 0;
};

namespace {

// UTF-8 字节偏移 -> QString 里的字符下标（4 字节码点占 2 个 UTF-16 单元）
int byteToCharIndex(const char *s, int byteOffset)
{
    int chars = 0;
    int i = 0;
    while (i < byteOffset) {
        const uchar c = uchar(s[i]);
        if (c < 0x80) {
            i += 1;
            chars += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
            chars += 1;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
            chars += 1;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
            chars += 2;
        } else {
            i += 1;
            chars += 1;
        }
    }
    return chars;
}

int groupOf(const RegexEngine &eng, std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        const int idx = eng.groupIndex(QString::fromLatin1(n));
        if (idx >= 0)
            return idx;
    }
    return -1;
}

} // namespace

LogView::LogView(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_font(lv::theme().mono)
    , m_fm(m_font)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setSingleStep(3);
    horizontalScrollBar()->setSingleStep(24);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        m_hScroll = v;
        viewport()->update();
    });

    updateMetrics();
}

LogView::~LogView() = default;

void LogView::setDocument(LogDocument *doc)
{
    m_doc = doc;
    m_cache.clear();
    m_compiledPattern.clear();
    m_selAnchor = -1;
    m_selCurrent = -1;
    m_hits.reset();
    m_activeHit = -1;
    m_hScroll = 0;
    horizontalScrollBar()->setValue(0);
    updateMetrics();
    viewport()->update();
}

void LogView::updateMetrics()
{
    m_font = lv::theme().mono;
    m_font.setPointSize(m_fontSize);
    m_fm = QFontMetrics(m_font);
    m_rowHeight = m_fm.height() + 7;

    const int total = m_doc ? m_doc->lineCount() : 0;
    const int digits = QString::number(qMax(1, total)).size();
    m_gutterWidth = m_fm.horizontalAdvance(QLatin1Char('0')) * qMax(digits, 4) + 20;

    m_cache.clear();
    updateScrollBars();
}

void LogView::updateScrollBars()
{
    const int rows = m_doc ? m_doc->rowCount() : 0;
    const int page = qMax(1, viewport()->height() / qMax(1, m_rowHeight));
    QScrollBar *vb = verticalScrollBar();
    vb->setRange(0, qMax(0, rows - page));
    vb->setPageStep(page);
    if (vb->value() > vb->maximum())
        vb->setValue(vb->maximum());
}

void LogView::ensureEngines()
{
    if (!m_doc)
        return;
    const QString pattern = m_doc->profile().pattern;
    if (pattern != m_compiledPattern) {
        m_compiledPattern = pattern;
        m_engine.compile(pattern);
        m_groupTime = groupOf(m_engine, {"time", "timestamp", "ts", "datetime", "date"});
        m_groupLevel = groupOf(m_engine, {"level", "lvl", "sev", "severity", "loglevel"});
        m_groupMsg = groupOf(m_engine, {"msg", "message", "text", "content", "body"});
        m_groupLoc = groupOf(m_engine, {"loc", "location", "source", "src", "file", "where"});
    }
}

void LogView::setFontPointSize(int pt)
{
    pt = qBound(6, pt, 36);
    if (pt == m_fontSize)
        return;
    m_fontSize = pt;
    updateMetrics();
    viewport()->update();
    emit fontPointSizeChanged(m_fontSize);
}

void LogView::setHighlightPattern(const QString &text, bool isRegex, bool caseSensitive)
{
    m_hlText = text;
    m_hlRegex = isRegex;
    m_hlCase = caseSensitive;
    m_hlNeedle = text.toUtf8();

    if (isRegex && !text.isEmpty()) {
        QString pattern = text;
        if (!caseSensitive)
            pattern.prepend(QStringLiteral("(?i)"));
        m_hlEngine.compile(pattern);
    } else {
        m_hlEngine.release();
    }

    m_cache.clear();
    viewport()->update();
}

void LogView::clearHighlight()
{
    setHighlightPattern(QString(), false, false);
}

void LogView::setSearchHits(std::shared_ptr<const QVector<SearchHit>> hits)
{
    if (m_hits == hits)
        return;
    m_hits = std::move(hits);
    m_activeHit = -1;
    m_cache.clear();
    viewport()->update();
}

void LogView::setActiveHit(int index)
{
    if (index == m_activeHit)
        return;
    const int old = m_activeHit;
    m_activeHit = index;

    // 只作废受影响的两行，其余行的布局缓存继续留着
    auto drop = [this](int idx) {
        if (!m_hits || idx < 0 || idx >= int(m_hits->size()))
            return;
        m_cache.erase(int(m_hits->at(idx).row));
    };
    drop(old);
    drop(index);
    viewport()->update();
}

void LogView::setLevelKeywordColored(bool on)
{
    if (m_hlLevelKeyword == on)
        return;
    m_hlLevelKeyword = on;
    m_cache.clear();
    viewport()->update();
}

void LogView::setMessageLevelColored(bool on)
{
    if (m_hlMessageLevel == on)
        return;
    m_hlMessageLevel = on;
    m_cache.clear();
    viewport()->update();
}

void LogView::setRowTintEnabled(bool on)
{
    if (m_hlRowTint == on)
        return;
    m_hlRowTint = on;
    viewport()->update();
}

void LogView::invalidateAll()
{
    m_cache.clear();
    updateMetrics();
    viewport()->update();
}

LogView::RowCache &LogView::cacheFor(int row)
{
    auto it = m_cache.find(row);
    if (it != m_cache.end())
        return *it->second;

    if (m_cache.size() > 1200)
        m_cache.clear();

    auto cache = std::make_unique<RowCache>();
    buildRow(row, *cache);
    RowCache &ref = *cache;
    m_cache.emplace(row, std::move(cache));
    return ref;
}

void LogView::buildRow(int row, RowCache &rc)
{
    rc.formats.clear();
    rc.layout.reset();
    rc.width = 0;
    rc.text.clear();
    rc.height = m_rowHeight;

    if (!m_doc)
        return;

    const int line = int(m_doc->rowLine(row));
    int len = 0;
    const char *p = m_doc->lineData(line, &len);
    if (!p)
        return;

    rc.text = QString::fromUtf8(p, len);
    const LineMeta meta = m_doc->meta(line);
    const lv::Theme &th = lv::theme();
    const QColor levelFg = lv::levelColor(lv::Level(meta.level), th.dark);

    ensureEngines();

    auto addRange = [&rc](int start, int length, const QColor &fg, const QColor &bg, bool bold) {
        if (length <= 0)
            return;
        QTextCharFormat f;
        if (fg.isValid())
            f.setForeground(fg);
        if (bg.isValid())
            f.setBackground(bg);
        if (bold)
            f.setFontWeight(QFont::DemiBold);
        QTextLayout::FormatRange r;
        r.start = start;
        r.length = length;
        r.format = f;
        rc.formats.append(r);
    };

    if (meta.isContinuation()) {
        addRange(0, rc.text.size(), th.textDim, QColor(), false);
    } else if (m_engine.isValid() && m_engine.match(p, len) == 1) {
        const auto toChar = [p](int off) { return off <= 0 ? 0 : byteToCharIndex(p, off); };

        if (m_groupTime >= 0 && m_engine.hasCapture(m_groupTime)) {
            const int a = toChar(m_engine.start(m_groupTime));
            const int b = toChar(m_engine.end(m_groupTime));
            addRange(a, b - a, th.tagTime, QColor(), false);
        }
        if (m_groupLevel >= 0 && m_engine.hasCapture(m_groupLevel)) {
            const int a = toChar(m_engine.start(m_groupLevel));
            const int b = toChar(m_engine.end(m_groupLevel));
            addRange(a, b - a, m_hlLevelKeyword ? levelFg : th.text, QColor(), m_hlLevelKeyword);
        }
        if (m_groupMsg >= 0 && m_engine.hasCapture(m_groupMsg)) {
            const int a = toChar(m_engine.start(m_groupMsg));
            const int b = toChar(m_engine.end(m_groupMsg));
            const bool strong = meta.level == quint8(lv::Level::Warn)
                || meta.level == quint8(lv::Level::Error)
                || meta.level == quint8(lv::Level::Fatal);
            // 开关打开时所有等级的正文都用等级色（与 [Info] 关键字同色）
            const bool colorMsg = m_hlMessageLevel
                ? (meta.level != quint8(lv::Level::Other))
                : strong;
            addRange(a, b - a, colorMsg ? levelFg : th.text, QColor(), false);
        }
        if (m_groupLoc >= 0 && m_engine.hasCapture(m_groupLoc)) {
            const int a = toChar(m_engine.start(m_groupLoc));
            const int b = toChar(m_engine.end(m_groupLoc));
            addRange(a, b - a, th.textDim, QColor(), false);
        }
    }

    // 搜索高亮（在其它格式之后追加，背景色覆盖）
    if (!m_hlText.isEmpty()) {
        if (m_hlRegex && m_hlEngine.isValid()) {
            int off = 0;
            while (off <= len) {
                if (m_hlEngine.matchAt(p, len, off) != 1)
                    break;
                const int a = m_hlEngine.start(0);
                const int b = m_hlEngine.end(0);
                if (b <= a)
                    break;
                const int ca = byteToCharIndex(p, a);
                const int cb = byteToCharIndex(p, b);
                addRange(ca, cb - ca, th.matchText, th.matchBg, false);
                off = b;
            }
        } else if (!m_hlNeedle.isEmpty() && m_hlNeedle.size() <= len) {
            const int n = int(m_hlNeedle.size());
            const char *nd = m_hlNeedle.constData();
            for (int i = 0; i + n <= len;) {
                bool hit = true;
                for (int k = 0; k < n; ++k) {
                    char hc = p[i + k];
                    char nc = nd[k];
                    if (!m_hlCase) {
                        if (hc >= 'A' && hc <= 'Z')
                            hc = char(hc - 'A' + 'a');
                        if (nc >= 'A' && nc <= 'Z')
                            nc = char(nc - 'A' + 'a');
                    }
                    if (hc != nc) {
                        hit = false;
                        break;
                    }
                }
                if (hit) {
                    const int ca = byteToCharIndex(p, i);
                    const int cb = byteToCharIndex(p, i + n);
                    addRange(ca, cb - ca, th.matchText, th.matchBg, false);
                    i += n;
                } else {
                    ++i;
                }
            }
        }
    }

    // 搜索命中（放在最后追加，背景色盖住上面的级别/筛选着色）
    if (m_hits && !m_hits->isEmpty()) {
        const quint32 rowKey = quint32(row);
        auto it = std::lower_bound(m_hits->begin(), m_hits->end(), rowKey,
                                   [](const SearchHit &h, quint32 r) { return h.row < r; });
        int hitIndex = int(it - m_hits->begin());
        for (; it != m_hits->end() && it->row == rowKey; ++it, ++hitIndex) {
            const int a = byteToCharIndex(p, int(it->startByte));
            const int b = byteToCharIndex(p, int(it->endByte));
            const bool active = hitIndex == m_activeHit;
            addRange(a, b - a,
                     active ? th.activeMatchText : th.searchText,
                     active ? th.activeMatchBg : th.searchBg,
                     active);
        }
    }

    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    opt.setTabStopDistance(m_fm.horizontalAdvance(QLatin1Char(' ')) * 4.0);

    rc.layout = std::make_unique<QTextLayout>(rc.text, m_font);
    rc.layout->setTextOption(opt);
    rc.layout->beginLayout();
    QTextLine textLine = rc.layout->createLine();
    if (textLine.isValid())
        textLine.setPosition(QPointF(0, 0));
    rc.layout->endLayout();

    rc.height = qMax(m_rowHeight, int(std::ceil(rc.layout->boundingRect().height())) + 6);
    rc.width = textLine.isValid() ? int(std::ceil(textLine.naturalTextWidth())) : 0;
}

void LogView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const lv::Theme &th = lv::theme();
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), th.bg);

    const int rows = m_doc ? m_doc->rowCount() : 0;
    if (!m_doc || rows == 0) {
        p.setPen(th.textDim);
        const QString text = m_doc ? QStringLiteral("没有匹配的日志行")
                                   : QStringLiteral("打开一个 .txt 日志文件（Ctrl+O）");
        p.drawText(viewport()->rect(), Qt::AlignCenter, text);
        return;
    }

    const int vh = viewport()->height();
    const int vw = viewport()->width();
    const int textLeft = m_gutterWidth + 8;
    int first = verticalScrollBar()->value();
    if (first >= rows)
        first = qMax(0, rows - 1);

    int selFirst = -1;
    int selLast = -1;
    selectionRange(&selFirst, &selLast);

    int activeRow = -1;
    if (m_hits && m_activeHit >= 0 && m_activeHit < int(m_hits->size()))
        activeRow = int(m_hits->at(m_activeHit).row);

    int maxWidth = 0;
    int y = 0;
    int row = first;

    while (row < rows && y < vh) {
        const int line = int(m_doc->rowLine(row));
        RowCache &rc = cacheFor(row);
        const LineMeta meta = m_doc->meta(line);
        const QColor levelFg = lv::levelColor(lv::Level(meta.level), th.dark);

        // 行号栏
        p.fillRect(0, y, m_gutterWidth, m_rowHeight, th.gutterBg);

        // 等级淡色背景
        const QColor tint = lv::levelTint(lv::Level(meta.level), th.dark);
        if (m_hlRowTint && tint.alpha() > 0)
            p.fillRect(m_gutterWidth, y, vw - m_gutterWidth, m_rowHeight, tint);

        // 选中
        if (row >= selFirst && row <= selLast)
            p.fillRect(0, y, vw, m_rowHeight, th.selBg);

        // 等级色条
        p.fillRect(m_gutterWidth - 3, y, 3, m_rowHeight, levelFg);

        // 当前搜索命中所在行，在行号栏左侧再压一条橙色标记
        if (row == activeRow)
            p.fillRect(m_gutterWidth - 8, y, 4, m_rowHeight, th.activeMatchBg);

        // 行号
        p.setPen(row == m_selCurrent ? th.gutterActive : th.gutterText);
        QFont gutterFont = m_font;
        gutterFont.setPointSize(qMax(6, m_fontSize - 1));
        p.setFont(gutterFont);
        p.drawText(QRect(2, y, m_gutterWidth - 10, m_rowHeight),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(line + 1));

        // 正文
        if (rc.layout) {
            p.save();
            p.setClipRect(QRect(textLeft, y, vw - textLeft, m_rowHeight));
            p.translate(textLeft - m_hScroll, y + (m_rowHeight - rc.height) / 2 + 2);
            p.setFont(m_font);
            QColor base = th.text;
            if (meta.isContinuation())
                base = th.textDim;
            else if (!m_hlMessageLevel
                     && (meta.level == quint8(lv::Level::Warn)
                         || meta.level == quint8(lv::Level::Error)
                         || meta.level == quint8(lv::Level::Fatal)))
                base = levelFg;
            p.setPen(base);
            rc.layout->draw(&p, QPointF(0, 0), rc.formats);
            p.restore();
        }

        maxWidth = qMax(maxWidth, rc.width + textLeft + 24);
        y += m_rowHeight;
        ++row;
    }

    // 行号栏右侧分隔线
    p.setPen(th.border);
    p.drawLine(m_gutterWidth - 1, 0, m_gutterWidth - 1, vh);

    const int hMax = qMax(0, maxWidth - vw);
    if (horizontalScrollBar()->maximum() != hMax) {
        horizontalScrollBar()->setRange(0, hMax);
        horizontalScrollBar()->setPageStep(qMax(1, vw));
    }
}

void LogView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void LogView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int step = event->angleDelta().y() > 0 ? 1 : -1;
        setFontPointSize(m_fontSize + step);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

int LogView::rowAt(int y) const
{
    if (!m_doc)
        return -1;
    const int first = verticalScrollBar()->value();
    const int row = first + y / qMax(1, m_rowHeight);
    return qBound(0, row, qMax(0, m_doc->rowCount() - 1));
}

void LogView::selectionRange(int *first, int *last) const
{
    if (m_selCurrent < 0) {
        *first = -1;
        *last = -1;
        return;
    }
    if (m_selAnchor < 0) {
        *first = m_selCurrent;
        *last = m_selCurrent;
        return;
    }
    *first = qMin(m_selAnchor, m_selCurrent);
    *last = qMax(m_selAnchor, m_selCurrent);
}

void LogView::moveCurrent(int row, bool extend)
{
    if (!m_doc || m_doc->rowCount() == 0)
        return;
    row = qBound(0, row, m_doc->rowCount() - 1);

    if (!extend || m_selAnchor < 0)
        m_selAnchor = row;
    m_selCurrent = row;

    // 保证可见
    QScrollBar *vb = verticalScrollBar();
    const int page = qMax(1, viewport()->height() / qMax(1, m_rowHeight));
    if (row < vb->value())
        vb->setValue(row);
    else if (row >= vb->value() + page)
        vb->setValue(qMin(vb->maximum(), row - page + 1));

    viewport()->update();
    emitCurrentLine();
}

void LogView::emitCurrentLine()
{
    emit currentLineChanged(currentLine());
}

int LogView::currentLine() const
{
    if (!m_doc || m_selCurrent < 0)
        return -1;
    return int(m_doc->rowLine(m_selCurrent));
}

void LogView::scrollToRow(int row, bool center)
{
    if (!m_doc)
        return;
    const int page = qMax(1, viewport()->height() / qMax(1, m_rowHeight));
    if (center)
        verticalScrollBar()->setValue(qMax(0, row - page / 2));
    else if (row < verticalScrollBar()->value() || row >= verticalScrollBar()->value() + page)
        verticalScrollBar()->setValue(qMax(0, qMin(row, qMax(0, m_doc->rowCount() - page))));
    viewport()->update();
}

int LogView::firstVisibleLine() const
{
    if (!m_doc || m_doc->rowCount() == 0)
        return -1;
    const int row = verticalScrollBar()->value();
    if (row < 0 || row >= m_doc->rowCount())
        return -1;
    return int(m_doc->rowLine(row));
}

int LogView::firstVisibleRow() const
{
    if (!m_doc || m_doc->rowCount() == 0)
        return -1;
    return qBound(0, verticalScrollBar()->value(), m_doc->rowCount() - 1);
}

void LogView::scrollToLineAtTop(int line)
{
    if (!m_doc || line < 0)
        return;
    const int row = m_doc->rowAtOrAfterLine(line);
    if (row < 0)
        return;
    verticalScrollBar()->setValue(row);
    viewport()->update();
}

void LogView::scrollToLine(int line, bool center)
{
    if (!m_doc)
        return;
    const int row = m_doc->rowOfLine(line);
    if (row < 0)
        return;
    m_selAnchor = row;
    m_selCurrent = row;
    scrollToRow(row, center);
    viewport()->update();
    emitCurrentLine();
}

void LogView::selectAll()
{
    if (!m_doc || m_doc->rowCount() == 0)
        return;
    m_selAnchor = 0;
    m_selCurrent = m_doc->rowCount() - 1;
    viewport()->update();
    emitCurrentLine();
}

void LogView::copySelection()
{
    if (!m_doc)
        return;
    int first = -1, last = -1;
    selectionRange(&first, &last);
    if (first < 0)
        return;

    QString out;
    out.reserve((last - first + 1) * 80);
    for (int row = first; row <= last; ++row) {
        const int line = int(m_doc->rowLine(row));
        int len = 0;
        const char *p = m_doc->lineData(line, &len);
        if (!p)
            continue;
        out.append(QString::fromUtf8(p, len));
        out.append(QLatin1Char('\n'));
    }
    QApplication::clipboard()->setText(out);
}

void LogView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int row = rowAt(int(event->position().y()));
        m_dragging = true;
        moveCurrent(row, (event->modifiers() & Qt::ShiftModifier) != 0);
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void LogView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        moveCurrent(rowAt(int(event->position().y())), true);
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void LogView::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void LogView::keyPressEvent(QKeyEvent *event)
{
    if (!m_doc) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    const int page = qMax(1, viewport()->height() / qMax(1, m_rowHeight) - 1);
    const int cur = m_selCurrent < 0 ? 0 : m_selCurrent;

    switch (event->key()) {
    case Qt::Key_Up:
        moveCurrent(cur - 1, (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    case Qt::Key_Down:
        moveCurrent(cur + 1, (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    case Qt::Key_PageUp:
        moveCurrent(cur - page, (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    case Qt::Key_PageDown:
        moveCurrent(cur + page, (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    case Qt::Key_Home:
        if (event->modifiers() & Qt::ControlModifier)
            moveCurrent(0, false);
        else
            moveCurrent(verticalScrollBar()->value(), false);
        return;
    case Qt::Key_End:
        if (event->modifiers() & Qt::ControlModifier)
            moveCurrent(m_doc->rowCount() - 1, false);
        else
            moveCurrent(verticalScrollBar()->value() + page, false);
        return;
    default:
        break;
    }

    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void LogView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(QStringLiteral("复制选中行"));
    menu.addSeparator();
    QAction *gotoAction = menu.addAction(QStringLiteral("跳到行…"));
    QAction *topAction = menu.addAction(QStringLiteral("回到开头"));
    QAction *endAction = menu.addAction(QStringLiteral("跳到末尾"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copyAction) {
        copySelection();
    } else if (chosen == gotoAction) {
        bool ok = false;
        const int line = QInputDialog::getInt(this, QStringLiteral("跳到行"),
                                              QStringLiteral("行号："), currentLine() + 1, 1,
                                              qMax(1, m_doc ? m_doc->lineCount() : 1), 1, &ok);
        if (ok)
            scrollToLine(line - 1, true);
    } else if (chosen == topAction) {
        scrollToRow(0);
    } else if (chosen == endAction) {
        scrollToRow(qMax(0, (m_doc ? m_doc->rowCount() : 1) - 1));
    }
}
