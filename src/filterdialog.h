#pragma once

#include "logdocument.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

// 筛选条件对话框（非模态，改动即时生效，不需要按“确定”）
class FilterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterDialog(QWidget *parent = nullptr);

    void setDocument(LogDocument *doc);
    FilterSpec currentSpec() const;
    void resetFilters();
    void focusKeyword();

signals:
    void filterChanged(const FilterSpec &spec);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void scheduleApply();
    void applyNow();
    void updateTimeBounds();
    void updateLevelLabels();
    void updateEnabledState();

    LogDocument *m_doc = nullptr;

    QCheckBox     *m_timeEnabled = nullptr;
    QDateTimeEdit *m_from = nullptr;
    QDateTimeEdit *m_to = nullptr;
    QLabel        *m_timeHint = nullptr;

    QVector<QCheckBox *> m_levelChecks;

    QLineEdit *m_text = nullptr;
    QCheckBox *m_textRegex = nullptr;
    QCheckBox *m_textCase = nullptr;
    QCheckBox *m_highlightOnly = nullptr;

    QTimer *m_debounce = nullptr;
    bool    m_updating = false;
};
