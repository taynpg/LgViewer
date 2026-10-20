#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// 「正则编写介绍」：一份可直接复制给 AI 的需求说明。
// 内含 LgViewer 的命名组约定、时间/等级识别规则、性能禁忌与期望的输出格式，
// 用户把它连同日志样例发给 AI，就能拿到可以直接使用的表达式。
class RegexPromptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegexPromptDialog(const QStringList &sampleLines, QWidget *parent = nullptr);

    static QString buildPrompt(const QStringList &sampleLines);
    QString currentText() const;

private:
    void refresh();
    void copyAll();
    void saveToFile();

    QStringList     m_samples;
    QCheckBox      *m_useSamples = nullptr;
    QPlainTextEdit *m_edit = nullptr;
    QLabel         *m_status = nullptr;
    QPushButton    *m_copyBtn = nullptr;
};
