#pragma once

#include "logdocument.h"

#include <QMainWindow>
#include <QVector>

class LogView;
class FilterDialog;
class QComboBox;
class QLabel;
class QProgressBar;

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
    void setScrollCheck(bool enabled);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void loadProfiles();
    void saveProfiles();
    void refreshProfileCombo();

    void openFileDialog();
    void reloadFile();

    void applyFilterFromPanel(const FilterSpec &spec);
    void onLoadFinished(bool ok, const QString &message);
    void onLoadProgress(int percent, const QString &stage);
    void onFilterChanged();
    void updateStatus();
    void updateFilterIndicator();
    void runScrollCheck();

    void showFilterDialog(const QString &focus = QString());
    void clearFilters();

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
    QComboBox    *m_profileCombo = nullptr;
    QComboBox    *m_encodingCombo = nullptr;
    QLabel       *m_statusFile = nullptr;
    QLabel       *m_statusCount = nullptr;
    QLabel       *m_statusFilter = nullptr;
    QLabel       *m_statusPerf = nullptr;
    QProgressBar *m_progress = nullptr;

    FilterSpec m_activeSpec;

    // 重新解析（切换配置 / 重载 / 换编码）时保持视口位置
    int  m_restoreLine = -1;
    bool m_reloading = false;
    void rememberScrollPosition();
    void restoreScrollPosition();

    QVector<PatternProfile> m_profiles;
    int  m_currentProfile = 0;
    bool m_suppressSignals = false;
    QString m_shotPath;
    bool    m_shotFilter = false;
    bool    m_shotPrompt = false;
    bool    m_scrollCheck = false;
    bool    m_bench = false;
    bool    m_benchFilter = false;
};