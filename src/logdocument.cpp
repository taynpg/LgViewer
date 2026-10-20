#include "logdocument.h"

#include "regexengine.h"
#include "timeparse.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <cstring>
#include <initializer_list>

struct LogData {
    QFile     file;
    uchar    *map = nullptr;
    qint64    mapSize = 0;
    QByteArray rawFallback;
    QByteArray converted; // 需要转码时持有 UTF-8 副本
    const char *utf8 = nullptr;
    qint64    utf8Size = 0;

    std::vector<quint64>  lineStart;
    std::vector<LineMeta> meta;

    qint64 minMs = 0;
    qint64 maxMs = 0;
    bool   hasTime = false;
    quint8 levels = 0;
    int    counts[lv::kLevelCount] = {0};
    TextEncoding enc = TextEncoding::Utf8;
    double parseSec = 0.0;
    qint64 fileSize = 0;

    ~LogData()
    {
        if (map) {
            if (file.isOpen())
                file.unmap(map);
            map = nullptr;
        }
    }
};

bool FilterSpec::isIdentity() const
{
    return !timeEnabled && levelMask == lv::kAllLevelsMask && text.isEmpty();
}

namespace {

inline char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool containsBytes(const char *hay, int hayLen, const QByteArray &needle, bool caseSensitive)
{
    const int n = int(needle.size());
    if (n == 0)
        return true;
    if (n > hayLen)
        return false;
    const char *nd = needle.constData();
    if (caseSensitive) {
        for (int i = 0; i + n <= hayLen; ++i) {
            if (std::memcmp(hay + i, nd, size_t(n)) == 0)
                return true;
        }
        return false;
    }
    for (int i = 0; i + n <= hayLen; ++i) {
        int k = 0;
        while (k < n && lowerAscii(hay[i + k]) == lowerAscii(nd[k]))
            ++k;
        if (k == n)
            return true;
    }
    return false;
}

int firstGroup(const RegexEngine &eng, std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        const int idx = eng.groupIndex(QString::fromLatin1(n));
        if (idx >= 0)
            return idx;
    }
    return -1;
}

} // namespace

LogDocument::LogDocument(QObject *parent)
    : QObject(parent)
{
    const QVector<PatternProfile> builtins = PatternProfile::builtins();
    if (!builtins.isEmpty())
        m_profile = builtins.first();
}

LogDocument::~LogDocument()
{
    cancelWorkers();
    m_data.reset();
}

void LogDocument::cancelWorkers()
{
    m_cancelLoad = true;
    m_cancelFilter = true;
    if (m_loadThread.joinable())
        m_loadThread.join();
    if (m_filterThread.joinable())
        m_filterThread.join();
    m_cancelLoad = false;
    m_cancelFilter = false;
    m_loading = false;
    m_filtering = false;
}

void LogDocument::openFile(const QString &path, TextEncoding encoding)
{
    cancelWorkers();
    const quint64 gen = ++m_generation;
    m_path = path;
    m_encoding = encoding;
    m_loading = true;
    m_filtering = false;

    emit loadStarted(path);

    const PatternProfile profile = m_profile;
    m_loadThread = std::thread([this, gen, path, encoding, profile]() {
        runLoad(gen, path, encoding, profile);
    });
}

void LogDocument::reload()
{
    if (!m_path.isEmpty())
        openFile(m_path, m_encoding);
}

void LogDocument::closeFile()
{
    cancelWorkers();
    ++m_generation;
    ++m_filterGeneration;
    m_data.reset();
    m_visible.reset();
    m_path.clear();
    emit filterChanged();
}

void LogDocument::setProfile(const PatternProfile &profile)
{
    m_profile = profile;
    if (!m_path.isEmpty())
        openFile(m_path, m_encoding);
}

void LogDocument::applyFilter(const FilterSpec &spec)
{
    m_filter = spec;

    if (!m_data) {
        m_visible.reset();
        emit filterChanged();
        return;
    }

    // 取消上一次筛选
    m_cancelFilter = true;
    if (m_filterThread.joinable())
        m_filterThread.join();
    m_cancelFilter = false;

    const quint64 gen = ++m_filterGeneration;

    if (spec.isIdentity()) {
        m_visible.reset();
        m_filtering = false;
        m_filterSeconds = 0.0;
        emit filterChanged();
        return;
    }

    m_filtering = true;
    auto data = m_data;
    m_filterThread = std::thread([this, gen, data, spec]() { runFilter(gen, data, spec); });
}

void LogDocument::publishLoadFailure(quint64 generation, const QString &message)
{
    QMetaObject::invokeMethod(
        this,
        [this, generation, message]() {
            if (generation != m_generation.load())
                return;
            m_loading = false;
            emit loadFinished(false, message);
        },
        Qt::QueuedConnection);
}

