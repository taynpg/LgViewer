#include "regexpromptdialog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kMaxSampleLines = 28;
constexpr int kMaxSampleChars = 400;

// 样例里如果出现 ``` 会把 Markdown 代码块截断，换成等长的引号
QString sanitize(const QString &line)
{
    QString s = line;
    s.replace(QLatin1String("```"), QStringLiteral("'''"));
    if (s.size() > kMaxSampleChars)
        s = s.left(kMaxSampleChars) + QStringLiteral("   …（本行超长，已截断）");
    return s;
}

} // namespace

QString RegexPromptDialog::buildPrompt(const QStringList &sampleLines)
{
    QString text = QString::fromUtf8(R"PROMPT(# 任务：为 LgViewer 编写一条日志解析正则（PCRE2）

你是正则表达式专家。请阅读文末的日志样例，为 LgViewer 日志查看器写出一条能完整解析这份日志的正则表达式，并逐行验证。

## 一、运行环境（硬约束，必须遵守）
- 引擎：PCRE2 10.48，编译选项 PCRE2_UTF | PCRE2_UCP，并以 JIT 编译执行。
- 匹配方式：文件按行切分后【逐行】送入 pcre2_match，一行一个输入字符串，输入里不包含换行符。
- 因此：^ 和 $ 就是整行的开头和结尾；不要写 \n，也不需要 (?m) 或 (?s)。
- 命名组用 (?<name>...)（(?P<name>...) 同样支持），反向引用写 \k<name>。
- 可以使用环视、原子组、条件组；但后行断言 (?<=...) / (?<!...) 必须是定长，变长写法会被拒绝。
- 单行可能有几千字符（例如十六进制报文、超长 JSON），表达式绝不能有灾难性回溯。

## 二、字段约定（最关键：只认下面 5 个命名组）
| 组名 | 必要性 | 含义 | 工具会怎么用 |
| time  | 强烈建议 | 时间戳原文 | 交给内置时间解析器换算成毫秒，用于时间范围筛选、时间显示 |
| level | 强烈建议 | 等级原文（Info / WARNING / 错误 …） | 大小写不敏感归一化为 TRACE/DEBUG/INFO/WARN/ERROR/FATAL，决定配色与等级筛选 |
| msg   | 建议     | 日志正文 | 决定正文从哪里开始着色；续行归并时显示在首行 |
| loc   | 可选     | 源码位置（file:func:line 之类） | 灰色弱化显示 |
| tid   | 可选     | 线程号 / 进程号 | 预留字段，暂不参与筛选 |

等价别名（用哪个都行，名称大小写不敏感）：
- time  ← timestamp / ts / datetime / date
- level ← lvl / sev / severity / loglevel
- msg   ← message / text / content / body
- loc   ← location / source / src / file / where

注意：
1. level 组只抓等级词本身，不要带 [ ] : 这类装饰符号（工具会 trim，但干净更稳）。
2. time 组只抓时间本身，不要把后面的空格、逗号、右括号带进来。
3. msg 建议写成「从固定标记之后一直到行尾」：`(?<msg>.*)$`，这样正文里的空格、Tab、中文都能原样保留。
4. 一个命名组都不写也能匹配，但那样所有行都等于「未解析」，没有等级配色、也无法按时间/等级筛选，所以至少要给 time 和 level。

## 三、时间格式
以下写法 time 组抓到原文后会自动识别（timeFormat 留空即可）：
  2026-05-29 08:44:46.210      2026-05-29 08:44:46,210      2026-05-29 08:44:46
  2026-05-29T08:44:46.210      2026/05/29 08:44:46.210      2026/05/29 08:44:46
  05-29 08:44:46.210           08:44:46.588                 08:44:46
  20260529 08:44:46.210        29/May/2026:08:44:46          10 位秒级 / 13 位毫秒级 Unix 时间戳
- 只有时间、没有日期也可以：工具会以文件修改日期为基准，并在时间回绕（跨天）时自动 +1 天。
- 若是其他格式（dd/MM/yyyy、带时区偏移的 ISO8601、中文日期等），请额外给出 Qt 的 QDateTime::fromString 格式串，例如 "yyyy-MM-dd HH:mm:ss.zzz"、"dd/MMM/yyyy:HH:mm:ss"，我填到 timeFormat 里。
- 若时间戳是数字（如 1716960286），直接把整串放进 time 组。

## 四、等级归一化（正则里不要自己做映射）
level 组抓到原文即可，不要写死成某个固定值，也不要用正则把 WARNING 改写成 WARN。工具按「先整词、后包含」的方式、忽略大小写判定：
  trace / verbose / v                → TRACE
  debug / dbg / d                    → DEBUG
  info / information / notice / ok   → INFO
  warn / warning / 警告              → WARN
  error / err / fail / severe / 错误 → ERROR
  fatal / critical / crit / panic    → FATAL
  匹配不到                           → 其他（灰色）
所以 `[Info]`、`[INFO]`、`[warn]`、`[WARNING]`、`[Error]` 都能正确归位，表达式只需要把方括号里的词抓住。

## 五、多行（续行）处理
- 没有匹配上的行，会被自动归并到上一条日志，沿用上一条的时间与等级，缩进显示。
- 所以正则只需要匹配「一条日志的第一行」，不要试图跨行匹配（输入里本来也没有换行符）。
- 反过来：如果日志里有多种首行形态（普通记录、分隔线、报文 dump 起始行、异常标题行…），必须都纳入同一个表达式（用 | 分支或可选组），否则它们会被错误地并进上一条，导致条数与等级统计错乱。
- 堆栈、缩进的续行、纯空白行，请保持「不匹配」，交给续行归并处理。

## 六、写法要求
1. 整行锚定：^ 开头、$ 结尾，避免命中行中间的相似文本。
2. 边界要确定：[^\]]*、[^,;]*、[^ ]* 这类比 .*? 更快，也不会回溯爆炸。
3. 禁止嵌套量词：(.*)*、(a+)+、(\w*)*，百万行日志会直接卡死。
4. 字面量转义：. ^ $ * + ? ( ) [ ] { } | \ 在正则中有特殊含义，要匹配它们本身必须加反斜杠，例如 \[ \] \( \) \. \- 。
5. 可能缺失的片段用 (?:...)? 包起来；空格数量不定用 \s* / \s+ 容错，不要让整行失配。
6. 表达式写成一行，不要换行、不要加注释；若确实需要可读性而使用 (?x)，注意空格会被忽略，需要写成 \s 或 \x20。
7. 中文日志可以直接写中文字面量；UTF 模式下 \w \d 已是 Unicode 语义，中文也能被 \w 匹配。
8. 覆盖率优先：除堆栈/续行外，所有行都应命中。宁可略宽松，也要保证不漏。
9. 性能优先：优先用「锚定 + 确定边界」，避免大量分支回溯；必要时用原子组 (?>...)。
10. 不要使用递归 (?R) 之类的花招，工具是逐行匹配，用不上。

## 七、请严格按下面的格式回答
1) 正则：单独一行，放在代码块里，方便我直接复制粘贴。
2) timeFormat：没有特殊格式就写「留空」。
3) 分组说明：表格列出 组名 | 对应日志片段 | 说明。
4) 逐行验证：用我给的每一行样例实际匹配一次，表格列出 原始行（可截断）| time | level | msg | 是否匹配。必须覆盖我提供的全部样例行。
5) 未匹配说明：哪些行不匹配、原因是什么、会被归并到哪一条。
6) 兼容性：如果样例里存在差异很大的多种格式，请优先给一条通用表达式；实在无法兼容时，再给多条并说明如何按文件/前缀区分。
7) 最后用一句话总结：这份日志的结构是「时间 + 等级 + 正文 + 附加字段」中的哪一种排列。

