#pragma once

#include "common.h"
#include "logpattern.h"
#include "textencoding.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using lv::TextEncoding;

// 单行解析结果（尽量紧凑，百万行也不能吃掉太多内存）
struct LineMeta {
    qint64 timeMs = 0;
    quint8 level = quint8(lv::Level::Other);
    quint8 flags = 0; // bit0 = 续行

    bool isContinuation() const { return (flags & 0x01) != 0; }
};

struct FilterSpec {
    bool   timeEnabled = false;
    qint64 fromMs = 0;
    qint64 toMs = 0;
    quint8 levelMask = lv::kAllLevelsMask;
    QString text;
    bool   textIsRegex = false;
    bool   textCaseSensitive = false;
    bool   highlightOnly = false;

    bool isIdentity() const;
};

// 一处搜索命中。row 是「当前可见行」下标（有筛选时只搜可见行），
// startByte/endByte 是该行内的 UTF-8 字节偏移，绘制时再换算成字符下标。
struct SearchHit {
    quint32 row = 0;
    quint32 startByte = 0;
    quint32 endByte = 0;
};

struct LogData;

class LogDocument : public QObject
{
    Q_OBJECT
public:
    explicit LogDocument(QObject *parent = nullptr);
    ~LogDocument() override;

    void openFile(const QString &path, TextEncoding encoding = TextEncoding::Auto);
    void reload();
    void closeFile();

    void setProfile(const PatternProfile &profile); // 换配置后自动重解析
    const PatternProfile &profile() const { return m_profile; }

    void applyFilter(const FilterSpec &spec);
    const FilterSpec &filter() const { return m_filter; }

    // 全文查找（字面量）。空串等于清空结果；结果按 row 升序排列。
    void setSearchQuery(const QString &needle, bool caseSensitive, bool wholeWord);
    void clearSearch() { setSearchQuery(QString(), false, false); }
    std::shared_ptr<const QVector<SearchHit>> searchHits() const { return m_hits; }
    QString searchQuery() const { return m_searchText; }
    bool    searchPending() const { return m_searching; }
    bool    searchTruncated() const { return m_searchTruncated; }
    double  searchSeconds() const { return m_searchSeconds; }

    bool isLoading() const { return m_loading; }
    bool isLoaded() const { return m_data != nullptr; }
    bool isFiltering() const { return m_filtering; }

    QString filePath() const { return m_path; }
    qint64 fileSize() const;
    TextEncoding encoding() const;

    int lineCount() const;
    int rowCount() const;
    quint32 rowLine(int row) const;
    int rowOfLine(int line) const; // 被过滤掉返回 -1
    int rowAtOrAfterLine(int line) const; // 该行被过滤掉时取其后第一条可见行，找不到返回 -1

    const char *lineData(int line, int *length) const;
    QString lineText(int line) const;
    LineMeta meta(int line) const;

    qint64 minTimeMs() const;
    qint64 maxTimeMs() const;
    bool hasTimestamps() const;
    quint8 levelsPresent() const;
    int levelCount(lv::Level level) const;

    double parseSeconds() const;
    double filterSeconds() const;

signals:
    void loadStarted(const QString &path);
    void loadProgress(int percent, const QString &stage);
    void loadFinished(bool ok, const QString &message);
    void filterChanged();
    // 搜索结果就绪（含被清空的情况）
    void searchChanged();

private:
    void runLoad(quint64 generation, QString path, TextEncoding encoding, PatternProfile profile);
    void runFilter(quint64 generation, std::shared_ptr<LogData> data, FilterSpec spec);
    void runSearch(quint64 generation, std::shared_ptr<LogData> data,
                   std::shared_ptr<const std::vector<quint32>> visible,
                   QByteArray needle, bool caseSensitive, bool wholeWord);
    void cancelSearch();
    // 可见行集合变了（首次加载、换配置、改筛选）之后按当前条件重跑一次
    void refreshSearch();
    void cancelWorkers();
    void publishLoadFailure(quint64 generation, const QString &message);
    bool readAndSplit(const std::shared_ptr<LogData> &data, const QString &path,
                      TextEncoding encoding, quint64 generation, QString *error);
    bool parseLines(const std::shared_ptr<LogData> &data, const PatternProfile &profile,
                    quint64 generation, QString *error);

    std::shared_ptr<LogData> m_data;
    std::shared_ptr<const std::vector<quint32>> m_visible;

    QString           m_path;
    PatternProfile    m_profile;
    FilterSpec        m_filter;
    TextEncoding      m_encoding = TextEncoding::Auto;

    bool m_loading = false;
    bool m_filtering = false;

    double m_filterSeconds = 0.0;

    // 搜索状态
    std::shared_ptr<const QVector<SearchHit>> m_hits;
    QString m_searchText;
    bool    m_searchCase = false;
    bool    m_searchWord = false;
    bool    m_searching = false;
    bool    m_searchTruncated = false;
    bool    m_searchPendingRefresh = false;
    double  m_searchSeconds = 0.0;

    std::atomic<quint64> m_generation{0};
    std::atomic<quint64> m_filterGeneration{0};
    std::atomic<quint64> m_searchGeneration{0};
    std::atomic<bool>    m_cancelLoad{false};
    std::atomic<bool>    m_cancelFilter{false};
    std::atomic<bool>    m_cancelSearch{false};

    std::thread m_loadThread;
    std::thread m_filterThread;
    std::thread m_searchThread;
};
