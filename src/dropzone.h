#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

// 拖拽文件进入窗口时铺满窗口的提示层：半透明底 + 居中虚框卡片。
// 只做视觉反馈，鼠标事件全部穿透（WA_TransparentForMouseEvents）。
class DropOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit DropOverlay(QWidget *parent = nullptr);

    // 显示"松开即打开"的提示；currentPath 与拖入文件相同时改提示为重新加载
    void showFiles(const QStringList &paths, const QString &currentPath = QString());
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QString m_detail;
    QString m_info;
};
