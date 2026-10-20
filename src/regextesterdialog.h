#pragma once

#include "logpattern.h"

#include <QDialog>
#include <QTextEdit>

class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTimer;

// 正则表达式测试器：左边样本、右边结果，实时匹配
class RegexTesterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegexTesterDialog(const PatternProfile &profile, const QString &sampleText,
                               QWidget *parent = nullptr);

    QString pattern() const;
    void    setPattern(const QString &pattern);
    void    setSampleText(const QString &text);

signals:
    void patternApplied(const QString &pattern);

private:
    void runTest();
    void applyToProfile();

    QPlainTextEdit *m_patternEdit = nullptr;
    QPlainTextEdit *m_sampleEdit = nullptr;
    QLabel         *m_summary = nullptr;
    QLabel         *m_error = nullptr;
    QTableWidget   *m_groups = nullptr;
    QTimer         *m_debounce = nullptr;

    QString m_initialPattern;
};
