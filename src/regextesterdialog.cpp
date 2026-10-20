#include "regextesterdialog.h"

#include "common.h"
#include "regexengine.h"

#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *kGroupBg[] = {"#ffe066", "#b2f2bb", "#a5d8ff", "#ffd8a8", "#d0bfff", "#ffc9c9"};

QPlainTextEdit *makeMonoEdit(int rows)
{
    auto *e = new QPlainTextEdit;
    QFont f(lv::monospaceFamily());
    f.setPointSize(10);
    e->setFont(f);
    e->setTabStopDistance(QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')) * 4);
    e->setLineWrapMode(QPlainTextEdit::NoWrap);
    Q_UNUSED(rows)
    return e;
}

} // namespace

RegexTesterDialog::RegexTesterDialog(const PatternProfile &profile, const QString &sampleText,
                                     QWidget *parent)
    : QDialog(parent)
    , m_initialPattern(profile.pattern)
{
    setWindowTitle(QStringLiteral("正则表达式测试器"));
    resize(1100, 720);

    auto *root = new QVBoxLayout(this);

    auto *patternLabel = new QLabel(QStringLiteral("日志结构正则（命名组 time / level / msg / loc / tid）"),
                                    this);
    root->addWidget(patternLabel);

    m_patternEdit = makeMonoEdit(3);
    m_patternEdit->setPlainText(profile.pattern);
    m_patternEdit->setFixedHeight(76);
    root->addWidget(m_patternEdit);

    m_error = new QLabel(this);
    m_error->setStyleSheet(QStringLiteral("color:#cf222e; font-weight:600;"));
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    auto *sampleBox = new QGroupBox(QStringLiteral("样本文本"), splitter);
    auto *sampleLayout = new QVBoxLayout(sampleBox);
    m_sampleEdit = makeMonoEdit(20);
    m_sampleEdit->setPlainText(sampleText);
    sampleLayout->addWidget(m_sampleEdit);
    splitter->addWidget(sampleBox);

    auto *rightBox = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightBox);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_summary = new QLabel(rightBox);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    rightLayout->addWidget(m_summary);

    m_groups = new QTableWidget(0, 4, rightBox);
    m_groups->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("组"),
                                         QStringLiteral("内容"), QStringLiteral("位置")});
    m_groups->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_groups->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_groups->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_groups->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_groups->verticalHeader()->setVisible(false);
    m_groups->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_groups->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_groups->setWordWrap(false);
    rightLayout->addWidget(m_groups, 1);

    splitter->addWidget(rightBox);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(this);
    auto *applyBtn = buttons->addButton(QStringLiteral("应用为当前配置"),
                                        QDialogButtonBox::ApplyRole);
    buttons->addButton(QDialogButtonBox::Close);
    root->addWidget(buttons);

    connect(applyBtn, &QPushButton::clicked, this, &RegexTesterDialog::applyToProfile);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout, this, &RegexTesterDialog::runTest);
    connect(m_patternEdit, &QPlainTextEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_sampleEdit, &QPlainTextEdit::textChanged, this, [this] { m_debounce->start(); });

    QTimer::singleShot(0, this, &RegexTesterDialog::runTest);
}

QString RegexTesterDialog::pattern() const
{
    return m_patternEdit->toPlainText();
}

void RegexTesterDialog::setPattern(const QString &pattern)
{
    m_patternEdit->setPlainText(pattern);
}

void RegexTesterDialog::setSampleText(const QString &text)
{
    m_sampleEdit->setPlainText(text);
}

void RegexTesterDialog::applyToProfile()
{
    if (m_error->isVisible())
        return;
    emit patternApplied(m_patternEdit->toPlainText());
    accept();
}

