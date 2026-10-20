#pragma once

#include <QDialog>

class QTextBrowser;
class QLineEdit;

// PCRE2 语法速查
class RegexHelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegexHelpDialog(QWidget *parent = nullptr);

    static QString helpHtml();

private:
    void findText(bool forward);

    QTextBrowser *m_view = nullptr;
    QLineEdit    *m_search = nullptr;
};
