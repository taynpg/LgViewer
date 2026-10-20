#include "filterdialog.h"

#include "common.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QDateTimeEdit *makeTimeEdit()
{
    auto *e = new QDateTimeEdit;
    e->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    e->setTimeZone(QTimeZone::UTC);
    e->setCalendarPopup(true);
    e->setMinimumWidth(210);
    return e;
}

} // namespace

FilterDialog::FilterDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("筛选条件"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);
    setMinimumWidth(560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(10);

    // ---------------- 时间范围 ----------------
    auto *timeBox = new QGroupBox(QStringLiteral("时间范围"), this);
    auto *timeLayout = new QVBoxLayout(timeBox);
    timeLayout->setSpacing(6);

    m_timeEnabled = new QCheckBox(QStringLiteral("启用时间筛选"), timeBox);
    timeLayout->addWidget(m_timeEnabled);

    auto *fromRow = new QHBoxLayout;
    fromRow->addWidget(new QLabel(QStringLiteral("从"), timeBox));
    m_from = makeTimeEdit();
    fromRow->addWidget(m_from, 1);
    timeLayout->addLayout(fromRow);

    auto *toRow = new QHBoxLayout;
    toRow->addWidget(new QLabel(QStringLiteral("到"), timeBox));
    m_to = makeTimeEdit();
    toRow->addWidget(m_to, 1);
    timeLayout->addLayout(toRow);

    auto *timeButtons = new QHBoxLayout;
    auto *firstBtn = new QPushButton(QStringLiteral("首条时间"), timeBox);
    auto *lastBtn = new QPushButton(QStringLiteral("末条时间"), timeBox);
    auto *allBtn = new QPushButton(QStringLiteral("不限时间"), timeBox);
    timeButtons->addWidget(firstBtn);
    timeButtons->addWidget(lastBtn);
    timeButtons->addWidget(allBtn);
    timeButtons->addStretch(1);
    timeLayout->addLayout(timeButtons);

    m_timeHint = new QLabel(timeBox);
    m_timeHint->setStyleSheet(QStringLiteral("color:#8c959f;"));
    timeLayout->addWidget(m_timeHint);

    root->addWidget(timeBox);

    // ---------------- 日志等级 ----------------
    auto *levelBox = new QGroupBox(QStringLiteral("日志等级"), this);
    auto *levelLayout = new QVBoxLayout(levelBox);
    levelLayout->setSpacing(6);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);
    for (int i = 0; i < lv::kLevelCount; ++i) {
        auto *cb = new QCheckBox;
        cb->setChecked(true);
        cb->setToolTip(QStringLiteral("识别为 %1：%2")
                           .arg(lv::levelDisplayName(lv::Level(i)), lv::levelAliasHint(lv::Level(i))));
        m_levelChecks.append(cb);
        grid->addWidget(cb, i / 3, i % 3);
    }
    levelLayout->addLayout(grid);

    auto *levelButtons = new QHBoxLayout;
    auto *checkAll = new QPushButton(QStringLiteral("全选"), levelBox);
    auto *clearAll = new QPushButton(QStringLiteral("全不选"), levelBox);
    levelButtons->addWidget(checkAll);
    levelButtons->addWidget(clearAll);
    levelButtons->addStretch(1);
    levelLayout->addLayout(levelButtons);

    auto *levelHint = new QLabel(
        QStringLiteral("等级取自正则里的 level 命名组（也接受 lvl / sev / severity），"
                       "大小写不敏感；没有该组时按行首的 [Info]、ERROR: 等标记兜底。"),
        levelBox);
    levelHint->setWordWrap(true);
    levelHint->setStyleSheet(QStringLiteral("color:#8c959f;"));
    levelLayout->addWidget(levelHint);

    root->addWidget(levelBox);

    // ---------------- 关键字 ----------------
    auto *textBox = new QGroupBox(QStringLiteral("关键字"), this);
    auto *textLayout = new QVBoxLayout(textBox);
    textLayout->setSpacing(6);

    m_text = new QLineEdit(textBox);
    m_text->setPlaceholderText(QStringLiteral("包含的文本，输入即筛选"));
    m_text->setClearButtonEnabled(true);
    textLayout->addWidget(m_text);

    auto *flagsRow = new QHBoxLayout;
    m_textRegex = new QCheckBox(QStringLiteral("正则"), textBox);
    m_textCase = new QCheckBox(QStringLiteral("区分大小写"), textBox);
    m_highlightOnly = new QCheckBox(QStringLiteral("仅高亮（不过滤）"), textBox);
    flagsRow->addWidget(m_textRegex);
    flagsRow->addWidget(m_textCase);
    flagsRow->addWidget(m_highlightOnly);
    flagsRow->addStretch(1);
    textLayout->addLayout(flagsRow);

    root->addWidget(textBox);
    root->addStretch(1);

    // ---------------- 底部按钮 ----------------
    auto *buttons = new QDialogButtonBox(this);
    auto *resetBtn = buttons->addButton(QStringLiteral("重置"), QDialogButtonBox::ResetRole);
    auto *closeBtn = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::AcceptRole);
    closeBtn->setDefault(true);
    root->addWidget(buttons);

    // ---------------- 信号 ----------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(220);
    connect(m_debounce, &QTimer::timeout, this, &FilterDialog::applyNow);

    connect(m_timeEnabled, &QCheckBox::toggled, this, [this] {
        updateEnabledState();
        scheduleApply();
    });
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &) {
        if (!m_updating)
            scheduleApply();
    });
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &) {
        if (!m_updating)
            scheduleApply();
    });
    connect(firstBtn, &QPushButton::clicked, this, [this] {
        if (!m_doc || !m_doc->hasTimestamps())
            return;
        {
            const QSignalBlocker b(m_timeEnabled);
            m_timeEnabled->setChecked(true);
        }
        updateEnabledState();
        m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(m_doc->minTimeMs(), QTimeZone::UTC));
        scheduleApply();
    });
    connect(lastBtn, &QPushButton::clicked, this, [this] {
        if (!m_doc || !m_doc->hasTimestamps())
            return;
        {
            const QSignalBlocker b(m_timeEnabled);
            m_timeEnabled->setChecked(true);
        }
        updateEnabledState();
        m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(m_doc->maxTimeMs(), QTimeZone::UTC));
        scheduleApply();
    });
    connect(allBtn, &QPushButton::clicked, this, [this] {
        {
            const QSignalBlocker b(m_timeEnabled);
            m_timeEnabled->setChecked(false);
        }
        updateTimeBounds();
        updateEnabledState();
        scheduleApply();
    });

    for (QCheckBox *cb : m_levelChecks)
        connect(cb, &QCheckBox::toggled, this, [this] { scheduleApply(); });

    connect(checkAll, &QPushButton::clicked, this, [this] {
        for (QCheckBox *cb : m_levelChecks) {
            const QSignalBlocker b(cb);
            cb->setChecked(true);
        }
        scheduleApply();
    });
    connect(clearAll, &QPushButton::clicked, this, [this] {
        for (QCheckBox *cb : m_levelChecks) {
            const QSignalBlocker b(cb);
            cb->setChecked(false);
        }
        scheduleApply();
    });

    connect(m_text, &QLineEdit::textChanged, this, [this] { scheduleApply(); });
    connect(m_text, &QLineEdit::returnPressed, this, &FilterDialog::applyNow);
    connect(m_textRegex, &QCheckBox::toggled, this, [this] { scheduleApply(); });
    connect(m_textCase, &QCheckBox::toggled, this, [this] { scheduleApply(); });
    connect(m_highlightOnly, &QCheckBox::toggled, this, [this] { scheduleApply(); });

    connect(resetBtn, &QPushButton::clicked, this, &FilterDialog::resetFilters);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    updateLevelLabels();
    updateEnabledState();
}

void FilterDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateTimeBounds();
    updateLevelLabels();
}

void FilterDialog::setDocument(LogDocument *doc)
{
    m_doc = doc;
    updateTimeBounds();
    updateLevelLabels();
    updateEnabledState();
}

void FilterDialog::updateTimeBounds()
{
    m_updating = true;
    const bool has = m_doc && m_doc->hasTimestamps() && m_doc->lineCount() > 0;
    m_timeEnabled->setEnabled(has);
    if (has) {
        m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(m_doc->minTimeMs(), QTimeZone::UTC));
        m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(m_doc->maxTimeMs(), QTimeZone::UTC));
        const QDateTime a = QDateTime::fromMSecsSinceEpoch(m_doc->minTimeMs(), QTimeZone::UTC);
        const QDateTime b = QDateTime::fromMSecsSinceEpoch(m_doc->maxTimeMs(), QTimeZone::UTC);
        m_timeHint->setText(QStringLiteral("日志时间跨度：%1 ~ %2")
                                .arg(a.toString(QStringLiteral("MM-dd HH:mm:ss.zzz")),
                                     b.toString(QStringLiteral("MM-dd HH:mm:ss.zzz"))));
    } else {
        m_timeHint->setText(QStringLiteral("当前配置未解析出时间字段"));
    }
    m_updating = false;
    updateEnabledState();
}

