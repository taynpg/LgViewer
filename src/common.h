#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace lv {

// ---------------------------------------------------------------------------
// 日志等级
// ---------------------------------------------------------------------------
enum class Level : quint8 {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Other
};

constexpr int kLevelCount = 7;

inline constexpr quint8 levelBit(Level l)
{
    return quint8(1u << static_cast<unsigned>(l));
}

inline constexpr quint8 kAllLevelsMask = quint8((1u << kLevelCount) - 1u);

Level   levelFromText(const QString &text, bool *recognized = nullptr);
// 当正则里没有 level 命名组时的兜底：在行首若干字节内找 [Info] / INFO: 这类明确标记
Level   levelFromPrefix(const char *line, int len, bool *recognized = nullptr);
QString levelName(Level level);        // 英文短名，写入配置用
QString levelDisplayName(Level level); // 界面显示
QString levelAliasHint(Level level);   // 识别到的写法，用于界面 tooltip

QColor levelColor(Level level, bool dark);
QColor levelTint(Level level, bool dark); // 整行背景淡色

// ---------------------------------------------------------------------------
// 主题（跟随系统深浅色）
// ---------------------------------------------------------------------------
struct Theme {
    bool   dark = false;
    QColor text;
    QColor textDim;
    QColor bg;
    QColor bgAlt;
    QColor gutterBg;
    QColor gutterText;
    QColor gutterActive;
    QColor border;
    QColor selBg;
    QColor selText;
    QColor matchBg;
    QColor matchText;
    QColor tagTime;
    QFont  mono;
};

const Theme &theme();
void         refreshTheme();
QString      monospaceFamily();

// UTF-8 字节偏移 -> QString 里的字符下标（代理对按 2 个单元计）
int utf8ByteToCharIndex(const char *s, int byteOffset);

} // namespace lv
