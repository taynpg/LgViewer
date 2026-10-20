#pragma once

#include "logdocument.h"
#include "regexengine.h"

#include <QAbstractScrollArea>
#include <QFont>
#include <QFontMetrics>
#include <QString>

#include <memory>
#include <unordered_map>

class QTextLayout;

// 只渲染可见行的日志视图：文件再大，绘制量也只与窗口高度有关
class LogView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit LogView(QWidget *parent = nullptr);
    ~LogView() override;

    void setDocument(LogDocument *doc);
    LogDocument *document() const { return m_doc; }

    void setFontPointSize(int pt);
    int  fontPointSize() const { return m_fontSize; }

    void setHighlightPattern(const QString &text, bool isRegex, bool caseSensitive);
    void clearHighlight();

    // 搜索命中：普通命中铺蓝色底，当前命中铺橙色底并在行号栏加标记
    void setSearchHits(std::shared_ptr<const QVector<SearchHit>> hits);
    void setActiveHit(int index);
    int  activeHit() const { return m_activeHit; }

    // 着色开关
    void setLevelKeywordColored(bool on);   // 等级关键字（[Info]）本身着色
    void setMessageLevelColored(bool on);   // 消息正文也用等级色（与关键字同色）
    void setRowTintEnabled(bool on);        // WARN/ERROR/FATAL 整行淡色底
    bool levelKeywordColored() const { return m_hlLevelKeyword; }
    bool messageLevelColored() const { return m_hlMessageLevel; }
    bool rowTintEnabled() const { return m_hlRowTint; }

    void scrollToRow(int row, bool center = false);
    void scrollToLine(int line, bool center = false);
    // 当前视口第一行对应的原始行号；恢复位置用
    int  firstVisibleLine() const;
    // 当前视口第一行的可见行下标；搜索定位用
    int  firstVisibleRow() const;
    // 把指定行顶到视口第一行（不改选中）；该行被过滤掉时取其后最近的一条
    void scrollToLineAtTop(int line);
    int  currentRow() const { return m_selCurrent; }
    int  currentLine() const;

    void copySelection();
    void selectAll();
    void invalidateAll();

signals:
    void currentLineChanged(int line);
    void fontPointSizeChanged(int pt);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct RowCache;

    void updateMetrics();
    void updateScrollBars();
    void ensureEngines();
    RowCache &cacheFor(int row);
    void buildRow(int row, RowCache &cache);
    int  rowAt(int y) const;
    void selectionRange(int *first, int *last) const;
    void moveCurrent(int row, bool extend);
    void emitCurrentLine();

    LogDocument *m_doc = nullptr;

    QFont        m_font;
    QFontMetrics m_fm;
    int  m_fontSize = 10;
    int  m_rowHeight = 18;
    int  m_gutterWidth = 60;

    int  m_selAnchor = -1;
    int  m_selCurrent = -1;
    bool m_dragging = false;

    std::unordered_map<int, std::unique_ptr<RowCache>> m_cache;

    // 日志结构正则
    RegexEngine m_engine;
    QString     m_compiledPattern;
    int m_groupTime = -1;
    int m_groupLevel = -1;
    int m_groupMsg = -1;
    int m_groupLoc = -1;

    // 搜索高亮
    RegexEngine m_hlEngine;
    QString     m_hlPattern;
    QString     m_hlText;
    bool        m_hlRegex = false;
    bool        m_hlCase = false;
    QByteArray  m_hlNeedle;

    // 搜索命中
    std::shared_ptr<const QVector<SearchHit>> m_hits;
    int m_activeHit = -1;

    bool m_hlLevelKeyword = true;
    bool m_hlMessageLevel = false;
    bool m_hlRowTint = true;

    int m_hScroll = 0;
};