bool LogDocument::readAndSplit(const std::shared_ptr<LogData> &data, const QString &path,
                               TextEncoding encoding, quint64 generation, QString *error)
{
    data->file.setFileName(path);
    if (!data->file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法打开文件：%1").arg(data->file.errorString());
        return false;
    }
    data->fileSize = data->file.size();
    if (data->fileSize <= 0) {
        data->utf8 = "";
        data->utf8Size = 0;
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    auto postProgress = [this, generation, &timer](int pct, const QString &stage, bool force = false) {
        static thread_local qint64 last = 0;
        if (!force && timer.elapsed() - last < 60)
            return;
        last = timer.elapsed();
        QMetaObject::invokeMethod(
            this,
            [this, generation, pct, stage]() {
                if (generation != m_generation.load())
                    return;
                emit loadProgress(pct, stage);
            },
            Qt::QueuedConnection);
    };

    postProgress(0, QStringLiteral("读取文件…"), true);

    uchar *mapped = data->file.map(0, data->fileSize);
    const char *raw = nullptr;
    qint64 rawSize = 0;
    if (mapped) {
        data->map = mapped;
        data->mapSize = data->fileSize;
        raw = reinterpret_cast<const char *>(mapped);
        rawSize = data->fileSize;
    } else {
        data->rawFallback = data->file.readAll();
        raw = data->rawFallback.constData();
        rawSize = data->rawFallback.size();
    }

    if (m_cancelLoad.load() || generation != m_generation.load())
        return false;

    TextEncoding enc = encoding;
    if (enc == TextEncoding::Auto)
        enc = lv::detectEncoding(raw, rawSize);
    data->enc = enc;

    qint64 bom = 0;
    if (enc == TextEncoding::Utf8Bom)
        bom = 3;
    else if (enc == TextEncoding::Utf16LE || enc == TextEncoding::Utf16BE)
        bom = 2;
    bom = qMin(bom, rawSize);

    if (lv::needsConversion(enc)) {
        postProgress(10, QStringLiteral("转码为 UTF-8…"), true);
        QString convError;
        if (!lv::convertToUtf8(data->converted, raw + bom, rawSize - bom, enc, &convError)) {
            *error = convError;
            return false;
        }
        data->utf8 = data->converted.constData();
        data->utf8Size = data->converted.size();
    } else {
        data->utf8 = raw + bom;
        data->utf8Size = rawSize - bom;
    }

    // ---- 切分行 -------------------------------------------------------------
    postProgress(15, QStringLiteral("建立行索引…"), true);
    const char *base = data->utf8;
    const qint64 total = data->utf8Size;
    data->lineStart.reserve(size_t(qMin<qint64>(total / 48 + 64, 64LL * 1024 * 1024)));

    qint64 pos = 0;
    qint64 lastReport = 0;
    while (pos < total) {
        data->lineStart.push_back(quint64(pos));
        const char *nl = static_cast<const char *>(std::memchr(base + pos, '\n', size_t(total - pos)));
        if (!nl)
            break;
        pos = (nl - base) + 1;
        if ((data->lineStart.size() & 0x3FFFF) == 0) {
            if (m_cancelLoad.load() || generation != m_generation.load())
                return false;
            const qint64 now = timer.elapsed();
            if (now - lastReport > 60) {
                lastReport = now;
                postProgress(int(15 + 25.0 * double(pos) / double(total)),
                             QStringLiteral("建立行索引…"));
            }
        }
    }

    return true;
}

bool LogDocument::parseLines(const std::shared_ptr<LogData> &data, const PatternProfile &profile,
                             quint64 generation, QString *error)
{
    RegexEngine engine;
    QString compileError;
    if (!engine.compile(profile.pattern, &compileError)) {
        *error = QStringLiteral("正则表达式编译失败：%1").arg(compileError);
        return false;
    }

    const int giTime = firstGroup(engine, {"time", "timestamp", "ts", "datetime", "date"});
    const int giLevel = firstGroup(engine, {"level", "lvl", "sev", "severity", "loglevel"});

    TimestampParser timeParser;
    timeParser.setExplicitFormat(profile.timeFormat);
    const QFileInfo fi(m_path);
    timeParser.setBaseDate(fi.lastModified().date());
    timeParser.reset();

    const int lines = int(data->lineStart.size());
    data->meta.resize(size_t(lines));

    QElapsedTimer timer;
    timer.start();
    qint64 lastReport = 0;

    lv::Level prevLevel = lv::Level::Other;
    qint64 prevTime = 0;
    bool haveEntry = false;

    for (int i = 0; i < lines; ++i) {
        if ((i & 0x3FFF) == 0) {
            if (m_cancelLoad.load() || generation != m_generation.load())
                return false;
            const qint64 now = timer.elapsed();
            if (now - lastReport > 60) {
                lastReport = now;
                const int pct = 40 + int(60.0 * double(i) / double(qMax(1, lines)));
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, pct]() {
                        if (generation != m_generation.load())
                            return;
                        emit loadProgress(pct, QStringLiteral("解析日志结构…"));
                    },
                    Qt::QueuedConnection);
            }
        }

        quint64 s = data->lineStart[size_t(i)];
        quint64 e = (i + 1 < lines) ? data->lineStart[size_t(i + 1)] : quint64(data->utf8Size);
        while (e > s && (data->utf8[e - 1] == '\n' || data->utf8[e - 1] == '\r'))
            --e;
        const char *p = data->utf8 + s;
        const int len = int(e - s);

        LineMeta &m = data->meta[size_t(i)];
        m.timeMs = 0;
        m.level = quint8(lv::Level::Other);
        m.flags = 0;

        const int rc = (len > 0) ? engine.match(p, len) : 0;
        if (rc == 1) {
            lv::Level level = lv::Level::Other;
            if (giLevel >= 0) {
                const int a = engine.start(giLevel);
                const int b = engine.end(giLevel);
                if (a >= 0 && b >= a)
                    level = lv::levelFromText(QString::fromUtf8(p + a, b - a));
            } else {
                // 正则里没有 level 命名组时，退化为行首关键字识别（[Info]、ERROR: 这类）
                level = lv::levelFromPrefix(p, len);
            }
            qint64 t = -1;
            if (giTime >= 0) {
                const int a = engine.start(giTime);
                const int b = engine.end(giTime);
                if (a >= 0 && b > a)
                    t = timeParser.parse(QString::fromUtf8(p + a, b - a));
            }
            if (t < 0)
                t = prevTime;

            m.level = quint8(level);
            m.timeMs = t;
            prevLevel = level;
            prevTime = t;
            haveEntry = true;
        } else if (profile.multiline && haveEntry) {
            m.flags |= 0x01;
            m.level = quint8(prevLevel);
            m.timeMs = prevTime;
        } else {
            m.level = quint8(lv::Level::Other);
            m.timeMs = prevTime;
        }

        if (m.timeMs > 0) {
            if (!data->hasTime) {
                data->minMs = data->maxMs = m.timeMs;
                data->hasTime = true;
            } else {
                if (m.timeMs < data->minMs)
                    data->minMs = m.timeMs;
                if (m.timeMs > data->maxMs)
                    data->maxMs = m.timeMs;
            }
        }
        data->levels |= lv::levelBit(lv::Level(m.level));
        if (m.level < lv::kLevelCount)
            data->counts[m.level]++;
    }

    return true;
}

