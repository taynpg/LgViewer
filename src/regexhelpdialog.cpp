#include "regexhelpdialog.h"

#include "common.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

QString RegexHelpDialog::helpHtml()
{
    return QString::fromUtf8(R"HTML(
<html><head><style>
body { font-family: "Microsoft YaHei UI", "PingFang SC", sans-serif; font-size: 10.5pt; line-height: 1.6; color:#1f2328; }
h2 { font-size: 13pt; margin: 18px 0 6px; border-bottom: 1px solid #e3e7ec; padding-bottom: 4px; }
h3 { font-size: 11pt; margin: 14px 0 4px; color:#0b5cad; }
code { font-family: Consolas, "Cascadia Mono", monospace; background:#f2f4f7; padding: 1px 4px; border-radius: 3px; color:#b31d28; }
table { border-collapse: collapse; margin: 6px 0 10px; width: 100%; }
th { text-align: left; background:#f6f8fa; font-weight: 600; padding: 5px 8px; border:1px solid #e3e7ec; }
td { padding: 4px 8px; border:1px solid #e3e7ec; vertical-align: top; }
td.c { font-family: Consolas, "Cascadia Mono", monospace; white-space: nowrap; color:#b31d28; width: 26%; }
.note { background:#fff8e5; border-left: 3px solid #d4a72c; padding: 8px 10px; margin: 8px 0; }
.tip  { background:#eef6ff; border-left: 3px solid #0969da; padding: 8px 10px; margin: 8px 0; }
</style></head><body>

<h2>这个工具怎么用</h2>
<p>工具是<b>逐行</b>匹配的：每一行文本单独作为正则的输入，行尾换行符不参与匹配。表达式里不需要写 <code>\n</code>。</p>
<p>通过<b>命名捕获组</b>告诉工具「哪个部分是时间、哪个是等级」，命名固定为下面几个（大小写不敏感，可只写用得到的）：</p>
<table>
<tr><th>组名</th><th>含义</th><th>作用</th></tr>
<tr><td class="c">time</td><td>时间戳</td><td>用于时间筛选；支持 <code>2026-05-29 08:44:46.210</code>、<code>14:28:12.588</code> 等常见写法</td></tr>
<tr><td class="c">level</td><td>等级</td><td>用于等级筛选与配色；info/debug/warn/error/fatal 等自动归类</td></tr>
<tr><td class="c">msg</td><td>正文</td><td>消息主体，长文本用它区分颜色</td></tr>
<tr><td class="c">loc</td><td>源码位置</td><td>可选，显示为灰色弱化</td></tr>
<tr><td class="c">tid</td><td>线程 / PID</td><td>可选，显示在右侧字段面板</td></tr>
</table>
<p>写法示例（对应你日志的第一种格式）：</p>
<p><code>^\[(?&lt;time&gt;[^\]]+)\]\s*\[(?&lt;level&gt;[^\]]+)\]:?\s*(?&lt;msg&gt;.*)$</code></p>

<div class="tip">
<b>命名组的两种写法</b>：<code>(?&lt;name&gt;...)</code> 与 <code>(?P&lt;name&gt;...)</code> 等价。取值用 <code>\k&lt;name&gt;</code>。
</div>

<h2>一、字符与元字符</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">.</td><td>任意一个字符（默认不含换行；<code>(?s)</code> 后可含）</td></tr>
<tr><td class="c">\d \D</td><td>数字 / 非数字。开启 UCP 后 <code>\d</code> 也匹配全角数字等 Unicode 数字</td></tr>
<tr><td class="c">\w \W</td><td>单词字符（字母、数字、下划线）/ 非单词字符。UCP 下包含中文</td></tr>
<tr><td class="c">\s \S</td><td>空白（空格、Tab、换行）/ 非空白</td></tr>
<tr><td class="c">\b \B</td><td>单词边界 / 非边界，例如 <code>\bINFO\b</code> 避免匹配到 <code>INFOX</code></td></tr>
<tr><td class="c">^ $</td><td>行首 / 行尾（逐行匹配时即整行首尾）</td></tr>
<tr><td class="c">\A \z \Z</td><td>文本开头 / 文本结尾 / 文本结尾（允许末尾换行）</td></tr>
<tr><td class="c">\n \t \r</td><td>换行、制表符、回车</td></tr>
<tr><td class="c">\x41 \x{4e2d}</td><td>十六进制字符（后者按码点，<code>\x{4e2d}</code> 就是「中」）</td></tr>
<tr><td class="c">\Q...\E</td><td>引号内的字符全部按字面量处理，适合拼路径</td></tr>
</table>

<h2>二、字符类</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">[abc]</td><td>a、b、c 之一</td></tr>
<tr><td class="c">[^abc]</td><td>除 a、b、c 之外的任意字符</td></tr>
<tr><td class="c">[a-z0-9]</td><td>范围。注意 <code>-</code> 放开头或结尾表示字面量</td></tr>
<tr><td class="c">[^\]]</td><td>非 <code>]</code>，日志里解析方括号最常用（也可写成 <code>[^]]</code>）</td></tr>
<tr><td class="c">[[:alpha:]]</td><td>POSIX 类：alpha / digit / alnum / space / punct / upper / lower</td></tr>
</table>

<h2>三、量词与贪婪</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">*</td><td>0 次或多次</td></tr>
<tr><td class="c">+</td><td>1 次或多次</td></tr>
<tr><td class="c">?</td><td>0 次或 1 次，也用于让前面「可选」，如 <code>:?</code></td></tr>
<tr><td class="c">{n} {n,} {n,m}</td><td>恰好 n 次 / 至少 n 次 / n 到 m 次</td></tr>
<tr><td class="c">*? +? ??</td><td>懒惰（最少匹配）。<code>.*?</code> 是日志解析的主力</td></tr>
<tr><td class="c">*+ ++ ?+</td><td>独占（不回退），性能最好，但要求表达式写得足够精确</td></tr>
</table>
<div class="note">
<b>贪婪是最大的坑</b>：<code>(?&lt;msg&gt;.*)@(?&lt;loc&gt;.*)</code> 会一直吃到<b>最后一个</b> @ 之前；想让 msg 停在第一个 @ 前，必须写 <code>(?&lt;msg&gt;.*?)@</code>。
</div>

<h2>四、分组与引用</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">(...)</td><td>捕获组，可用序号 <code>\1</code> 引用</td></tr>
<tr><td class="c">(?:...)</td><td>非捕获组，只分组不占编号，省性能</td></tr>
<tr><td class="c">(?&lt;name&gt;...)</td><td>命名捕获组（本工具靠它识别字段）</td></tr>
<tr><td class="c">(?>...)</td><td>原子组，放弃内部回溯</td></tr>
<tr><td class="c">\1 \k&lt;name&gt;</td><td>反向引用，如 <code>(\w+) ... \1</code> 匹配重复出现的同一个词</td></tr>
<tr><td class="c">(?(1)a|b)</td><td>条件：第 1 组匹配成功则用 a，否则用 b</td></tr>
</table>

<h2>五、环视（零宽断言）</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">(?=...)</td><td>后面必须是：<code>error(?=\.log)</code></td></tr>
<tr><td class="c">(?!...)</td><td>后面必须不是：<code>INFO(?!RM)</code></td></tr>
<tr><td class="c">(?&lt;=...)</td><td>前面必须是（定长）：<code>(?&lt;=\[)\w+(?=\])</code></td></tr>
<tr><td class="c">(?&lt;!...)</td><td>前面必须不是</td></tr>
</table>

<h2>六、内联选项</h2>
<table>
<tr><th>写法</th><th>说明</th></tr>
<tr><td class="c">(?i)</td><td>忽略大小写，可写成分组形式 <code>(?i:info)</code></td></tr>
<tr><td class="c">(?m)</td><td>多行模式：<code>^ $</code> 匹配每行首尾（本工具逐行，一般用不到）</td></tr>
<tr><td class="c">(?s)</td><td>点号匹配换行</td></tr>
<tr><td class="c">(?x)</td><td>宽松模式：忽略空白并支持 <code>#</code> 注释，长表达式排版更清爽</td></tr>
<tr><td class="c">(?U)</td><td>反转贪婪：量词默认变懒惰</td></tr>
</table>

<h2>七、日志解析常用片段</h2>
<table>
<tr><th>目标</th><th>表达式</th></tr>
<tr><td>日期时间（毫秒）</td><td class="c">\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}[.,]\d{3}</td></tr>
<tr><td>仅时分秒</td><td class="c">\d{2}:\d{2}:\d{2}[.,]\d{3}</td></tr>
<tr><td>等级（不区分大小写）</td><td class="c">(?i)\b(trace|debug|info|warn|error|fatal)\b</td></tr>
<tr><td>方括号字段</td><td class="c">\[([^\]]*)\]</td></tr>
<tr><td>源码位置 file:func:line</td><td class="c">//[\w./\\-]+:\w+:\d+</td></tr>
<tr><td>线程号（行末逗号后数字）</td><td class="c">,(?&lt;tid&gt;\d+)\s*$</td></tr>
<tr><td>IPv4</td><td class="c">\b(?:\d{1,3}\.){3}\d{1,3}\b</td></tr>
<tr><td>十六进制报文</td><td class="c">\b(?:[0-9A-Fa-f]{2}\s?){4,}\b</td></tr>
</table>

<h2>八、常见坑</h2>
<ul>
<li><code>.*</code> 贪婪：见第三节。日志解析几乎都应该用 <code>.*?</code>。</li>
<li><b>字面量要转义</b>：<code>. ^ $ * + ? ( ) [ ] { } | \</code> 在正则里有特殊含义，匹配它们本身要加反斜杠，例如匹配点号用 <code>\.</code>。</li>
<li><b>结尾锚点被忽略</b>：如果表达式末尾没有 <code>$</code>，<code>.*</code> 之外的残留内容不会报错，容易误匹配。要严格整行匹配就补 <code>$</code>。</li>
<li><b>中文</b>：本工具编译时开启了 UTF + UCP，<code>\w</code>、<code>\d</code> 都已 Unicode 化，中文可以直接写进表达式。</li>
<li><b>性能</b>：避免 <code>(.*)*</code>、<code>(a+)+</code> 这类嵌套量词，会出现灾难性回溯。日志解析尽量用「能确定边界」的写法，例如 <code>[^\]]*</code> 优于 <code>.*?</code>。</li>
<li><b>逐行匹配</b>：一条日志跨多行时，用「续行归并」把它们合并到上一条，而不是让正则去吞换行。</li>
</ul>

<h2>九、本工具的正则引擎</h2>
<p>底层是 <b>PCRE2</b>（Perl Compatible Regular Expressions 第 2 版），开启 <code>PCRE2_UTF</code> + <code>PCRE2_UCP</code> 与 <b>JIT</b> 编译。因此 Perl / PHP / 大多数编辑器里能用的写法这里都能用，包括反向引用、环视、原子组、递归、条件组。</p>
<p>不支持的只有 PCRE2 本身也不做的部分：变长后行断言（lookbehind 内的 <code>*</code>、<code>+</code>）需要改写为 <code>\K</code> 或调整分组。</p>

</body></html>
)HTML");
}

RegexHelpDialog::RegexHelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("正则表达式速查（PCRE2）"));
    resize(880, 720);

    auto *root = new QVBoxLayout(this);

    auto *bar = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("在说明里查找…"));
    auto *prevBtn = new QPushButton(QStringLiteral("上一个"), this);
    auto *nextBtn = new QPushButton(QStringLiteral("下一个"), this);
    bar->addWidget(m_search, 1);
    bar->addWidget(prevBtn);
    bar->addWidget(nextBtn);
    root->addLayout(bar);

    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(false);
    m_view->setHtml(helpHtml());
    root->addWidget(m_view, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    connect(prevBtn, &QPushButton::clicked, this, [this] { findText(false); });
    connect(nextBtn, &QPushButton::clicked, this, [this] { findText(true); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] { findText(true); });
}

void RegexHelpDialog::findText(bool forward)
{
    const QString text = m_search->text();
    if (text.isEmpty())
        return;
    const QTextDocument::FindFlags flags = forward ? QTextDocument::FindFlags()
                                                   : QTextDocument::FindBackward;
    if (!m_view->find(text, flags))
        m_view->moveCursor(forward ? QTextCursor::Start : QTextCursor::End);
}
