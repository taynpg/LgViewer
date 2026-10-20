#include "common.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace lv {

namespace {

struct LevelWord {
    const char *word;
    Level       level;
};

// 顺序有意义：越靠前优先级越高（先按严重程度判，避免 "warning error" 这类复合词误判）
const LevelWord kLevelWords[] = {
    {"fatal", Level::Fatal},   {"critical", Level::Fatal}, {"crit", Level::Fatal},
    {"panic", Level::Fatal},   {"emerg", Level::Fatal},    {"alert", Level::Fatal},
    {"severe", Level::Error},  {"error", Level::Error},    {"failed", Level::Error},
    {"failure", Level::Error}, {"warning", Level::Warn},   {"warn", Level::Warn},
    {"notice", Level::Info},   {"information", Level::Info}, {"info", Level::Info},
    {"success", Level::Info},  {"debug", Level::Debug},    {"verbose", Level::Trace},
    {"trace", Level::Trace},
};

// 整词精确匹配表（单个字母的缩写必须整词相等才算，否则 "I" 会误伤一大片）
const LevelWord kLevelExact[] = {
    {"f", Level::Fatal},      {"ftl", Level::Fatal},     {"critical", Level::Fatal},
    {"crit", Level::Fatal},   {"panic", Level::Fatal},   {"emerg", Level::Fatal},
    {"emergency", Level::Fatal}, {"alert", Level::Fatal}, {"致命", Level::Fatal},
    {"崩溃", Level::Fatal},   {"e", Level::Error},       {"err", Level::Error},
    {"error", Level::Error},  {"severe", Level::Error},  {"failed", Level::Error},
    {"failure", Level::Error}, {"错误", Level::Error},    {"失败", Level::Error},
    {"w", Level::Warn},       {"wrn", Level::Warn},      {"warn", Level::Warn},
    {"warning", Level::Warn}, {"警告", Level::Warn},      {"警示", Level::Warn},
    {"i", Level::Info},       {"inf", Level::Info},      {"info", Level::Info},
    {"information", Level::Info}, {"notice", Level::Info}, {"note", Level::Info},
    {"ok", Level::Info},      {"success", Level::Info},
    {"信息", Level::Info},     {"通知", Level::Info},      {"提示", Level::Info},
    {"成功", Level::Info},
    {"d", Level::Debug},      {"dbg", Level::Debug},     {"debug", Level::Debug},
    {"调试", Level::Debug},
    {"t", Level::Trace},      {"v", Level::Trace},       {"trc", Level::Trace},
    {"trace", Level::Trace},  {"verbose", Level::Trace}, {"详细", Level::Trace},
    {"追踪", Level::Trace},    {"跟踪", Level::Trace},
};

bool isAsciiAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

} // namespace

