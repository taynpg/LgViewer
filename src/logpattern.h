#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

// 一条「日志结构配置」= 一个正则 + 可选的时间格式
struct PatternProfile {
    QString name;
    QString pattern;
    QString timeFormat;         // 空 = 自动识别
    bool    multiline = true;   // 未匹配的行归并到上一条

    QJsonObject toJson() const;
    static PatternProfile fromJson(const QJsonObject &obj);

    // 内置示例配置
    static QVector<PatternProfile> builtins();
};

class PatternStore
{
public:
    static QString filePath();

    QVector<PatternProfile> load() const;              // 不存在时返回内置配置
    bool save(const QVector<PatternProfile> &list) const;
};
