#include "findbar.h"

#include "common.h"
#include "icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPalette>
#include <QTimer>
#include <QToolButton>

namespace {

QToolButton *makeTool(QWidget *parent, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus); // 焦点留在输入框，连续按 Enter / F3 不丢
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    return b;
}

QToolButton *makeToggle(QWidget *parent, const QString &text, const QString &tip)
{
    QToolButton *b = makeTool(parent, tip);
    b->setText(text);
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 6, 8, 6);
    lay->setSpacing(6);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(
        QStringLiteral("在日志中查找（Enter 下一个，Shift+Enter 上一个，Esc 关闭）"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setMinimumWidth(240);
    // 拖文件进来时别把路径插进输入框，交给主窗口打开文件
    m_edit->setAcceptDrops(false);
    m_edit->installEventFilter(this);
    lay->addWidget(m_edit, 1);

    m_case = makeToggle(this, QStringLiteral("Aa"), QStringLiteral("区分大小写"));
    lay->addWidget(m_case);

    m_word = makeToggle(
        this, QStringLiteral("全字"),
        QStringLiteral("全字匹配：英文/数字必须是完整单词（\"warn\" 不会命中 \"warning\"）；"
                       "中文关键词不受影响"));
    lay->addWidget(m_word);

    m_count = new QLabel(this);
    m_count->setMinimumWidth(96);
    m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(m_count);

    m_prev = makeTool(this, QStringLiteral("上一个匹配 (Shift+F3)"));
    m_prev->setIcon(lv::icon(lv::Icon::ArrowUp));
    lay->addWidget(m_prev);

    m_next = makeTool(this, QStringLiteral("下一个匹配 (F3)"));
    m_next->setIcon(lv::icon(lv::Icon::ArrowDown));
    lay->addWidget(m_next);

    m_close = makeTool(this, QStringLiteral("关闭查找栏 (Esc)"));
    m_close->setIcon(lv::icon(lv::Icon::CloseMark));
    lay->addWidget(m_close);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(220);

    connect(m_debounce, &QTimer::timeout, this, &FindBar::queryChanged);
    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &) { scheduleQuery(); });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        m_debounce->stop();
        emit goNext();
    });
    connect(m_case, &QToolButton::toggled, this, [this](bool) {
        m_debounce->stop();
        emit queryChanged();
    });
    connect(m_word, &QToolButton::toggled, this, [this](bool) {
        m_debounce->stop();
        emit queryChanged();
    });
    connect(m_prev, &QToolButton::clicked, this, &FindBar::goPrev);
    connect(m_next, &QToolButton::clicked, this, &FindBar::goNext);
    connect(m_close, &QToolButton::clicked, this, &FindBar::closed);

    applyStyle();
    showIdle();
}

void FindBar::applyStyle()
{
    const lv::Theme &th = lv::theme();
    QString css = QStringLiteral(R"(
QWidget#findBar { background: %BGALT%; border-top: 1px solid %BORDER%; }
QLineEdit {
    background: %BG%; color: %TEXT%;
    border: 1px solid %BORDER%; border-radius: 3px;
    padding: 3px 6px; margin: 0;
    selection-background-color: %SELBG%; selection-color: %SELTEXT%;
}
QLineEdit:focus { border-color: %ACCENT%; }
QToolButton {
    color: %TEXT%; border: 1px solid transparent; border-radius: 3px;
    padding: 2px 7px;
}
QToolButton:hover { background: %HOVER%; }
QToolButton:checked { background: %SELBG%; color: %SELTEXT%; border-color: %ACCENT%; }
QLabel { color: %DIM%; background: transparent; }
)");
    css.replace(QStringLiteral("%BGALT%"), th.bgAlt.name());
    css.replace(QStringLiteral("%BORDER%"), th.border.name());
    css.replace(QStringLiteral("%BG%"), th.bg.name());
    css.replace(QStringLiteral("%TEXT%"), th.text.name());
    css.replace(QStringLiteral("%SELBG%"), th.selBg.name());
    css.replace(QStringLiteral("%SELTEXT%"), th.selText.name());
    css.replace(QStringLiteral("%ACCENT%"), th.tagTime.name());
    css.replace(QStringLiteral("%HOVER%"), th.gutterBg.name());
    css.replace(QStringLiteral("%DIM%"), th.textDim.name());
    setStyleSheet(css);

    QPalette pal = m_edit->palette();
    pal.setColor(QPalette::PlaceholderText, th.textDim);
    m_edit->setPalette(pal);
}

void FindBar::scheduleQuery()
{
    m_debounce->start();
}

void FindBar::activate(const QString &seed)
{
    if (!seed.isEmpty())
        m_edit->setText(seed);
    show();
    m_edit->setFocus();
    m_edit->selectAll();
}

QString FindBar::text() const
{
    return m_edit->text();
}

bool FindBar::caseSensitive() const
{
    return m_case->isChecked();
}

bool FindBar::wholeWord() const
{
    return m_word->isChecked();
}

void FindBar::setCaseSensitive(bool on)
{
    m_case->setChecked(on);
}

void FindBar::setWholeWord(bool on)
{
    m_word->setChecked(on);
}

void FindBar::refreshStyle()
{
    applyStyle();
    update();
}

void FindBar::showIdle()
{
    m_count->setText(QString());
}

void FindBar::showPending()
{
    m_count->setText(QStringLiteral("搜索中…"));
}

void FindBar::showCount(int total, bool truncated)
{
    if (total <= 0) {
        showNoMatch();
        return;
    }
    const QLocale loc;
    m_count->setText(QStringLiteral("共 %1 处%2")
                         .arg(loc.toString(qint64(total)),
                              truncated ? QStringLiteral("+") : QString()));
}

void FindBar::showPosition(int index1Based, int total, bool truncated)
{
    if (total <= 0) {
        showNoMatch();
        return;
    }
    const QLocale loc;
    m_count->setText(QStringLiteral("%1 / %2%3")
                         .arg(loc.toString(qint64(index1Based)),
                              loc.toString(qint64(total)),
                              truncated ? QStringLiteral("+") : QString()));
}

void FindBar::showNoMatch()
{
    m_count->setText(QStringLiteral("无结果"));
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool shift = (ke->modifiers() & Qt::ShiftModifier) != 0;

        if (ke->key() == Qt::Key_Escape) {
            emit closed();
            return true;
        }
        // QLineEdit 的 returnPressed 区分不了 Shift，这里自己判断
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            m_debounce->stop();
            if (shift)
                emit goPrev();
            else
                emit goNext();
            return true;
        }
        if (ke->key() == Qt::Key_F3) {
            m_debounce->stop();
            if (shift)
                emit goPrev();
            else
                emit goNext();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