## 八、日志样例
<<<LOG_SAMPLES>>>

补充：如果除了上面的样例还有其他形态（换行堆栈、超长报文、中文乱码行、文件开头若干行是启动信息），我会再补给你，请以实际样例为准。
)PROMPT");

    QString block;
    if (sampleLines.isEmpty()) {
        block = QStringLiteral("```text\n"
                               "（在这里粘贴日志原文，建议 10~30 行，保留行首空格与 Tab）\n"
                               "```");
    } else {
        QStringList lines;
        lines.reserve(sampleLines.size());
        for (const QString &l : sampleLines)
            lines.append(sanitize(l));
        block = QStringLiteral("```text\n%1\n```").arg(lines.join(QLatin1Char('\n')));
    }
    text.replace(QStringLiteral("<<<LOG_SAMPLES>>>"), block);
    return text;
}

RegexPromptDialog::RegexPromptDialog(const QStringList &sampleLines, QWidget *parent)
    : QDialog(parent)
    , m_samples(sampleLines)
{
    setWindowTitle(QStringLiteral("正则编写介绍 — 复制给 AI，让它帮我写正则"));
    resize(920, 780);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *hint = new QLabel(this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    hint->setText(QStringLiteral(
        "<b>怎么用：</b>① 复制下面的全文 → ② 连同日志样例一起发给 AI（ChatGPT / Claude / DeepSeek 等）"
        " → ③ 把 AI 给出的正则粘到 <b>Ctrl+T 正则测试器</b>里验证，满意后「应用为当前配置」。<br>"
        "<span style=\"color:#6a737d\">这段文字已经写明了本工具的命名组约定、时间与等级识别规则、性能禁忌和期望的回答格式，"
        "AI 照着写出来的表达式可以直接使用。</span>"));
    root->addWidget(hint);

    auto *bar = new QHBoxLayout;
    if (!m_samples.isEmpty()) {
        m_useSamples = new QCheckBox(
            QStringLiteral("末尾附带当前日志的样例行（%1 行，推荐）").arg(m_samples.size()), this);
        m_useSamples->setChecked(true);
        m_useSamples->setToolTip(
            QStringLiteral("从已打开的日志里挑出不同等级、不同类型（含续行）的行，"
                           "一起复制给 AI，它就能直接对着真实数据写式子"));
        connect(m_useSamples, &QCheckBox::toggled, this, [this] { refresh(); });
        bar->addWidget(m_useSamples);
    } else {
        auto *tip = new QLabel(QStringLiteral("未打开日志文件：复制后请自行粘贴日志样例"), this);
        tip->setStyleSheet(QStringLiteral("color:#b08800;"));
        bar->addWidget(tip);
    }
    bar->addStretch(1);
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#6a737d;"));
    bar->addWidget(m_status);
    root->addLayout(bar);

    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_edit->setFont(mono);
    m_edit->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4);
    root->addWidget(m_edit, 1);

    auto *buttons = new QHBoxLayout;
    auto *saveBtn = new QPushButton(QStringLiteral("保存为文件…"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    m_copyBtn = new QPushButton(QStringLiteral("复制全部内容"), this);
    m_copyBtn->setDefault(true);
    m_copyBtn->setMinimumWidth(150);
    buttons->addWidget(saveBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_copyBtn);
    buttons->addWidget(closeBtn);
    root->addLayout(buttons);

    connect(m_copyBtn, &QPushButton::clicked, this, &RegexPromptDialog::copyAll);
    connect(saveBtn, &QPushButton::clicked, this, &RegexPromptDialog::saveToFile);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
    m_edit->moveCursor(QTextCursor::Start);
}

QString RegexPromptDialog::currentText() const
{
    const bool withSamples = m_useSamples && m_useSamples->isChecked();
    return buildPrompt(withSamples ? m_samples : QStringList());
}

void RegexPromptDialog::refresh()
{
    const QString text = currentText();
    m_edit->setPlainText(text);
    m_status->setText(QStringLiteral("共 %1 字符").arg(text.size()));
}

void RegexPromptDialog::copyAll()
{
    const QString text = m_edit->toPlainText();
    QGuiApplication::clipboard()->setText(text);
    m_status->setText(QStringLiteral("已复制 %1 字符到剪贴板").arg(text.size()));

    m_copyBtn->setText(QStringLiteral("已复制 ✓"));
    QTimer::singleShot(1400, this, [this] {
        if (m_copyBtn)
            m_copyBtn->setText(QStringLiteral("复制全部内容"));
    });
}

void RegexPromptDialog::saveToFile()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存正则编写介绍"), QStringLiteral("LgViewer-正则需求.md"),
        QStringLiteral("Markdown (*.md);;文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString());
        return;
    }
    f.write(m_edit->toPlainText().toUtf8());
    if (!f.commit()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString());
        return;
    }
    m_status->setText(QStringLiteral("已保存：%1").arg(path));
}
