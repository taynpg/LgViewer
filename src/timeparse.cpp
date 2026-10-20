#include "timeparse.h"

#include <QDateTime>
#include <QTime>

namespace {

constexpr qint64 kEpochJulian = 2440588; // 1970-01-01

inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

inline int num2(const char *p)
{
    return (p[0] - '0') * 10 + (p[1] - '0');
}

inline int num4(const char *p)
{
    return (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
}

// 解析秒后的小数部分（.210 / ,123456）
int parseFraction(const char *p, int len, int pos)
{
    if (pos >= len)
        return 0;
    const char sep = p[pos];
    if (sep != '.' && sep != ',')
        return 0;
    int val = 0;
    int digits = 0;
    while (pos + 1 + digits < len && digits < 3 && isDigit(p[pos + 1 + digits])) {
        val = val * 10 + (p[pos + 1 + digits] - '0');
        ++digits;
    }
    if (digits == 0)
        return 0;
    while (digits < 3) {
        val *= 10;
        ++digits;
    }
    return val;
}

inline qint64 toWallMs(const QDate &d, int todMs)
{
    if (!d.isValid())
        return -1;
    return (qint64(d.toJulianDay()) - kEpochJulian) * 86400000LL + todMs;
}

} // namespace

void TimestampParser::reset()
{
    m_lastMs = -1;
    m_dayShift = 0;
}

void TimestampParser::setExplicitFormat(const QString &fmt)
{
    m_explicit = fmt.trimmed();
}

void TimestampParser::setBaseDate(const QDate &date)
{
    if (date.isValid())
        m_baseDate = date;
}

QStringList TimestampParser::knownFormats()
{
    return {
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss,zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("MM-dd HH:mm:ss.zzz"),
        QStringLiteral("HH:mm:ss.zzz"),
        QStringLiteral("HH:mm:ss"),
        QStringLiteral("yyyyMMdd HH:mm:ss.zzz"),
        QStringLiteral("dd/MMM/yyyy:HH:mm:ss"),
    };
}

qint64 TimestampParser::parse(const QString &text)
{
    if (text.isEmpty())
        return -1;
    if (!m_explicit.isEmpty()) {
        const qint64 v = parseWithExplicit(text);
        if (v >= 0)
            return v;
    }
    return parseAuto(text);
}

qint64 TimestampParser::parseWithExplicit(const QString &text)
{
    QDateTime dt = QDateTime::fromString(text.trimmed(), m_explicit);
    if (!dt.isValid())
        return -1;

    const bool hasYear = m_explicit.contains(QLatin1Char('y'));
    const bool hasDate = hasYear || m_explicit.contains(QLatin1Char('M'))
        || m_explicit.contains(QLatin1Char('d'), Qt::CaseSensitive);

    const QTime t = dt.time();
    const int tod = t.hour() * 3600000 + t.minute() * 60000 + t.second() * 1000 + t.msec();

    if (!hasDate) {
        qint64 ms = toWallMs(m_baseDate.addDays(m_dayShift), tod);
        if (m_lastMs >= 0 && ms < m_lastMs - 60000) {
            ++m_dayShift;
            ms += 86400000LL;
        }
        m_lastMs = ms;
        return ms;
    }

    QDate d = dt.date();
    if (!hasYear)
        d = QDate(m_baseDate.year(), d.month(), d.day());
    return toWallMs(d, tod);
}

qint64 TimestampParser::parseAuto(const QString &text)
{
    const QString s = text.trimmed();
    const QByteArray b = s.toLatin1();
    const char *p = b.constData();
    const int n = b.size();

    // 纯数字：10 位秒 / 13 位毫秒时间戳
    if (n >= 10 && n <= 13) {
        bool allDigits = true;
        for (int i = 0; i < n; ++i) {
            if (!isDigit(p[i])) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            const qint64 v = s.toLongLong();
            return (n <= 10) ? v * 1000 : v;
        }
    }

    // yyyy-MM-dd / yyyy/MM/dd + 时间
    if (n >= 19 && isDigit(p[0]) && isDigit(p[1]) && isDigit(p[2]) && isDigit(p[3])
        && (p[4] == '-' || p[4] == '/') && isDigit(p[5]) && isDigit(p[6])
        && (p[7] == '-' || p[7] == '/') && isDigit(p[8]) && isDigit(p[9])
        && (p[10] == ' ' || p[10] == 'T') && isDigit(p[11]) && isDigit(p[12]) && p[13] == ':'
        && isDigit(p[14]) && isDigit(p[15]) && p[16] == ':' && isDigit(p[17]) && isDigit(p[18])) {
        const QDate d(num4(p), num2(p + 5), num2(p + 8));
        if (!d.isValid())
            return -1;
        const int tod = num2(p + 11) * 3600000 + num2(p + 14) * 60000 + num2(p + 17) * 1000
            + parseFraction(p, n, 19);
        return toWallMs(d, tod);
    }

    // MM-dd HH:mm:ss（缺年份）
    if (n >= 14 && isDigit(p[0]) && isDigit(p[1]) && p[2] == '-' && isDigit(p[3])
        && isDigit(p[4]) && p[5] == ' ' && isDigit(p[6]) && isDigit(p[7]) && p[8] == ':') {
        const QDate d(m_baseDate.year(), num2(p), num2(p + 3));
        if (!d.isValid())
            return -1;
        const int tod = num2(p + 6) * 3600000 + num2(p + 9) * 60000 + num2(p + 12) * 1000
            + parseFraction(p, n, 14);
        return toWallMs(d, tod);
    }

    // HH:mm:ss[.zzz]
    if (n >= 8 && isDigit(p[0]) && isDigit(p[1]) && p[2] == ':' && isDigit(p[3]) && isDigit(p[4])
        && p[5] == ':' && isDigit(p[6]) && isDigit(p[7])) {
        const int tod = num2(p) * 3600000 + num2(p + 3) * 60000 + num2(p + 6) * 1000
            + parseFraction(p, n, 8);
        qint64 ms = toWallMs(m_baseDate.addDays(m_dayShift), tod);
        if (m_lastMs >= 0 && ms < m_lastMs - 60000) {
            ++m_dayShift;
            ms += 86400000LL;
        }
        m_lastMs = ms;
        return ms;
    }

    // 兜底：逐个已知格式尝试
    for (const QString &fmt : knownFormats()) {
        QDateTime dt = QDateTime::fromString(s, fmt);
        if (dt.isValid()) {
            const QTime t = dt.time();
            const int tod = t.hour() * 3600000 + t.minute() * 60000 + t.second() * 1000
                + t.msec();
            if (fmt.contains(QLatin1Char('y')))
                return toWallMs(dt.date(), tod);
            qint64 ms = toWallMs(m_baseDate.addDays(m_dayShift), tod);
            if (m_lastMs >= 0 && ms < m_lastMs - 60000) {
                ++m_dayShift;
                ms += 86400000LL;
            }
            m_lastMs = ms;
            return ms;
        }
    }
    return -1;
}
