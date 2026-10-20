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
    QColor matchBg;      // 筛选关键字高亮（琥珀色）
    QColor matchText;
    QColor searchBg;     // 搜索命中（蓝色，与筛选高亮区分）
    QColor searchText;
    QColor activeMatchBg; // 当前聚焦的那一处命中（橙色）
    QColor activeMatchText;
    QColor tagTime;
    QFont  mono;
};

const Theme &theme();
void         refreshTheme();
QString      monospaceFamily();

// ---------------------------------------------------------------------------
// 控件风格（QStyle）
// ---------------------------------------------------------------------------
// 平台上的原生 Windows 风格键名（"Windows" / "windows11" / "windowsvista"），
// 非 Windows 平台返回空串。偏好顺序即返回值顺序。
QString windowsStyleKey();
QString fusionStyleKey();
// 切换 QApplication 的控件风格并重建主题；失败时退回 Fusion 并返回 false
bool    applyUiStyle(const QString &key);
// 当前生效的风格键名（QStyle::objectName）
QString currentUiStyle();

// UTF-8 字节偏移 -> QString 里的字符下标（代理对按 2 个单元计）
int utf8ByteToCharIndex(const char *s, int byteOffset);

} // namespace lv
