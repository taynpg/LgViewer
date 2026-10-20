#pragma once

#include <QDate>
#include <QString>
#include <QtGlobal>

// 时间戳解析：先走快速手写路径（覆盖 99% 的日志格式），失败再退回 QDateTime
class TimestampParser
{
public:
    void reset();
    // 空字符串 = 自动识别
    void setExplicitFormat(const QString &fmt);
    void setBaseDate(const QDate &date);

    // 返回毫秒时间戳；解析失败返回 -1
    qint64 parse(const QString &text);

    // 今天（基准日）+ 天数偏移，供界面构造默认筛选范围
    QDate baseDate() const { return m_baseDate; }

    static QStringList knownFormats();

private:
    qint64 parseWithExplicit(const QString &text);
    qint64 parseAuto(const QString &text);

    QString m_explicit;
    QDate   m_baseDate = QDate::currentDate();
    qint64  m_lastMs = -1;
    int     m_dayShift = 0;
};