Level levelFromText(const QString &text, bool *recognized)
{
    QString t = text.trimmed().toLower();
    while (!t.isEmpty()) {
        const QChar c = t.at(0);
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            break;
        t.remove(0, 1);
    }
    while (!t.isEmpty()) {
        const QChar c = t.at(t.size() - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            break;
        t.chop(1);
    }

    if (recognized)
        *recognized = true;

    if (t.isEmpty()) {
        if (recognized)
            *recognized = false;
        return Level::Other;
    }

    // 1) 整词精确匹配：Info / info / INFO / WARNING / 警告 / I
    for (const LevelWord &w : kLevelExact) {
        if (t == QLatin1String(w.word))
            return w.level;
    }

    // 2) 包含匹配：Level: 或 [Warning] 之类带修饰的写法
    for (const LevelWord &w : kLevelWords) {
        if (t.contains(QLatin1String(w.word)))
            return w.level;
    }

    if (recognized)
        *recognized = false;
    return Level::Other;
}

Level levelFromPrefix(const char *line, int len, bool *recognized)
{
    if (recognized)
        *recognized = false;
    if (!line || len <= 0)
        return Level::Other;

    // 只在行首这一段里找，避免把正文里出现的 "error" 当成等级
    const int limit = qMin(len, 48);
    for (const LevelWord &w : kLevelWords) {
        const int wl = int(qstrlen(w.word));
        if (wl <= 0 || wl > limit)
            continue;
        for (int i = 0; i + wl <= limit; ++i) {
            char buf[24];
            for (int k = 0; k < wl; ++k) {
                char c = line[i + k];
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
                buf[k] = c;
            }
            if (qstrncmp(buf, w.word, size_t(wl)) != 0)
                continue;
            // 要求两侧是非字母，否则 "failure" 里也会命中 "fail"
            if (i > 0 && isAsciiAlpha(line[i - 1]))
                continue;
            if (i + wl < len && isAsciiAlpha(line[i + wl]))
                continue;
            if (recognized)
                *recognized = true;
            return w.level;
        }
    }
    return Level::Other;
}

QString levelAliasHint(Level level)
{
    switch (level) {
    case Level::Fatal:
        return QStringLiteral("fatal / fatal error / critical / crit / panic / alert / 致命");
    case Level::Error:
        return QStringLiteral("error / err / ERROR / failed / severe / 错误 / 失败");
    case Level::Warn:
        return QStringLiteral("warn / warnING / WARNING / warning / 警告");
    case Level::Info:
        return QStringLiteral("info / Info / INFO / information / notice / ok / success / 信息");
    case Level::Debug:
        return QStringLiteral("debug / Debug / DEBUG / dbg / 调试");
    case Level::Trace:
        return QStringLiteral("trace / verbose / v / 详细");
    default:
        return QStringLiteral("未识别：正则没抓到 level 组，行首也没有 [INFO] 这类标记");
    }
}


QString levelName(Level level)
{
    switch (level) {
    case Level::Trace: return QStringLiteral("trace");
    case Level::Debug: return QStringLiteral("debug");
    case Level::Info:  return QStringLiteral("info");
    case Level::Warn:  return QStringLiteral("warn");
    case Level::Error: return QStringLiteral("error");
    case Level::Fatal: return QStringLiteral("fatal");
    default:           return QStringLiteral("other");
    }
}

QString levelDisplayName(Level level)
{
    switch (level) {
    case Level::Trace: return QStringLiteral("TRACE");
    case Level::Debug: return QStringLiteral("DEBUG");
    case Level::Info:  return QStringLiteral("INFO");
    case Level::Warn:  return QStringLiteral("WARN");
    case Level::Error: return QStringLiteral("ERROR");
    case Level::Fatal: return QStringLiteral("FATAL");
    default:           return QStringLiteral("OTHER");
    }
}

QColor levelColor(Level level, bool dark)
{
    if (dark) {
        switch (level) {
        case Level::Trace: return QColor(0x7d, 0x85, 0x90);
        case Level::Debug: return QColor(0x79, 0xc0, 0xff);
        case Level::Info:  return QColor(0x56, 0xd3, 0x64);
        case Level::Warn:  return QColor(0xe3, 0xb3, 0x41);
        case Level::Error: return QColor(0xff, 0x7b, 0x72);
        case Level::Fatal: return QColor(0xff, 0x9d, 0x9d);
        default:           return QColor(0x8b, 0x94, 0x9e);
        }
    }
    switch (level) {
    case Level::Trace: return QColor(0x6e, 0x77, 0x81);
    case Level::Debug: return QColor(0x09, 0x69, 0xda);
    case Level::Info:  return QColor(0x14, 0x7a, 0x3c);
    case Level::Warn:  return QColor(0x9a, 0x67, 0x00);
    case Level::Error: return QColor(0xcf, 0x22, 0x2e);
    case Level::Fatal: return QColor(0xb3, 0x1c, 0x1c);
    default:           return QColor(0x57, 0x60, 0x6a);
    }
}

QColor levelTint(Level level, bool dark)
{
    if (dark) {
        switch (level) {
        case Level::Warn:  return QColor(0x3a, 0x2d, 0x0c);
        case Level::Error: return QColor(0x3a, 0x1d, 0x1d);
        case Level::Fatal: return QColor(0x5c, 0x1c, 0x1c);
        default:           return QColor(Qt::transparent);
        }
    }
    switch (level) {
    case Level::Warn:  return QColor(0xff, 0xf8, 0xe5);
    case Level::Error: return QColor(0xff, 0xee, 0xec);
    case Level::Fatal: return QColor(0xff, 0xda, 0xd6);
    default:           return QColor(Qt::transparent);
    }
}

QString monospaceFamily()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    const QStringList wanted = {
        QStringLiteral("Cascadia Mono"),   QStringLiteral("Cascadia Code"),
        QStringLiteral("JetBrains Mono"),  QStringLiteral("Consolas"),
        QStringLiteral("Source Code Pro"), QStringLiteral("DejaVu Sans Mono"),
        QStringLiteral("Menlo"),           QStringLiteral("Monaco"),
        QStringLiteral("Ubuntu Mono"),     QStringLiteral("Liberation Mono"),
    };
    const QStringList families = QFontDatabase::families();
    for (const QString &w : wanted) {
        if (families.contains(w, Qt::CaseInsensitive)) {
            cached = w;
            return cached;
        }
    }
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    cached = f.family();
    return cached;
}

