#include "logpattern.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

QJsonObject PatternProfile::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("pattern"), pattern);
    if (!timeFormat.isEmpty())
        o.insert(QStringLiteral("timeFormat"), timeFormat);
    o.insert(QStringLiteral("multiline"), multiline);
    return o;
}

PatternProfile PatternProfile::fromJson(const QJsonObject &obj)
{
    PatternProfile p;
    p.name = obj.value(QStringLiteral("name")).toString();
    p.pattern = obj.value(QStringLiteral("pattern")).toString();
    p.timeFormat = obj.value(QStringLiteral("timeFormat")).toString();
    p.multiline = obj.value(QStringLiteral("multiline")).toBool(true);
    if (p.name.isEmpty())
        p.name = QStringLiteral("未命名配置");
    return p;
}

QVector<PatternProfile> PatternProfile::builtins()
{
    QVector<PatternProfile> list;

    {
        PatternProfile p;
        p.name = QStringLiteral("示例1   [时间][等级]: 消息");
        p.pattern = QStringLiteral("^\\[(?<time>[^\\]]+)\\]\\s*\\[(?<level>[^\\]]+)\\]\\s*:?\\s*(?<msg>.*)$");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("示例1b  [时间][等级] + 源码位置");
        p.pattern = QStringLiteral(
            "^\\[(?<time>[^\\]]+)\\]\\s*\\[(?<level>[^\\]]+)\\]\\s*:?\\s*(?<msg>.*?)"
            "@(?<loc>\\S+?)(?:$|,(?<tid>\\d+)$)");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("示例2   时间 [等级] 消息");
        p.pattern = QStringLiteral(
            "^(?<time>\\d{2}:\\d{2}:\\d{2}[.,]\\d{3})\\s+\\[(?<level>[^\\]]+)\\]\\s*(?<msg>.*)$");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("通用     ISO 时间 + 等级");
        p.pattern = QStringLiteral(
            "(?i)^(?<time>\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d{1,6})?)"
            "\\s*[\\[(]?(?<level>TRACE|DEBUG|INFO|NOTICE|WARN|WARNING|ERROR|FATAL|CRITICAL)"
            "[\\])]?\\s*[:\\-|]?\\s*(?<msg>.*)$");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("通用     日期 + 时间 + 线程 + 消息");
        p.pattern = QStringLiteral(
            "^(?<time>\\d{4}[-/]\\d{2}[-/]\\d{2}\\s+\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d{1,6})?)"
            "\\s+\\[?(?<tid>\\d+)\\]?\\s+\\[?(?<level>\\w+)\\]?\\s*[:\\-]?\\s*(?<msg>.*)$");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("syslog   Linux 系统日志");
        p.pattern = QStringLiteral(
            "^(?<time>[A-Z][a-z]{2}\\s+\\d{1,2}\\s+\\d{2}:\\d{2}:\\d{2})\\s+\\S+\\s+(?<msg>.*)$");
        list.append(p);
    }
    {
        PatternProfile p;
        p.name = QStringLiteral("兜底     无结构纯文本");
        p.pattern = QStringLiteral("^(?<msg>.*)$");
        p.multiline = false;
        list.append(p);
    }

    return list;
}

QString PatternStore::filePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    return dir + QStringLiteral("/patterns.json");
}

QVector<PatternProfile> PatternStore::load() const
{
    QFile f(filePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return PatternProfile::builtins();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return PatternProfile::builtins();

    const QJsonArray arr = doc.object().value(QStringLiteral("patterns")).toArray();
    QVector<PatternProfile> list;
    for (const QJsonValue &v : arr) {
        if (v.isObject()) {
            PatternProfile p = PatternProfile::fromJson(v.toObject());
            if (!p.pattern.isEmpty())
                list.append(p);
        }
    }
    if (list.isEmpty())
        return PatternProfile::builtins();
    return list;
}

bool PatternStore::save(const QVector<PatternProfile> &list) const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (const PatternProfile &p : list)
        arr.append(p.toJson());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("patterns"), arr);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