void LogDocument::runLoad(quint64 generation, QString path, TextEncoding encoding,
                          PatternProfile profile)
{
    auto data = std::make_shared<LogData>();
    QElapsedTimer timer;
    timer.start();

    QString error;
    if (!readAndSplit(data, path, encoding, generation, &error)) {
        if (m_cancelLoad.load() || generation != m_generation.load())
            return;
        publishLoadFailure(generation, error);
        return;
    }

    if (m_cancelLoad.load() || generation != m_generation.load())
        return;

    if (!parseLines(data, profile, generation, &error)) {
        if (m_cancelLoad.load() || generation != m_generation.load())
            return;
        publishLoadFailure(generation, error);
        return;
    }

    data->parseSec = double(timer.elapsed()) / 1000.0;

    QMetaObject::invokeMethod(
        this,
        [this, generation, data]() {
            if (generation != m_generation.load())
                return;
            m_data = data;
            m_visible.reset();
            m_loading = false;
            m_filtering = false;
            emit loadProgress(100, QStringLiteral("完成"));
            emit filterChanged();
            emit loadFinished(true, QString());
        },
        Qt::QueuedConnection);
}

void LogDocument::runFilter(quint64 generation, std::shared_ptr<LogData> data, FilterSpec spec)
{
    QElapsedTimer timer;
    timer.start();

    auto visible = std::make_shared<std::vector<quint32>>();
    const int lines = int(data->meta.size());

    RegexEngine engine;
    QByteArray needle = spec.text.toUtf8();
    bool useRegex = spec.textIsRegex && !spec.text.isEmpty();
    if (useRegex) {
        QString pattern = spec.text;
        if (!spec.textCaseSensitive)
            pattern.prepend(QStringLiteral("(?i)"));
        QString err;
        if (!engine.compile(pattern, &err))
            useRegex = false; // 正则写错时退回子串匹配
    }

    visible->reserve(size_t(lines));

    for (int i = 0; i < lines; ++i) {
        if ((i & 0x3FFF) == 0) {
            if (m_cancelFilter.load() || generation != m_filterGeneration.load())
                return;
        }
        const LineMeta &m = data->meta[size_t(i)];

        if (spec.timeEnabled && (m.timeMs < spec.fromMs || m.timeMs > spec.toMs))
            continue;
        if (!(spec.levelMask & lv::levelBit(lv::Level(m.level))))
            continue;

        if (!spec.text.isEmpty()) {
            quint64 s = data->lineStart[size_t(i)];
            quint64 e = (i + 1 < lines) ? data->lineStart[size_t(i + 1)]
                                        : quint64(data->utf8Size);
            while (e > s && (data->utf8[e - 1] == '\n' || data->utf8[e - 1] == '\r'))
                --e;
            const char *p = data->utf8 + s;
            const int len = int(e - s);
            const bool hit = useRegex ? (engine.match(p, len) == 1)
                                      : containsBytes(p, len, needle, spec.textCaseSensitive);
            if (!hit)
                continue;
        }

        visible->push_back(quint32(i));
    }

    const double sec = double(timer.elapsed()) / 1000.0;

    QMetaObject::invokeMethod(
        this,
        [this, generation, visible, sec]() {
            if (generation != m_filterGeneration.load())
                return;
            m_visible = visible;
            m_filtering = false;
            m_filterSeconds = sec;
            emit filterChanged();
        },
        Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 访问器
// ---------------------------------------------------------------------------
qint64 LogDocument::fileSize() const
{
    return m_data ? m_data->fileSize : 0;
}

TextEncoding LogDocument::encoding() const
{
    return m_data ? m_data->enc : TextEncoding::Auto;
}

int LogDocument::lineCount() const
{
    return m_data ? int(m_data->lineStart.size()) : 0;
}

int LogDocument::rowCount() const
{
    if (!m_data)
        return 0;
    return m_visible ? int(m_visible->size()) : lineCount();
}

quint32 LogDocument::rowLine(int row) const
{
    if (!m_data)
        return 0;
    if (!m_visible)
        return quint32(row);
    if (row < 0 || row >= int(m_visible->size()))
        return 0;
    return (*m_visible)[size_t(row)];
}

int LogDocument::rowOfLine(int line) const
{
    if (!m_data)
        return -1;
    if (!m_visible)
        return (line >= 0 && line < lineCount()) ? line : -1;
    const auto it = std::lower_bound(m_visible->begin(), m_visible->end(), quint32(line));
    if (it == m_visible->end() || *it != quint32(line))
        return -1;
    return int(it - m_visible->begin());
}

int LogDocument::rowAtOrAfterLine(int line) const
{
    if (!m_data || line < 0)
        return -1;
    if (!m_visible)
        return (line < lineCount()) ? line : -1;
    if (m_visible->empty())
        return -1;
    const auto it = std::lower_bound(m_visible->begin(), m_visible->end(), quint32(line));
    if (it == m_visible->end())
        return -1;
    return int(it - m_visible->begin());
}

const char *LogDocument::lineData(int line, int *length) const
{
    if (length)
        *length = 0;
    if (!m_data || line < 0 || line >= lineCount())
        return nullptr;
    const LogData &d = *m_data;
    quint64 s = d.lineStart[size_t(line)];
    quint64 e = (line + 1 < int(d.lineStart.size())) ? d.lineStart[size_t(line + 1)]
                                                     : quint64(d.utf8Size);
    while (e > s && (d.utf8[e - 1] == '\n' || d.utf8[e - 1] == '\r'))
        --e;
    if (length)
        *length = int(e - s);
    return d.utf8 + s;
}

QString LogDocument::lineText(int line) const
{
    int len = 0;
    const char *p = lineData(line, &len);
    if (!p)
        return QString();
    return QString::fromUtf8(p, len);
}

LineMeta LogDocument::meta(int line) const
{
    if (!m_data || line < 0 || line >= lineCount())
        return LineMeta();
    return m_data->meta[size_t(line)];
}

qint64 LogDocument::minTimeMs() const
{
    return m_data ? m_data->minMs : 0;
}

qint64 LogDocument::maxTimeMs() const
{
    return m_data ? m_data->maxMs : 0;
}

bool LogDocument::hasTimestamps() const
{
    return m_data && m_data->hasTime;
}

quint8 LogDocument::levelsPresent() const
{
    return m_data ? m_data->levels : 0;
}

int LogDocument::levelCount(lv::Level level) const
{
    if (!m_data || int(level) >= lv::kLevelCount)
        return 0;
    return m_data->counts[int(level)];
}

double LogDocument::parseSeconds() const
{
    return m_data ? m_data->parseSec : 0.0;
}

double LogDocument::filterSeconds() const
{
    return m_filterSeconds;
}