static Theme buildTheme(bool dark)
{
    Theme t;
    t.dark = dark;

    QFont mono(monospaceFamily());
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(dark ? 10 : 10);
    mono.setFixedPitch(true);
    t.mono = mono;

    if (dark) {
        t.text       = QColor(0xd8, 0xde, 0xe7);
        t.textDim    = QColor(0x7d, 0x85, 0x90);
        t.bg         = QColor(0x16, 0x18, 0x1d);
        t.bgAlt      = QColor(0x1b, 0x1e, 0x24);
        t.gutterBg   = QColor(0x1b, 0x1e, 0x24);
        t.gutterText = QColor(0x60, 0x68, 0x73);
        t.gutterActive = QColor(0xb0, 0xb8, 0xc4);
        t.border     = QColor(0x2d, 0x33, 0x3c);
        t.selBg      = QColor(0x25, 0x3a, 0x55);
        t.selText    = QColor(0xe8, 0xee, 0xf6);
        t.matchBg    = QColor(0x5c, 0x4a, 0x00);
        t.matchText  = QColor(0xff, 0xf1, 0xa8);
        t.searchBg   = QColor(0x1b, 0x3c, 0x63);
        t.searchText = QColor(0xd6, 0xe8, 0xff);
        t.activeMatchBg   = QColor(0xff, 0x9b, 0x2e);
        t.activeMatchText = QColor(0x2b, 0x15, 0x00);
        t.tagTime    = QColor(0x6c, 0x8c, 0xa8);
    } else {
        t.text       = QColor(0x1f, 0x23, 0x28);
        t.textDim    = QColor(0x8c, 0x95, 0x9f);
        t.bg         = QColor(0xff, 0xff, 0xff);
        t.bgAlt      = QColor(0xfa, 0xfa, 0xfa);
        t.gutterBg   = QColor(0xf6, 0xf8, 0xfa);
        t.gutterText = QColor(0xa8, 0xb0, 0xb9);
        t.gutterActive = QColor(0x57, 0x60, 0x6a);
        t.border     = QColor(0xe3, 0xe7, 0xec);
        t.selBg      = QColor(0xdc, 0xe9, 0xfc);
        t.selText    = QColor(0x0f, 0x1b, 0x2b);
        t.matchBg    = QColor(0xff, 0xe8, 0x8a);
        t.matchText  = QColor(0x4a, 0x35, 0x00);
        t.searchBg   = QColor(0xcf, 0xe6, 0xff);
        t.searchText = QColor(0x0a, 0x2c, 0x55);
        t.activeMatchBg   = QColor(0xff, 0x9b, 0x2e);
        t.activeMatchText = QColor(0x2b, 0x15, 0x00);
        t.tagTime    = QColor(0x6f, 0x8b, 0xa6);
    }
    t.bgAlt.setAlpha(255);
    return t;
}

const Theme &theme()
{
    static Theme t;
    static bool built = false;
    if (!built) {
        bool dark = false;
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            dark = app->palette().color(QPalette::Window).lightness() < 128;
        t = buildTheme(dark);
        built = true;
    }
    return t;
}

int utf8ByteToCharIndex(const char *s, int byteOffset)
{
    int chars = 0;
    int i = 0;
    while (i < byteOffset) {
        const uchar c = uchar(s[i]);
        if (c < 0x80) {
            i += 1;
            chars += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
            chars += 1;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
            chars += 1;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
            chars += 2;
        } else {
            i += 1;
            chars += 1;
        }
    }
    return chars;
}

void refreshTheme()
{
    bool dark = false;
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        dark = app->palette().color(QPalette::Window).lightness() < 128;
    // 重建静态对象
    const_cast<Theme &>(theme()) = buildTheme(dark);
}

// ---------------------------------------------------------------------------
// 控件风格
// ---------------------------------------------------------------------------
namespace {

QString findStyleKey(const QStringList &wanted)
{
    const QStringList keys = QStyleFactory::keys();
    for (const QString &want : wanted) {
        for (const QString &k : keys) {
            if (k.compare(want, Qt::CaseInsensitive) == 0)
                return k;
        }
    }
    return QString();
}

} // namespace

QString windowsStyleKey()
{
    // "Windows" 是经典 Win32 原生控件；windows11 是 Qt 6.8+ 的 Fluent 实现
    return findStyleKey({QStringLiteral("Windows"), QStringLiteral("windows11"),
                         QStringLiteral("windowsvista")});
}

QString fusionStyleKey()
{
    const QString k = findStyleKey({QStringLiteral("Fusion")});
    return k.isEmpty() ? QStringLiteral("Fusion") : k;
}

bool applyUiStyle(const QString &key)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return false;

    QStyle *style = QStyleFactory::create(key);
    bool ok = style != nullptr;
    if (!style)
        style = QStyleFactory::create(fusionStyleKey());
    if (!style)
        return false;

    app->setStyle(style); // 旧 style 由 QApplication 接管释放
    refreshTheme();
    return ok;
}

QString currentUiStyle()
{
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        if (QStyle *s = app->style())
            return s->objectName();
    }
    return QString();
}

} // namespace lv
