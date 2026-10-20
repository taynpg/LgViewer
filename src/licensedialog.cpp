#include "licensedialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {

QString readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("（许可文本缺失：%1）").arg(path);
    return QString::fromUtf8(f.readAll());
}

QPlainTextEdit *makeLicenseView(const QString &path, QWidget *parent)
{
    auto *view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    view->setFont(mono);
    view->setPlainText(readResource(path));
    view->moveCursor(QTextCursor::Start);
    return view;
}

QString overviewHtml()
{
    const QString build = QString::fromLatin1(QT_VERSION_STR);
    const QString runtime = QString::fromLatin1(qVersion());
    char pcre[64] = {0};
    pcre2_config(PCRE2_CONFIG_VERSION, pcre);

    return QStringLiteral(
        "<html><head><style>"
        "body { font-family: 'Microsoft YaHei UI', 'PingFang SC', sans-serif; font-size: 10.5pt;"
        "       line-height: 1.65; color:#1f2328; }"
        "h3 { font-size: 13pt; margin: 0 0 4px; }"
        "h4 { font-size: 11pt; margin: 16px 0 6px; color:#0b5cad; }"
        "table { border-collapse: collapse; margin: 8px 0; width: 100%; }"
        "th { text-align: left; background:#f6f8fa; font-weight: 600; padding: 5px 8px;"
        "     border:1px solid #e3e7ec; width: 24%; }"
        "td { padding: 5px 8px; border:1px solid #e3e7ec; }"
        "code { font-family: Consolas, monospace; background:#f2f4f7; padding: 1px 4px;"
        "       border-radius: 3px; }"
        "ul { margin: 4px 0 4px 18px; padding: 0; }"
        "li { margin: 3px 0; }"
        ".tip { background:#eef6ff; border-left: 3px solid #0969da; padding: 8px 10px; margin: 10px 0; }"
        "</style></head><body>"

        "<h3>Qt %1</h3>"
        "<p>LgViewer 使用 <b>Qt 开源版</b> 构建，界面部分完全基于 Qt Widgets。"
        "版本与版权信息也可通过「帮助 → 关于 Qt」查看（Qt 自带对话框）。</p>"

        "<table>"
        "<tr><th>编译时版本</th><td>Qt %1</td></tr>"
        "<tr><th>运行时版本</th><td>Qt %2</td></tr>"
        "<tr><th>许可证</th><td><b>LGPL v3</b>（GNU Lesser General Public License, version 3）</td></tr>"
        "<tr><th>链接方式</th><td>动态链接（Qt6Core / Qt6Gui / Qt6Widgets 以独立 DLL / .so 形式随程序发布）</td></tr>"
        "<tr><th>使用模块</th><td>QtCore、QtGui、QtWidgets（均为 LGPL v3 许可的模块）</td></tr>"
        "<tr><th>项目主页</th><td><a href=\"https://www.qt.io/\">https://www.qt.io/</a>　"
        "<a href=\"https://doc.qt.io/qt-6/lgpl.html\">Qt 6 与 LGPL 说明</a></td></tr>"
        "</table>"

        "<h4>LGPL v3 赋予你的权利</h4>"
        "<ul>"
        "<li><b>可替换 Qt 库</b>：本程序动态链接 Qt，你可将随附的 Qt6*.dll（或 .so）替换为"
        "自行编译的、接口兼容的版本，而无需重新编译 LgViewer。</li>"
        "<li><b>可获取 Qt 源码</b>：Qt %2 的完整源码可从 "
        "<a href=\"https://download.qt.io/official_releases/qt/\">download.qt.io</a> 或 "
        "<a href=\"https://code.qt.io/\">code.qt.io</a> 获取。</li>"
        "<li><b>修改 Qt 需回馈</b>：如果分发修改过的 Qt 库，需按 LGPL v3 第 4 条以源码形式提供这些修改。</li>"
        "<li><b>本程序自身代码</b>采用 MIT 许可（见仓库 LICENSE），不受 LGPL 的传染性约束。</li>"
        "</ul>"

        "<div class=\"tip\">"
        "LGPL v3 全文见本窗口的「LGPL v3 全文」标签页；仓库内同时提供 "
        "<code>LICENSES/LGPL-3.0-only.txt</code>，发布压缩包时请一并附带。"
        "</div>"

        "<h4>第三方组件</h4>"
        "<table>"
        "<tr><th>PCRE2 %3</th><td>正则表达式引擎，<b>BSD-3-Clause</b> 许可（含 PCRE2 二进制分发例外）。"
        "全文见「PCRE2 许可」标签页，仓库内为 <code>LICENSES/PCRE2-BSD-3-Clause.txt</code>。</td></tr>"
        "<tr><th>Qt 内置组件</th><td>zlib、libpng、FreeType、PCRE2、Unicode 数据等，"
        "各自许可见 Qt 源码包 <code>LICENSES/</code> 目录。</td></tr>"
        "</table>"
        "</body></html>")
        .arg(build, runtime, QString::fromLatin1(pcre));
}

} // namespace

LicenseDialog::LicenseDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("开源许可与第三方组件"));
    resize(820, 680);

    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    auto *overview = new QTextBrowser(m_tabs);
    overview->setOpenExternalLinks(true);
    overview->setHtml(overviewHtml());
    m_tabs->addTab(overview, QStringLiteral("概览"));

    m_tabs->addTab(makeLicenseView(QStringLiteral(":/licenses/LGPL-3.0-only.txt"), m_tabs),
                   QStringLiteral("LGPL v3 全文"));
    m_tabs->addTab(makeLicenseView(QStringLiteral(":/licenses/PCRE2-BSD-3-Clause.txt"), m_tabs),
                   QStringLiteral("PCRE2 许可"));

    root->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);
}
