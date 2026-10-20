#pragma once

#include <QDialog>

class QTabWidget;

// 「开源许可」对话框：说明本程序使用的 Qt 版本与许可方式（LGPL v3 动态链接），
// 并内置 LGPL v3 全文与 PCRE2 的 BSD-3-Clause 许可文本，便于分发时随程序提供。
//
// 注：Qt 自身提供了标准的「关于 Qt」对话框（QMessageBox::aboutQt），
// 那里只有版本号与官方链接；本对话框用于满足 LGPL v3「随程序提供许可全文」的要求。
class LicenseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LicenseDialog(QWidget *parent = nullptr);

private:
    QTabWidget *m_tabs = nullptr;
};