void RegexTesterDialog::runTest()
{
    m_debounce->stop();
    m_groups->setRowCount(0);
    m_error->hide();

    QVector<QTextEdit::ExtraSelection> selections;

    const QString pattern = m_patternEdit->toPlainText();
    if (pattern.isEmpty()) {
        m_summary->setText(QStringLiteral("<span style='color:#8c959f'>请输入正则表达式</span>"));
        m_sampleEdit->setExtraSelections(selections);
        return;
    }

    RegexEngine engine;
    QString error;
    int errorOffset = 0;
    if (!engine.compile(pattern, &error, &errorOffset)) {
        m_error->setText(QStringLiteral("编译失败（位置 %1）：%2").arg(errorOffset).arg(error));
        m_error->show();
        m_summary->setText(QString());
        m_sampleEdit->setExtraSelections(selections);
        return;
    }

    const QStringList names = engine.groupNames();
    const int giTime = engine.groupIndex(QStringLiteral("time"));
    const int giLevel = engine.groupIndex(QStringLiteral("level"));
    const bool hasTime = giTime >= 0;
    const bool hasLevel = giLevel >= 0;

    const QString sample = m_sampleEdit->toPlainText();
    QElapsedTimer timer;
    timer.start();

    int lineNo = 0;
    int hitLines = 0;
    int matchCount = 0;
    int docPos = 0;
    int pos = 0;
    const int tableLimit = 4000;

    const QColor matchBg(0xff, 0xe0, 0x66);
    const QColor matchFg(0x3a, 0x2a, 0x00);

    while (pos <= sample.size()) {
        const int nl = sample.indexOf(QLatin1Char('\n'), pos);
        const QString line = (nl < 0) ? sample.mid(pos) : sample.mid(pos, nl - pos);
        const int lineDocStart = docPos;

        const QByteArray bytes = line.toUtf8();
        int byteOffset = 0;
        bool lineHit = false;

        while (byteOffset <= bytes.size()) {
            if (engine.matchAt(bytes.constData(), bytes.size(), byteOffset) != 1)
                break;
            const int a = engine.start(0);
            const int b = engine.end(0);
            if (b <= a)
                break;
            lineHit = true;
            ++matchCount;

            // 整段命中高亮
            {
                QTextEdit::ExtraSelection sel;
                QTextCursor cursor(m_sampleEdit->document());
                cursor.setPosition(lineDocStart + lv::utf8ByteToCharIndex(bytes.constData(), a));
                cursor.setPosition(lineDocStart + lv::utf8ByteToCharIndex(bytes.constData(), b),
                                   QTextCursor::KeepAnchor);
                sel.cursor = cursor;
                sel.format.setBackground(matchBg);
                sel.format.setForeground(matchFg);
                selections.append(sel);
            }

            // 分组表
            if (m_groups->rowCount() < tableLimit) {
                auto addRow = [this, lineNo, a](int groupIndex, const QString &name,
                                                const QString &value, int relPos) {
                    const int row = m_groups->rowCount();
                    m_groups->insertRow(row);
                    m_groups->setItem(row, 0, new QTableWidgetItem(QString::number(lineNo + 1)));
                    m_groups->setItem(row, 1,
                                      new QTableWidgetItem(groupIndex == 0
                                                               ? QStringLiteral("整个匹配")
                                                               : name));
                    m_groups->setItem(row, 2, new QTableWidgetItem(value));
                    m_groups->setItem(row, 3,
                                      new QTableWidgetItem(relPos < 0 ? QStringLiteral("—")
                                                                      : QString::number(relPos)));
                    Q_UNUSED(a)
                };

                addRow(0, QString(), line.mid(lv::utf8ByteToCharIndex(bytes.constData(), a),
                                              lv::utf8ByteToCharIndex(bytes.constData(), b)
                                                  - lv::utf8ByteToCharIndex(bytes.constData(), a)),
                       lv::utf8ByteToCharIndex(bytes.constData(), a));

                for (int i = 0; i < names.size(); ++i) {
                    const int gi = engine.groupIndex(names.at(i));
                    if (gi < 0 || !engine.hasCapture(gi))
                        continue;
                    const int ga = lv::utf8ByteToCharIndex(bytes.constData(), engine.start(gi));
                    const int gb = lv::utf8ByteToCharIndex(bytes.constData(), engine.end(gi));
                    addRow(gi, names.at(i), line.mid(ga, gb - ga), ga);

                    // 命名组单独上色
                    QTextEdit::ExtraSelection sel;
                    QTextCursor cursor(m_sampleEdit->document());
                    cursor.setPosition(lineDocStart + ga);
                    cursor.setPosition(lineDocStart + gb, QTextCursor::KeepAnchor);
                    sel.cursor = cursor;
                    sel.format.setBackground(QColor(QString::fromLatin1(kGroupBg[i % 6])));
                    sel.format.setForeground(QColor(0x1f, 0x23, 0x28));
                    selections.append(sel);
                }
            }

            byteOffset = b;
            if (matchCount > 20000)
                break;
        }

        if (lineHit)
            ++hitLines;

        if (nl < 0)
            break;
        docPos = lineDocStart + line.size() + 1;
        pos = nl + 1;
        ++lineNo;
    }

    const double ms = double(timer.nsecsElapsed()) / 1e6;

    QString summary = QStringLiteral("命中 <b>%1</b> 行 / 共 %2 行，<b>%3</b> 处匹配 · 耗时 <b>%4 ms</b>")
                          .arg(hitLines)
                          .arg(lineNo + 1)
                          .arg(matchCount)
                          .arg(ms, 0, 'f', 2);
    summary += engine.jitState() == 1 ? QStringLiteral(" · JIT 已启用")
                                      : QStringLiteral(" · JIT 不可用（已回退解释执行）");
    if (hasTime)
        summary += QStringLiteral("<br/><span style='color:#147a3c'>✓ 检测到 time 组</span>");
    else
        summary += QStringLiteral("<br/><span style='color:#9a6700'>△ 没有 time 组，时间筛选不可用</span>");
    if (hasLevel)
        summary += QStringLiteral("<span style='color:#147a3c'>　✓ 检测到 level 组</span>");
    else
        summary += QStringLiteral("<span style='color:#9a6700'>　△ 没有 level 组，等级配色不可用</span>");

    m_summary->setText(summary);
    m_sampleEdit->setExtraSelections(selections);

    if (m_groups->rowCount() >= tableLimit)
        m_summary->setText(m_summary->text() + QStringLiteral("<br/><i>（表格仅显示前 %1 条）</i>")
                                                     .arg(tableLimit));
}
