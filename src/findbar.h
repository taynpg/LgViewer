#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

// 底部查找栏：输入框 + 大小写/全字开关 + 命中计数 + 上一个/下一个 + 关闭
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    // 显示并聚焦（已有内容时全选，方便直接改）
    void activate(const QString &seed = QString());

    QString text() const;
    bool    caseSensitive() const;
    bool    wholeWord() const;
    void    setCaseSensitive(bool on);
    void    setWholeWord(bool on);

    // 状态提示
    void showIdle();
    void showPending();
    void showCount(int total, bool truncated);
    void showPosition(int index1Based, int total, bool truncated);
    void showNoMatch();

signals:
    void queryChanged(); // 关键词或开关变化，需要重新搜索
    void goNext();
    void goPrev();
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyStyle();
    void scheduleQuery();

    QLineEdit   *m_edit = nullptr;
    QLabel      *m_count = nullptr;
    QToolButton *m_case = nullptr;
    QToolButton *m_word = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_close = nullptr;
    QTimer      *m_debounce = nullptr;
};