void FilterDialog::updateLevelLabels()
{
    const lv::Theme &th = lv::theme();
    const QLocale locale;
    for (int i = 0; i < lv::kLevelCount; ++i) {
        const auto level = lv::Level(i);
        const int count = m_doc ? m_doc->levelCount(level) : 0;
        QCheckBox *cb = m_levelChecks.at(i);
        cb->setText(QStringLiteral("%1 (%2)")
                        .arg(lv::levelDisplayName(level), locale.toString(qint64(count))));
        cb->setStyleSheet(QStringLiteral("color:%1; font-weight:600;")
                              .arg(lv::levelColor(level, th.dark).name()));
        cb->setEnabled(m_doc != nullptr);
    }
}

void FilterDialog::updateEnabledState()
{
    const bool timeOn = m_timeEnabled->isChecked() && m_timeEnabled->isEnabled();
    m_from->setEnabled(timeOn);
    m_to->setEnabled(timeOn);
}

void FilterDialog::scheduleApply()
{
    if (m_updating)
        return;
    if (m_debounce)
        m_debounce->start();
}

void FilterDialog::applyNow()
{
    if (m_debounce)
        m_debounce->stop();
    emit filterChanged(currentSpec());
}

FilterSpec FilterDialog::currentSpec() const
{
    FilterSpec spec;
    if (m_doc && m_doc->hasTimestamps() && m_timeEnabled->isChecked()) {
        spec.timeEnabled = true;
        spec.fromMs = m_from->dateTime().toMSecsSinceEpoch();
        spec.toMs = m_to->dateTime().toMSecsSinceEpoch();
        if (spec.toMs < spec.fromMs)
            std::swap(spec.fromMs, spec.toMs);
    }

    quint8 mask = 0;
    for (int i = 0; i < m_levelChecks.size(); ++i) {
        if (m_levelChecks.at(i)->isChecked())
            mask |= lv::levelBit(lv::Level(i));
    }
    spec.levelMask = mask;

    spec.text = m_text->text();
    spec.textIsRegex = m_textRegex->isChecked();
    spec.textCaseSensitive = m_textCase->isChecked();
    spec.highlightOnly = m_highlightOnly->isChecked();
    return spec;
}

void FilterDialog::resetFilters()
{
    m_updating = true;
    {
        const QSignalBlocker b(m_timeEnabled);
        m_timeEnabled->setChecked(false);
    }
    {
        const QSignalBlocker b(m_text);
        m_text->clear();
    }
    for (QCheckBox *cb : m_levelChecks) {
        const QSignalBlocker b(cb);
        cb->setChecked(true);
    }
    m_updating = false;
    updateEnabledState();
    applyNow();
}

void FilterDialog::focusKeyword()
{
    if (isHidden())
        show();
    raise();
    activateWindow();
    m_text->setFocus();
    m_text->selectAll();
}
