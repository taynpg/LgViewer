#include "profiledialog.h"

#include "common.h"
#include "regextesterdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ProfileDialog::ProfileDialog(const PatternProfile &profile, QWidget *parent)
    : QDialog(parent)
    , m_originalPattern(profile.pattern)
{
    setWindowTitle(QStringLiteral("编辑日志结构配置"));
    resize(760, 460);

    auto *root = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_name = new QLineEdit(profile.name, this);
    form->addRow(QStringLiteral("名称"), m_name);

    m_timeFormat = new QLineEdit(profile.timeFormat, this);
    m_timeFormat->setPlaceholderText(QStringLiteral("留空自动识别，例如 yyyy-MM-dd HH:mm:ss.zzz"));
    form->addRow(QStringLiteral("时间格式"), m_timeFormat);
    root->addLayout(form);

    root->addWidget(new QLabel(QStringLiteral("日志结构正则（逐行匹配，命名组：time / level / msg / loc / tid）"),
                               this));

    m_pattern = new QPlainTextEdit(this);
    QFont mono(lv::monospaceFamily());
    mono.setPointSize(10);
    m_pattern->setFont(mono);
    m_pattern->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4);
    m_pattern->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_pattern->setPlainText(profile.pattern);
    root->addWidget(m_pattern, 1);

    m_multiline = new QCheckBox(
        QStringLiteral("续行归并：未匹配的行归到上一条日志（用于异常堆栈、报文分段）"), this);
    m_multiline->setChecked(profile.multiline);
    root->addWidget(m_multiline);

    auto *bottom = new QHBoxLayout;
    auto *testBtn = new QPushButton(QStringLiteral("用样本测试…"), this);
    bottom->addWidget(testBtn);
    bottom->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(buttons);
    root->addLayout(bottom);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_pattern->toPlainText().trimmed().isEmpty()) {
            m_pattern->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(testBtn, &QPushButton::clicked, this, &ProfileDialog::openTester);
}

PatternProfile ProfileDialog::profile() const
{
    PatternProfile p;
    p.name = m_name->text().trimmed();
    if (p.name.isEmpty())
        p.name = QStringLiteral("未命名配置");
    p.pattern = m_pattern->toPlainText().trimmed();
    p.timeFormat = m_timeFormat->text().trimmed();
    p.multiline = m_multiline->isChecked();
    return p;
}

void ProfileDialog::openTester()
{
    PatternProfile p = profile();
    RegexTesterDialog dlg(p, QString(), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_pattern->setPlainText(dlg.pattern());
    }
}
