#pragma once

#include "logpattern.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// 编辑单条日志结构配置
class ProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileDialog(const PatternProfile &profile, QWidget *parent = nullptr);

    PatternProfile profile() const;

private:
    void openTester();

    QLineEdit      *m_name = nullptr;
    QPlainTextEdit *m_pattern = nullptr;
    QLineEdit      *m_timeFormat = nullptr;
    QCheckBox      *m_multiline = nullptr;
    QString         m_originalPattern;
};
