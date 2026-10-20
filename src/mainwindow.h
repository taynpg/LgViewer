#pragma once

#include "logdocument.h"

#include <QMainWindow>
#include <QVector>

#include <memory>

class DropOverlay;
class LogView;
class FilterDialog;
class FindBar;
class QAction;
class QComboBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QMimeData;
class QProgressBar;
class QResizeEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString &path);
    void selectProfile(int index);
    void showTester();
    void showRegexPrompt();
    void showQtAbout();
    void showLicenseInfo();

    // 自检用：加载完成后抓图并退出 / 打印性能数据
    void setAutoScreenshot(const QString &pngPath);
    void setBenchMode(bool enabled);
    void setShotFilterDialog(bool enabled);
    void setShotPromptDialog(bool enabled);
    void setShotFind(const QString &query, bool wholeWord, bool caseSensitive);
    void setScrollCheck(bool enabled);
    // 自检用：模拟"文件拖到窗口上"的提示层，配合 --shot 出图
    void setShotDropHint(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    // 拖拽文件到窗口打开
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void loadProfiles();
    void saveProfiles();
    void refreshProfileCombo();

    void openFileDialog();
    void reloadFile();

    // 拖拽打开
    QStringList localFilesFrom(const QMimeData *mime) const;
    void openDroppedFiles(const QStringList &files);
    void showDropHint(const QStringList &files);
    void hideDropHint();
    void placeDropHint();

    void applyFilterFromPanel(const FilterSpec &spec);
    void onLoadFinished(bool ok, const QString &message);
    void onLoadProgress(int percent, const QString &stage);
    void onFilterChanged();
    void updateStatus();
    void updateFilterIndicator();
    void runScrollCheck();

    void showFilterDialog(const QString &focus = QString());
    void clearFilters();

    // 查找
    void showFind();
    void closeFind();
    void runSearch();
    void onSearchChanged();
    void findNext();
    void findPrev();
    void gotoHit(int index, bool scroll);

    // 切换控件风格（Windows 原生 / Fusion）后重刷所有依赖主题颜色的部件
    void setWindowsStyle(bool on);
    void refreshThemedUi();

    void changeEncoding(int comboIndex);
    void editProfile();
    void newProfile();
    void deleteProfile();
    void showHelp();    void showAbout();
    QStringList sampleLinesForPrompt() const;
    void gotoLine();
    void zoomFont(int delta);
    QString sampleText() const;

    LogDocument  *m_doc = nullptr;
    LogView      *m_view = nullptr;
    FilterDialog *m_filterDlg = nullptr;
    FindBar      *m_findBar = nullptr;
    DropOverlay  *m_dropHint = nullptr;
    QComboBox    *m_profileCombo = nullptr;
    QComboBox    *m_encodingCombo = nullptr;
    QLabel       *m_statusFile = nullptr;
    QLabel       *m_statusCount = nullptr;
    QLabel       *m_statusFilter = nullptr;
    QLabel       *m_statusPerf = nullptr;
    QProgressBar *m_progress = nullptr;

    FilterSpec m_activeSpec;

    // 搜索命中缓存（与 LogDocument 共享同一份，跳转时不再拷贝）
    std::shared_ptr<const QVector<SearchHit>> m_hits;
    int m_hitIndex = -1;

    // 重新解析（切换配置 / 重载 / 换编码）时保持视口位置
    int  m_restoreLine = -1;
    bool m_reloading = false;
    void rememberScrollPosition();
    void restoreScrollPosition();

    QVector<PatternProfile> m_profiles;
    int  m_currentProfile = 0;
    bool m_suppressSignals = false;
    QAction *m_winStyleAction = nullptr;
    QString m_shotPath;
    bool    m_shotFilter = false;
    bool    m_shotPrompt = false;
    QString m_shotFind;
    bool    m_shotFindWhole = false;
    bool    m_shotFindCase = false;
    QString m_shotDropHint;
    bool    m_scrollCheck = false;
    bool    m_bench = false;
    bool    m_benchFilter = false;
};