#include "mainwindow.h"

#include "common.h"
#include "dropzone.h"
#include "filterdialog.h"
#include "findbar.h"
#include "icons.h"
#include "logview.h"
#include "profiledialog.h"
#include "licensedialog.h"
#include "regexhelpdialog.h"
#include "regexpromptdialog.h"
#include "regextesterdialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QResizeEvent>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {

// 「最近打开」保留的条数
constexpr int kMaxRecentFiles = 10;

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < qint64(1024) * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_doc = new LogDocument(this);

    // 先读最近打开记录，buildMenus 里要用它填菜单
    loadRecentFiles();

    // 筛选项在独立窗口里，主界面只留日志视图
    m_filterDlg = new FilterDialog(this);
    m_filterDlg->setDocument(nullptr);

    buildUi();
    buildMenus();

    connect(m_doc, &LogDocument::loadStarted, this, [this](const QString &) {
        m_reloading = true;
        m_progress->show();
        m_progress->setValue(0);
        if (m_findBar->isVisible() && !m_findBar->text().isEmpty())
            m_findBar->showPending();
    });
    connect(m_doc, &LogDocument::loadProgress, this, &MainWindow::onLoadProgress);
    connect(m_doc, &LogDocument::loadFinished, this, &MainWindow::onLoadFinished);
    connect(m_doc, &LogDocument::filterChanged, this, &MainWindow::onFilterChanged);
    connect(m_doc, &LogDocument::searchChanged, this, &MainWindow::onSearchChanged);
    connect(m_filterDlg, &FilterDialog::filterChanged, this, &MainWindow::applyFilterFromPanel);
    connect(m_view, &LogView::fontPointSizeChanged, this, [this](int pt) {
        statusBar()->showMessage(QStringLiteral("字号 %1").arg(pt), 1500);
    });

    connect(m_findBar, &FindBar::queryChanged, this, &MainWindow::runSearch);
    connect(m_findBar, &FindBar::goNext, this, &MainWindow::findNext);
    connect(m_findBar, &FindBar::goPrev, this, &MainWindow::findPrev);
    connect(m_findBar, &FindBar::closed, this, &MainWindow::closeFind);

    loadProfiles();

    m_view->setDocument(m_doc);

    // 拖拽打开：把文件从资源管理器拖到窗口任意位置
    setAcceptDrops(true);
    m_dropHint = new DropOverlay(this);
    m_dropHint->hide();

    resize(1400, 860);
    updateStatus();
    m_progress->hide();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    // 中央区域：日志视图 + 底部查找栏（查找栏默认隐藏，不占高度）
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_view = new LogView(central);
    centralLayout->addWidget(m_view, 1);

    m_findBar = new FindBar(central);
    m_findBar->hide();
    centralLayout->addWidget(m_findBar, 0);

    setCentralWidget(central);

    auto *toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));

    QAction *openAction = toolbar->addAction(lv::icon(lv::Icon::Open), QStringLiteral("打开"));
    QAction *reloadAction = toolbar->addAction(lv::icon(lv::Icon::Reload), QStringLiteral("重新加载"));
    reloadAction->setToolTip(QStringLiteral("重新加载 (F5)"));
    toolbar->addSeparator();
    QAction *findAction = toolbar->addAction(lv::icon(lv::Icon::Find), QStringLiteral("查找"));
    findAction->setToolTip(QStringLiteral("在日志中查找 (Ctrl+F)"));
    QAction *filterAction = toolbar->addAction(lv::icon(lv::Icon::Filter), QStringLiteral("筛选"));
    filterAction->setToolTip(QStringLiteral("筛选条件 (Ctrl+Shift+F)"));
    QAction *clearFilterAction = toolbar->addAction(lv::icon(lv::Icon::ClearFilter), QStringLiteral("清除筛选"));
    clearFilterAction->setToolTip(QStringLiteral("清除所有筛选条件 (Ctrl+Alt+F)"));
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(QStringLiteral(" 编码 ")));
    m_encodingCombo = new QComboBox(toolbar);
    m_encodingCombo->addItem(QStringLiteral("自动检测"), int(TextEncoding::Auto));
    m_encodingCombo->addItem(QStringLiteral("UTF-8"), int(TextEncoding::Utf8));
    m_encodingCombo->addItem(QStringLiteral("GBK / GB18030"), int(TextEncoding::Gbk));
    m_encodingCombo->addItem(QStringLiteral("UTF-16 LE"), int(TextEncoding::Utf16LE));
    m_encodingCombo->addItem(QStringLiteral("UTF-16 BE"), int(TextEncoding::Utf16BE));
    m_encodingCombo->addItem(QStringLiteral("Latin-1"), int(TextEncoding::Latin1));
    toolbar->addWidget(m_encodingCombo);

    toolbar->addWidget(new QLabel(QStringLiteral("  配置 ")));
    m_profileCombo = new QComboBox(toolbar);
    m_profileCombo->setMinimumWidth(240);
    toolbar->addWidget(m_profileCombo);

    QAction *editAction = toolbar->addAction(lv::icon(lv::Icon::EditProfile), QStringLiteral("编辑"));
    QAction *testAction = toolbar->addAction(lv::icon(lv::Icon::Regex), QStringLiteral("正则测试"));
    toolbar->addSeparator();
    QAction *zoomInTb = toolbar->addAction(lv::icon(lv::Icon::ZoomIn), QStringLiteral("A+"));
    zoomInTb->setToolTip(QStringLiteral("放大字号 (Ctrl+=)"));
    QAction *zoomOutTb = toolbar->addAction(lv::icon(lv::Icon::ZoomOut), QStringLiteral("A-"));
    zoomOutTb->setToolTip(QStringLiteral("缩小字号 (Ctrl+-)"));
    connect(zoomInTb, &QAction::triggered, this, [this] { zoomFont(1); });
    connect(zoomOutTb, &QAction::triggered, this, [this] { zoomFont(-1); });

    // 重新绑定带快捷键的动作
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadFile);
    connect(findAction, &QAction::triggered, this, [this] { showFind(); });
    connect(filterAction, &QAction::triggered, this, [this] { showFilterDialog(); });
    connect(clearFilterAction, &QAction::triggered, this, &MainWindow::clearFilters);
    connect(editAction, &QAction::triggered, this, &MainWindow::editProfile);
    connect(testAction, &QAction::triggered, this, &MainWindow::showTester);
    connect(m_encodingCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeEncoding);
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::selectProfile);

    // 状态栏
    m_statusFile = new QLabel(this);
    m_statusCount = new QLabel(this);
    m_statusFilter = new QLabel(this);
    m_statusFilter->setStyleSheet(
        QStringLiteral("QLabel { color:#8a5a00; background:#fff3d6; border-radius:3px; "
                       "padding:0 6px; }"));
    m_statusFilter->hide();
    m_statusPerf = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(240);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("%p%"));

    statusBar()->addWidget(m_statusFile, 1);
    statusBar()->addPermanentWidget(m_statusFilter);
    statusBar()->addPermanentWidget(m_statusCount);
    statusBar()->addPermanentWidget(m_statusPerf);
    statusBar()->addPermanentWidget(m_progress);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction *actOpen = fileMenu->addAction(lv::icon(lv::Icon::Open), QStringLiteral("打开…"));
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::openFileDialog);

    QAction *actReload = fileMenu->addAction(lv::icon(lv::Icon::Reload), QStringLiteral("重新加载"));
    actReload->setShortcut(QKeySequence(Qt::Key_F5));
    connect(actReload, &QAction::triggered, this, &MainWindow::reloadFile);

    QAction *actClose = fileMenu->addAction(lv::icon(lv::Icon::CloseFile), QStringLiteral("关闭文件"));
    connect(actClose, &QAction::triggered, this, [this] {
        closeFind();
        m_doc->closeFile();
        m_filterDlg->setDocument(nullptr);
        m_view->setDocument(m_doc);
        m_activeSpec = FilterSpec();
        updateStatus();
    });

    fileMenu->addSeparator();
    m_recentMenu = fileMenu->addMenu(QStringLiteral("最近打开(&R)"));
    m_recentMenu->setToolTipsVisible(true);
    rebuildRecentMenu();

    fileMenu->addSeparator();
    QAction *actQuit = fileMenu->addAction(lv::icon(lv::Icon::Quit), QStringLiteral("退出"));
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    QAction *actCopy = editMenu->addAction(lv::icon(lv::Icon::Copy), QStringLiteral("复制选中行"));
    actCopy->setShortcut(QKeySequence::Copy);
    connect(actCopy, &QAction::triggered, this, [this] { m_view->copySelection(); });

    QAction *actSelectAll = editMenu->addAction(lv::icon(lv::Icon::SelectAll), QStringLiteral("全选"));
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    connect(actSelectAll, &QAction::triggered, this, [this] { m_view->selectAll(); });

    editMenu->addSeparator();
    QAction *actFind = editMenu->addAction(lv::icon(lv::Icon::Find), QStringLiteral("查找…"));
    actFind->setShortcut(QKeySequence::Find);
    actFind->setToolTip(QStringLiteral("在日志中查找文本 (Ctrl+F)"));
    connect(actFind, &QAction::triggered, this, [this] { showFind(); });

    QAction *actFindNext = editMenu->addAction(lv::icon(lv::Icon::ArrowDown),
                                               QStringLiteral("查找下一个"));
    actFindNext->setShortcut(QKeySequence(Qt::Key_F3));
    connect(actFindNext, &QAction::triggered, this, &MainWindow::findNext);

    QAction *actFindPrev = editMenu->addAction(lv::icon(lv::Icon::ArrowUp),
                                               QStringLiteral("查找上一个"));
    actFindPrev->setShortcut(QKeySequence(QStringLiteral("Shift+F3")));
    connect(actFindPrev, &QAction::triggered, this, &MainWindow::findPrev);

    editMenu->addSeparator();
    QAction *actFilter = editMenu->addAction(lv::icon(lv::Icon::Filter),
                                             QStringLiteral("筛选条件…"));
    actFilter->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(actFilter, &QAction::triggered, this, [this] { showFilterDialog(); });

    QAction *actClearFilter = editMenu->addAction(lv::icon(lv::Icon::ClearFilter), QStringLiteral("清除筛选"));
    actClearFilter->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    connect(actClearFilter, &QAction::triggered, this, &MainWindow::clearFilters);

    QAction *actGoto = editMenu->addAction(lv::icon(lv::Icon::GotoLine), QStringLiteral("跳到行…"));
    actGoto->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(actGoto, &QAction::triggered, this, &MainWindow::gotoLine);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    QAction *actZoomIn = viewMenu->addAction(lv::icon(lv::Icon::ZoomIn), QStringLiteral("放大字号"));
    actZoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(actZoomIn, &QAction::triggered, this, [this] { zoomFont(1); });

    QAction *actZoomOut = viewMenu->addAction(lv::icon(lv::Icon::ZoomOut), QStringLiteral("缩小字号"));
    actZoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(actZoomOut, &QAction::triggered, this, [this] { zoomFont(-1); });

    QAction *actZoomReset = viewMenu->addAction(lv::icon(lv::Icon::ZoomReset), QStringLiteral("重置字号"));
    connect(actZoomReset, &QAction::triggered, this, [this] { m_view->setFontPointSize(10); });

    viewMenu->addSeparator();
    QMenu *hlMenu = viewMenu->addMenu(QStringLiteral("高亮显示(&H)"));

    QAction *actLevelKw = hlMenu->addAction(QStringLiteral("等级关键字着色"));
    actLevelKw->setCheckable(true);
    actLevelKw->setToolTip(QStringLiteral("把 [Info] / [WARN] 这样的等级词按等级颜色显示"));

    QAction *actMsgColor = hlMenu->addAction(QStringLiteral("消息正文按等级着色"));
    actMsgColor->setCheckable(true);
    actMsgColor->setToolTip(
        QStringLiteral("打勾后 INFO / DEBUG / TRACE 的消息正文也用等级颜色显示，与等级关键字同色；"
                       "取消则只有 WARN / ERROR / FATAL 的正文着色"));

    QAction *actRowTint = hlMenu->addAction(QStringLiteral("WARN / ERROR 整行淡色底"));
    actRowTint->setCheckable(true);
    actRowTint->setToolTip(QStringLiteral("给 WARN / ERROR / FATAL 的行铺一层淡色背景"));

    {
        QSettings st;
        actLevelKw->setChecked(st.value(QStringLiteral("view/levelKeywordColor"), true).toBool());
        actMsgColor->setChecked(st.value(QStringLiteral("view/messageLevelColor"), false).toBool());
        actRowTint->setChecked(st.value(QStringLiteral("view/rowTint"), true).toBool());
        m_view->setLevelKeywordColored(actLevelKw->isChecked());
        m_view->setMessageLevelColored(actMsgColor->isChecked());
        m_view->setRowTintEnabled(actRowTint->isChecked());
    }

    connect(actLevelKw, &QAction::toggled, this, [this](bool on) {
        m_view->setLevelKeywordColored(on);
        QSettings().setValue(QStringLiteral("view/levelKeywordColor"), on);
    });
    connect(actMsgColor, &QAction::toggled, this, [this](bool on) {
        m_view->setMessageLevelColored(on);
        QSettings().setValue(QStringLiteral("view/messageLevelColor"), on);
    });
    connect(actRowTint, &QAction::toggled, this, [this](bool on) {
        m_view->setRowTintEnabled(on);
        QSettings().setValue(QStringLiteral("view/rowTint"), on);
    });

    viewMenu->addSeparator();
    QAction *actTop = viewMenu->addAction(lv::icon(lv::Icon::ScrollTop), QStringLiteral("回到开头"));
    actTop->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    connect(actTop, &QAction::triggered, this, [this] { m_view->scrollToRow(0); });

    QAction *actBottom = viewMenu->addAction(lv::icon(lv::Icon::ScrollBottom), QStringLiteral("跳到末尾"));
    actBottom->setShortcut(QKeySequence(QStringLiteral("Ctrl+End")));
    connect(actBottom, &QAction::triggered, this, [this] {
        m_view->scrollToRow(qMax(0, m_doc->rowCount() - 1), true);
    });

    // 控件风格：Qt 自带的 Windows 原生风格 ↔ Fusion 跨平台风格
    viewMenu->addSeparator();
    const QString winKey = lv::windowsStyleKey();
    m_winStyleAction = viewMenu->addAction(QStringLiteral("使用 Windows 原生控件风格"));
    m_winStyleAction->setCheckable(true);
    if (winKey.isEmpty()) {
        m_winStyleAction->setEnabled(false);
        m_winStyleAction->setToolTip(QStringLiteral("当前平台没有 Qt 自带的 Windows 控件风格"));
    } else {
        m_winStyleAction->setToolTip(
            QStringLiteral("勾选：Qt 的「%1」风格，用系统原生控件外观；"
                           "取消：Fusion 跨平台风格").arg(winKey));
    }
    m_winStyleAction->setChecked(
        !winKey.isEmpty() && lv::currentUiStyle().compare(winKey, Qt::CaseInsensitive) == 0);
    connect(m_winStyleAction, &QAction::toggled, this, &MainWindow::setWindowsStyle);

    QMenu *toolMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    QAction *actTester = toolMenu->addAction(lv::icon(lv::Icon::Regex), QStringLiteral("正则表达式测试器…"));
    actTester->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(actTester, &QAction::triggered, this, &MainWindow::showTester);

    QAction *actHelp = toolMenu->addAction(lv::icon(lv::Icon::Book), QStringLiteral("正则语法速查…"));
    actHelp->setShortcut(QKeySequence(Qt::Key_F1));
    connect(actHelp, &QAction::triggered, this, &MainWindow::showHelp);

    QAction *actPrompt = toolMenu->addAction(lv::icon(lv::Icon::Spark),
                                             QStringLiteral("正则编写介绍（复制给 AI）…"));
    actPrompt->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    actPrompt->setToolTip(QStringLiteral("一份可直接复制给 AI 的正则需求说明，附带当前日志样例行"));
    connect(actPrompt, &QAction::triggered, this, &MainWindow::showRegexPrompt);

    toolMenu->addSeparator();
    QAction *actNewProfile = toolMenu->addAction(lv::icon(lv::Icon::NewProfile), QStringLiteral("新建配置…"));
    connect(actNewProfile, &QAction::triggered, this, &MainWindow::newProfile);

    QAction *actEditProfile = toolMenu->addAction(lv::icon(lv::Icon::EditProfile), QStringLiteral("编辑当前配置…"));
    connect(actEditProfile, &QAction::triggered, this, &MainWindow::editProfile);

    QAction *actDeleteProfile = toolMenu->addAction(lv::icon(lv::Icon::DeleteProfile), QStringLiteral("删除当前配置"));
    connect(actDeleteProfile, &QAction::triggered, this, &MainWindow::deleteProfile);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    // Qt 自带的「关于 Qt」对话框（QMessageBox::aboutQt），文案与许可链接由 Qt 官方维护
    QAction *actQt = helpMenu->addAction(lv::icon(lv::Icon::About), QStringLiteral("关于 Qt"));
    actQt->setToolTip(QStringLiteral("Qt 版本与许可信息（Qt 自带对话框）"));
    connect(actQt, &QAction::triggered, this, &MainWindow::showQtAbout);

    QAction *actLicense = helpMenu->addAction(lv::icon(lv::Icon::Book),
                                              QStringLiteral("开源许可…"));
    actLicense->setToolTip(QStringLiteral("Qt LGPL v3 全文、PCRE2 BSD-3 许可与动态链接说明"));
    connect(actLicense, &QAction::triggered, this, &MainWindow::showLicenseInfo);

    QAction *actAbout = helpMenu->addAction(lv::icon(lv::Icon::About), QStringLiteral("关于 LgViewer"));
    connect(actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::loadProfiles()
{
    m_profiles = PatternStore().load();
    if (m_profiles.isEmpty())
        m_profiles = PatternProfile::builtins();
    m_currentProfile = 0;
    refreshProfileCombo();
    m_doc->setProfile(m_profiles.at(m_currentProfile));
}

void MainWindow::saveProfiles()
{
    PatternStore().save(m_profiles);
}

void MainWindow::refreshProfileCombo()
{
    m_suppressSignals = true;
    m_profileCombo->clear();
    for (const PatternProfile &p : m_profiles)
        m_profileCombo->addItem(p.name);
    m_currentProfile = qBound(0, m_currentProfile, qMax(0, m_profiles.size() - 1));
    m_profileCombo->setCurrentIndex(m_currentProfile);
    m_suppressSignals = false;
}

void MainWindow::selectProfile(int index)
{
    if (m_suppressSignals || index < 0 || index >= m_profiles.size())
        return;
    m_currentProfile = index;
    rememberScrollPosition();
    m_doc->setProfile(m_profiles.at(index));
    if (!m_doc->isLoaded())
        m_view->invalidateAll();
    updateStatus();
}

void MainWindow::rememberScrollPosition()
{
    if (m_doc->isLoaded() && m_doc->lineCount() > 0)
        m_restoreLine = m_view->firstVisibleLine();
    else
        m_restoreLine = -1;
}

void MainWindow::restoreScrollPosition()
{
    if (m_restoreLine < 0 || !m_doc->isLoaded())
        return;
    m_view->scrollToLineAtTop(m_restoreLine);
    // 筛选还在后台跑时保留目标行，等筛选完成再校正一次
    if (!m_doc->isFiltering())
        m_restoreLine = -1;
}

// ---------------------------------------------------------------------------
// 最近打开的文件
// ---------------------------------------------------------------------------
void MainWindow::loadRecentFiles()
{
    QSettings settings;
    const QStringList saved = settings.value(QStringLiteral("recent/files")).toStringList();

    QStringList cleaned;
    for (const QString &p : saved) {
        if (p.isEmpty())
            continue;
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (!cleaned.contains(abs))
            cleaned.append(abs);
        if (cleaned.size() >= kMaxRecentFiles)
            break;
    }
    m_recentFiles = cleaned;
}

void MainWindow::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;

    const QString abs = QFileInfo(path).absoluteFilePath();
    m_recentFiles.removeAll(abs);
    m_recentFiles.prepend(abs);
    while (m_recentFiles.size() > kMaxRecentFiles)
        m_recentFiles.removeLast();

    QSettings().setValue(QStringLiteral("recent/files"), m_recentFiles);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;

    m_recentMenu->clear();

    // 同名的文件补上父目录名，免得列表里两个 sample.log 分不清
    QHash<QString, int> nameCount;
    for (const QString &p : m_recentFiles)
        ++nameCount[QFileInfo(p).fileName()];

    for (int i = 0; i < m_recentFiles.size(); ++i) {
        const QString path = m_recentFiles.at(i);
        const QFileInfo fi(path);

        QString text = fi.fileName();
        if (nameCount.value(fi.fileName()) > 1)
            text += QStringLiteral("  —  ") + fi.dir().dirName();

        if (i < 9)
            text = QStringLiteral("&%1  %2").arg(i + 1).arg(text);
        else
            text = QStringLiteral("%1  %2").arg(i + 1).arg(text);

        QAction *act = m_recentMenu->addAction(text);
        act->setToolTip(QDir::toNativeSeparators(path));
        act->setStatusTip(QDir::toNativeSeparators(path));
        if (i < 9)
            act->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        connect(act, &QAction::triggered, this, [this, path] { openRecentFile(path); });
    }

    if (m_recentFiles.isEmpty()) {
        QAction *none = m_recentMenu->addAction(QStringLiteral("（暂无记录）"));
        none->setEnabled(false);
    }

    m_recentMenu->addSeparator();
    QAction *actClear = m_recentMenu->addAction(QStringLiteral("清除最近记录"));
    actClear->setEnabled(!m_recentFiles.isEmpty());
    connect(actClear, &QAction::triggered, this, [this] {
        m_recentFiles.clear();
        QSettings().setValue(QStringLiteral("recent/files"), m_recentFiles);
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        QMessageBox::warning(this, QStringLiteral("文件不存在"),
                             QStringLiteral("找不到文件：\n%1\n\n该记录已从最近列表中移除。")
                                 .arg(QDir::toNativeSeparators(path)));
        m_recentFiles.removeAll(path);
        QSettings().setValue(QStringLiteral("recent/files"), m_recentFiles);
        rebuildRecentMenu();
        return;
    }

    if (!m_doc->filePath().isEmpty()
        && QFileInfo(m_doc->filePath()).absoluteFilePath() == fi.absoluteFilePath()) {
        reloadFile();
        return;
    }
    openPath(path);
}

QString MainWindow::lastOpenDir() const
{
    // 最近一次打开的文件所在目录（跳过已被删掉的记录）
    for (const QString &p : m_recentFiles) {
        const QFileInfo fi(p);
        if (fi.exists())
            return fi.absolutePath();
    }

    const QString last = QSettings().value(QStringLiteral("recent/lastDir")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;

    return QDir::homePath();
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开日志文件"), lastOpenDir(),
        QStringLiteral("日志文件 (*.txt *.log *.log.*);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("recent/lastDir"), QFileInfo(path).absolutePath());
    openPath(path);
}

void MainWindow::openPath(const QString &path)
{
    const auto enc = TextEncoding(m_encodingCombo->currentData().toInt());
    m_restoreLine = -1; // 打开新文件回到开头
    m_doc->openFile(path, enc);
    setWindowTitle(QStringLiteral("LgViewer — %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::reloadFile()
{
    if (m_doc->filePath().isEmpty())
        return;
    rememberScrollPosition();
    m_doc->openFile(m_doc->filePath(), TextEncoding(m_encodingCombo->currentData().toInt()));
}

// ---------------------------------------------------------------------------
// 拖拽文件到窗口打开
// ---------------------------------------------------------------------------
QStringList MainWindow::localFilesFrom(const QMimeData *mime) const
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &u : urls) {
        if (!u.isLocalFile())
            continue;
        const QString path = u.toLocalFile();
        if (QFileInfo(path).isFile())
            files.append(path);
    }
    return files;
}

void MainWindow::showDropHint(const QStringList &files)
{
    if (!m_dropHint || files.isEmpty())
        return;
    placeDropHint();
    m_dropHint->showFiles(files, m_doc->filePath());
    m_dropHint->show();
    m_dropHint->raise();
}

void MainWindow::hideDropHint()
{
    if (m_dropHint)
        m_dropHint->clear();
}

void MainWindow::placeDropHint()
{
    if (m_dropHint)
        m_dropHint->setGeometry(rect());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QStringList files = localFilesFrom(event->mimeData());
    if (files.isEmpty()) {
        if (event->mimeData()->hasUrls())
            statusBar()->showMessage(QStringLiteral("只能拖入本地文件（暂不支持文件夹）"), 4000);
        event->ignore();
        return;
    }
    showDropHint(files);
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (localFilesFrom(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
    hideDropHint();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    hideDropHint();

    const QStringList files = localFilesFrom(event->mimeData());
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();

    raise();
    activateWindow();
    openDroppedFiles(files);
}

void MainWindow::openDroppedFiles(const QStringList &files)
{
    if (files.isEmpty())
        return;

    const QString path = files.first();
    const QString name = QFileInfo(path).fileName();
    const bool same = !m_doc->filePath().isEmpty()
            && QFileInfo(m_doc->filePath()).absoluteFilePath() == QFileInfo(path).absoluteFilePath();

    if (same) {
        reloadFile(); // 拖入的正是当前文件：原地刷新，保留滚动位置
    } else {
        openPath(path);
        setFocus(Qt::OtherFocusReason);
    }

    QString msg = same ? QStringLiteral("已重新加载 %1") : QStringLiteral("已打开 %1");
    msg = msg.arg(name);
    if (files.size() > 1)
        msg += QStringLiteral("，忽略另外 %1 个文件").arg(files.size() - 1);
    statusBar()->showMessage(msg, 4000);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    placeDropHint();
}

void MainWindow::changeEncoding(int index)
{
    if (m_suppressSignals || index < 0)
        return;
    if (m_doc->filePath().isEmpty())
        return;
    rememberScrollPosition();
    m_doc->openFile(m_doc->filePath(), TextEncoding(m_encodingCombo->itemData(index).toInt()));
}

void MainWindow::applyFilterFromPanel(const FilterSpec &spec)
{
    FilterSpec s = spec;
    s.highlightOnly = false;
    if (spec.highlightOnly)
        s.text.clear();

    m_activeSpec = spec;
    m_doc->applyFilter(s);

    if (spec.text.isEmpty())
        m_view->clearHighlight();
    else
        m_view->setHighlightPattern(spec.text, spec.textIsRegex, spec.textCaseSensitive);

    updateStatus();
}

void MainWindow::showFilterDialog(const QString &focus)
{
    m_filterDlg->setDocument(m_doc);
    if (!m_filterDlg->isVisible())
        m_filterDlg->show();
    m_filterDlg->raise();
    m_filterDlg->activateWindow();
    if (focus == QLatin1String("keyword"))
        m_filterDlg->focusKeyword();
}

void MainWindow::clearFilters()
{
    m_filterDlg->resetFilters();
}

// ---------------------------------------------------------------------------
// 查找
// ---------------------------------------------------------------------------
void MainWindow::showFind()
{
    if (!m_doc->isLoaded()) {
        statusBar()->showMessage(QStringLiteral("先打开一个日志文件再查找"), 2000);
        return;
    }
    m_findBar->activate();
    m_findBar->raise();
}

void MainWindow::closeFind()
{
    m_findBar->hide();
    m_doc->clearSearch();
    m_hits.reset();
    m_hitIndex = -1;
    m_view->setSearchHits(nullptr);
    m_view->setActiveHit(-1);
    m_view->setFocus();
}

void MainWindow::runSearch()
{
    m_hitIndex = -1;

    if (m_findBar->text().isEmpty()) {
        m_doc->clearSearch();
        m_hits.reset();
        m_view->setSearchHits(nullptr);
        m_view->setActiveHit(-1);
        m_findBar->showIdle();
        return;
    }

    m_findBar->showPending();
    m_doc->setSearchQuery(m_findBar->text(), m_findBar->caseSensitive(),
                          m_findBar->wholeWord());
}

void MainWindow::onSearchChanged()
{
    m_hits = m_doc->searchHits();
    m_hitIndex = -1;
    m_view->setActiveHit(-1);
    m_view->setSearchHits(m_hits);

    const int total = m_hits ? int(m_hits->size()) : 0;
    if (total == 0) {
        m_findBar->showNoMatch();
    } else {
        m_findBar->showCount(total, m_doc->searchTruncated());
        if (m_findBar->isVisible()) {
            // 从当前视口往下找第一处命中，避免输入时视图乱跳到文件开头
            const int row = qMax(0, m_view->firstVisibleRow());
            const auto it = std::lower_bound(m_hits->begin(), m_hits->end(), quint32(row),
                                             [](const SearchHit &h, quint32 r) { return h.row < r; });
            int index = int(it - m_hits->begin());
            if (index >= total)
                index = 0;
            gotoHit(index, true);
        }
    }

    if (!m_shotFind.isEmpty()) {
        std::fprintf(stderr, "[find] query=\"%s\" total=%d truncated=%d took=%.3fs firstRow=%d\n",
                     qPrintable(m_doc->searchQuery()), total, int(m_doc->searchTruncated()),
                     m_doc->searchSeconds(),
                     total > 0 ? int(m_hits->first().row) : -1);
        std::fflush(stderr);
    }
}

void MainWindow::gotoHit(int index, bool scroll)
{
    if (!m_hits || m_hits->isEmpty())
        return;

    const int n = int(m_hits->size());
    index = ((index % n) + n) % n;
    m_hitIndex = index;

    const SearchHit &hit = m_hits->at(index);
    m_view->setActiveHit(index);
    if (scroll) {
        const int line = int(m_doc->rowLine(int(hit.row)));
        m_view->scrollToLine(line, true);
    }
    m_findBar->showPosition(index + 1, n, m_doc->searchTruncated());
}

void MainWindow::findNext()
{
    if (!m_findBar->isVisible())
        m_findBar->activate();
    if (!m_hits || m_hits->isEmpty()) {
        runSearch();
        return;
    }
    gotoHit(m_hitIndex < 0 ? 0 : m_hitIndex + 1, true);
}

void MainWindow::findPrev()
{
    if (!m_findBar->isVisible())
        m_findBar->activate();
    if (!m_hits || m_hits->isEmpty()) {
        runSearch();
        return;
    }
    const int n = int(m_hits->size());
    gotoHit(m_hitIndex <= 0 ? n - 1 : m_hitIndex - 1, true);
}

void MainWindow::onLoadProgress(int percent, const QString &stage)
{
    m_progress->setVisible(true);
    m_progress->setValue(percent);
    m_progress->setFormat(QStringLiteral("%1% · %2").arg(percent).arg(stage));
}

void MainWindow::onLoadFinished(bool ok, const QString &message)
{
    m_progress->hide();
    if (!ok) {
        m_reloading = false;
        m_restoreLine = -1;
        QMessageBox::warning(this, QStringLiteral("打开失败"), message);
        updateStatus();
        return;
    }

    m_filterDlg->setDocument(m_doc);
    m_view->setDocument(m_doc);
    if (m_restoreLine < 0)
        m_view->scrollToRow(0);

    // 记进"最近打开"（重载也会把它顶到最前）
    addRecentFile(m_doc->filePath());

    m_suppressSignals = true;
    // 显示实际识别到的编码
    const int enc = int(m_doc->encoding());
    const int idx = m_encodingCombo->findData(enc);
    if (idx >= 0 && m_encodingCombo->currentData().toInt() != enc)
        m_encodingCombo->setCurrentIndex(idx);
    m_suppressSignals = false;

    applyFilterFromPanel(m_filterDlg->currentSpec());
    m_reloading = false;
    restoreScrollPosition();
    updateStatus();

    if (m_exitAfterLoad) {
        m_exitAfterLoad = false;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }

    if (m_shotRecent) {
        m_shotRecent = false;
        const QString path = m_shotPath;
        m_shotPath.clear();
        QTimer::singleShot(200, this, [this] {
            const QRect bar = menuBar()->geometry();
            m_recentMenu->popup(mapToGlobal(QPoint(bar.left() + 12, bar.bottom() + 2)));
        });
        QTimer::singleShot(700, this, [this, path] {
            if (!path.isEmpty())
                m_recentMenu->grab().save(path);
            QCoreApplication::quit();
        });
        return;
    }

    if (m_scrollCheck) {
        m_scrollCheck = false;
        runScrollCheck();
        return;
    }

    if (m_bench) {
        std::fprintf(stderr,
                     "[bench] file=%s size=%.1fMB lines=%d parse=%.3fs filter=%.3fs enc=%s "
                     "minMs=%lld maxMs=%lld\n",
                     qPrintable(QFileInfo(m_doc->filePath()).fileName()),
                     double(m_doc->fileSize()) / (1024.0 * 1024.0), m_doc->lineCount(),
                     m_doc->parseSeconds(), m_doc->filterSeconds(),
                     qPrintable(lv::encodingLabel(m_doc->encoding())),
                     (long long)m_doc->minTimeMs(), (long long)m_doc->maxTimeMs());
        std::fflush(stderr);

        // 顺手验证一次组合筛选：WARN/ERROR + 文本
        FilterSpec f;
        f.levelMask = quint8(lv::levelBit(lv::Level::Warn) | lv::levelBit(lv::Level::Error));
        f.text = QStringLiteral("超时");
        m_benchFilter = true;
        m_doc->applyFilter(f);
    }

    // 自检：--find= 打开查找栏并搜索（配合 --shot 出图）
    if (!m_shotFind.isEmpty()) {
        const QString query = m_shotFind;
        m_findBar->setWholeWord(m_shotFindWhole);
        m_findBar->setCaseSensitive(m_shotFindCase);
        QTimer::singleShot(150, this, [this, query] {
            showFind();
            m_findBar->activate(query);
        });
    }

    if (!m_shotPath.isEmpty()) {
        const QString path = m_shotPath;
        if (m_shotPrompt) {
            QTimer::singleShot(250, this, [this] { showRegexPrompt(); });
            return;
        }
        m_shotPath.clear();
        if (!m_shotDropHint.isEmpty()) {
            // 自检：画出"拖拽文件进来"的提示层
            const QString target = m_shotDropHint;
            QTimer::singleShot(150, this, [this, target] { showDropHint(QStringList{target}); });
            QTimer::singleShot(650, this, [this, path] {
                grab().save(path);
                QCoreApplication::quit();
            });
        } else if (!m_shotFind.isEmpty()) {
            QTimer::singleShot(1500, this, [this, path] {
                grab().save(path);
                QCoreApplication::quit();
            });
        } else if (m_shotFilter) {
            m_filterDlg->setDocument(m_doc);
            m_filterDlg->show();
            QTimer::singleShot(600, this, [this, path] {
                m_filterDlg->grab().save(path);
                QCoreApplication::quit();
            });
        } else {
            QTimer::singleShot(700, this, [this, path] {
                grab().save(path);
                QCoreApplication::quit();
            });
        }
    }
}

void MainWindow::setAutoScreenshot(const QString &pngPath)
{
    m_shotPath = pngPath;
}

void MainWindow::setBenchMode(bool enabled)
{
    m_bench = enabled;
}

void MainWindow::setShotFilterDialog(bool enabled)
{
    m_shotFilter = enabled;
}

void MainWindow::setShotPromptDialog(bool enabled)
{
    m_shotPrompt = enabled;
}

void MainWindow::setShotFind(const QString &query, bool wholeWord, bool caseSensitive)
{
    m_shotFind = query;
    m_shotFindWhole = wholeWord;
    m_shotFindCase = caseSensitive;
}

void MainWindow::setScrollCheck(bool enabled)
{
    m_scrollCheck = enabled;
}

void MainWindow::setShotDropHint(const QString &path)
{
    m_shotDropHint = path;
}

void MainWindow::setExitAfterLoad(bool enabled)
{
    m_exitAfterLoad = enabled;
}

void MainWindow::setShotRecentMenu(bool enabled)
{
    m_shotRecent = enabled;
}

// 自检：滚到中间 → 切换配置 → 打印切换前后视口首行，确认位置没有跳回开头
void MainWindow::runScrollCheck()
{
    const int target = qMin(500000, qMax(0, m_doc->lineCount() - 1));
    m_view->scrollToLine(target);
    std::fprintf(stderr, "[check] target=%d before=%d\n", target, m_view->firstVisibleLine());
    std::fflush(stderr);

    QTimer::singleShot(300, this, [this] {
        selectProfile(3);
        QTimer::singleShot(1800, this, [this] {
            std::fprintf(stderr, "[check] after=%d\n", m_view->firstVisibleLine());
            std::fflush(stderr);
            QCoreApplication::quit();
        });
    });
}

void MainWindow::onFilterChanged()
{
    if (m_benchFilter) {
        m_benchFilter = false;
        std::fprintf(stderr, "[bench] filter(WARN|ERROR + \"超时\") -> %d rows in %.3fs\n",
                     m_doc->rowCount(), m_doc->filterSeconds());
        std::fflush(stderr);
    }
    m_view->invalidateAll();
    if (!m_reloading)
        restoreScrollPosition();
    updateStatus();
}

void MainWindow::updateStatus()
{
    if (!m_doc->isLoaded()) {
        m_statusFile->setText(QStringLiteral("未打开文件 · Ctrl+O 打开日志"));
        m_statusCount->clear();
        m_statusPerf->clear();
        m_statusFilter->hide();
        return;
    }

    const QLocale locale;
    const QFileInfo fi(m_doc->filePath());
    m_statusFile->setText(QStringLiteral("%1  ·  %2").arg(fi.fileName(), humanSize(m_doc->fileSize())));

    const int total = m_doc->lineCount();
    const int shown = m_doc->rowCount();
    if (shown == total) {
        m_statusCount->setText(QStringLiteral("%1 行").arg(locale.toString(qint64(total))));
    } else {
        m_statusCount->setText(QStringLiteral("显示 %1 / %2 行")
                                   .arg(locale.toString(qint64(shown)),
                                        locale.toString(qint64(total))));
    }

    m_statusPerf->setText(QStringLiteral("解析 %1s · 筛选 %2s · %3")
                              .arg(m_doc->parseSeconds(), 0, 'f', 2)
                              .arg(m_doc->filterSeconds(), 0, 'f', 2)
                              .arg(lv::encodingLabel(m_doc->encoding())));

    updateFilterIndicator();
}

void MainWindow::updateFilterIndicator()
{
    QStringList parts;
    if (m_activeSpec.timeEnabled)
        parts << QStringLiteral("时间");
    if (m_activeSpec.levelMask != lv::kAllLevelsMask) {
        QStringList names;
        for (int i = 0; i < lv::kLevelCount; ++i) {
            if (m_activeSpec.levelMask & lv::levelBit(lv::Level(i)))
                names << lv::levelDisplayName(lv::Level(i));
        }
        parts << (names.isEmpty() ? QStringLiteral("无等级") : names.join(QLatin1Char('/')));
    }
    if (!m_activeSpec.text.isEmpty())
        parts << (m_activeSpec.highlightOnly ? QStringLiteral("高亮") : QStringLiteral("关键字"));

    if (parts.isEmpty() || !m_doc->isLoaded()) {
        m_statusFilter->hide();
        return;
    }
    m_statusFilter->setText(QStringLiteral("筛选 %1").arg(parts.join(QStringLiteral(" + "))));
    m_statusFilter->setToolTip(QStringLiteral("当前筛选条件，Ctrl+Shift+F 修改 / Ctrl+Alt+F 清除"));
    m_statusFilter->show();
}

void MainWindow::editProfile()
{
    if (m_currentProfile < 0 || m_currentProfile >= m_profiles.size())
        return;
    ProfileDialog dlg(m_profiles.at(m_currentProfile), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_profiles[m_currentProfile] = dlg.profile();
        saveProfiles();
        refreshProfileCombo();
        m_doc->setProfile(m_profiles.at(m_currentProfile));
        updateStatus();
    }
}

void MainWindow::newProfile()
{
    PatternProfile p;
    p.name = QStringLiteral("新配置 %1").arg(m_profiles.size() + 1);
    p.pattern = m_profiles.isEmpty() ? QStringLiteral("^(?<msg>.*)$") : m_profiles.at(m_currentProfile).pattern;

    ProfileDialog dlg(p, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_profiles.append(dlg.profile());
        m_currentProfile = m_profiles.size() - 1;
        saveProfiles();
        refreshProfileCombo();
        m_doc->setProfile(m_profiles.at(m_currentProfile));
    }
}

void MainWindow::deleteProfile()
{
    if (m_profiles.size() <= 1 || m_currentProfile < 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("删除配置"),
                              QStringLiteral("确定删除配置「%1」？")
                                  .arg(m_profiles.at(m_currentProfile).name))
        != QMessageBox::Yes)
        return;
    m_profiles.remove(m_currentProfile);
    m_currentProfile = qMax(0, m_currentProfile - 1);
    saveProfiles();
    refreshProfileCombo();
    m_doc->setProfile(m_profiles.at(m_currentProfile));
}

QString MainWindow::sampleText() const
{
    if (m_doc && m_doc->isLoaded() && m_doc->lineCount() > 0) {
        const int n = qMin(80, m_doc->lineCount());
        QStringList lines;
        lines.reserve(n);
        for (int i = 0; i < n; ++i)
            lines << m_doc->lineText(i);
        return lines.join(QLatin1Char('\n'));
    }
    return QString::fromUtf8(
        u8"[2026-05-29 08:44:46.210][info]: 创建订单 [ORD-20260529-0012]，金额 [128.50] 元@//order_service.cpp/:OrderService::Submit:96,4012\n"
        u8"[2026-05-29 08:44:46.210][debug]: 订单 [ORD-20260529-0012] 已写入数据库，耗时 6 ms@//order_service.cpp/:OrderService::OnWrite:162,4012\n"
        u8"[2026-05-29 08:44:46.273][info]: 支付网关响应：[SUCCESS,0000]，流水号 [PAY2026052900001]@//payment_client.cpp/:PaymentClient::Send:123,4012\n"
        u8"[2026-05-29 08:44:46.274][warn]: 调用库存服务超时，准备第 2 次重试@//payment_client.cpp/:PaymentClient::Retry:141,4012\n"
        u8"\n"
        u8"14:28:12.588 [Info] scheduler tick, pending tasks = 3\n"
        u8"14:28:12.589 [Info] worker[2] start job JOB-2041\n"
        u8"14:28:12.596 [Debug] job JOB-2041 reading 128 records\n"
        u8"14:28:12.609 [Error] connect to upstream 192.0.2.12:9000 timeout\n");
}

void MainWindow::showTester()
{
    PatternProfile p = (m_currentProfile >= 0 && m_currentProfile < m_profiles.size())
        ? m_profiles.at(m_currentProfile)
        : m_doc->profile();

    RegexTesterDialog dlg(p, sampleText(), this);
    connect(&dlg, &RegexTesterDialog::patternApplied, this, [this](const QString &pattern) {
        if (m_currentProfile < 0 || m_currentProfile >= m_profiles.size())
            return;
        m_profiles[m_currentProfile].pattern = pattern.trimmed();
        saveProfiles();
        m_doc->setProfile(m_profiles.at(m_currentProfile));
        updateStatus();
    });

    if (!m_shotPath.isEmpty()) {
        const QString path = m_shotPath;
        m_shotPath.clear();
        QTimer::singleShot(500, &dlg, [&dlg, path] {
            std::fprintf(stderr, "[tester] shot -> %s\n", qPrintable(path));
            std::fflush(stderr);
            const bool ok = dlg.grab().save(path);
            std::fprintf(stderr, "[tester] save ok=%d\n", int(ok));
            std::fflush(stderr);
            QCoreApplication::exit(0);
        });
        dlg.exec();
        return;
    }

    dlg.exec();
}

QStringList MainWindow::sampleLinesForPrompt() const
{
    QStringList out;
    if (!m_doc || !m_doc->isLoaded())
        return out;

    const int total = m_doc->lineCount();
    if (total <= 0)
        return out;

    QSet<int> usedLines;
    QSet<int> usedSignatures;

    // force=true 时不看签名，用于补齐开头几行
    auto take = [&](int line, bool force) {
        if (line < 0 || line >= total || out.size() >= 28 || usedLines.contains(line))
            return false;
        const QString text = m_doc->lineText(line);
        if (text.trimmed().isEmpty())
            return false;
        const LineMeta m = m_doc->meta(line);
        const int sig = int(m.level) * 2 + (m.isContinuation() ? 1 : 0);
        if (!force && usedSignatures.contains(sig))
            return false;
        usedSignatures.insert(sig);
        usedLines.insert(line);
        out.append(text);
        return true;
    };

    // 1) 开头 10 行：日志的格式声明、启动信息通常都在这里
    for (int i = 0, taken = 0; i < total && taken < 10; ++i) {
        if (take(i, taken < 4))
            ++taken;
    }

    // 2) 全文等距抽样：补足不同等级、以及续行/未匹配这类形态
    const int step = qMax(1, total / 90);
    for (int i = step; i < total && out.size() < 24; i += step)
        take(i, false);

    // 3) 还太少（小文件或格式高度重复）就从头补齐
    for (int i = 0; i < total && out.size() < 12; ++i)
        take(i, true);

    return out;
}

void MainWindow::showRegexPrompt()
{
    RegexPromptDialog dlg(sampleLinesForPrompt(), this);

    if (!m_shotPath.isEmpty()) {
        const QString path = m_shotPath;
        m_shotPath.clear();
        QTimer::singleShot(500, &dlg, [&dlg, path] {
            const bool ok = dlg.grab().save(path);
            std::fprintf(stderr, "[prompt] shot=%s ok=%d\n", qPrintable(path), int(ok));
            std::fflush(stderr);
            QCoreApplication::exit(0);
        });
        dlg.exec();
        return;
    }

    dlg.exec();
}

void MainWindow::showHelp()
{
    RegexHelpDialog dlg(this);
    dlg.exec();
}

void MainWindow::showQtAbout()
{
    // Qt 自带实现：显示运行版本、版权与 LGPL/GPL 官方链接
    QMessageBox::aboutQt(this, QStringLiteral("关于 Qt"));
}

void MainWindow::showLicenseInfo()
{
    LicenseDialog dlg(this);

    if (!m_shotPath.isEmpty()) {
        const QString path = m_shotPath;
        m_shotPath.clear();
        QTimer::singleShot(500, &dlg, [&dlg, path] {
            const bool ok = dlg.grab().save(path);
            std::fprintf(stderr, "[license] shot=%s ok=%d\n", qPrintable(path), int(ok));
            std::fflush(stderr);
            QCoreApplication::exit(0);
        });
        dlg.exec();
        return;
    }

    dlg.exec();
}

void MainWindow::showAbout()
{
    char buf[64] = {0};
    pcre2_config(PCRE2_CONFIG_VERSION, buf);

    QMessageBox::about(
        this, QStringLiteral("关于 LgViewer"),
        QStringLiteral(
            "<h3>LgViewer %1</h3>"
            "<p>跨平台大文本日志查看器</p>"
            "<ul>"
            "<li>Qt %2（Widgets，自绘虚拟化视图）</li>"
            "<li>PCRE2 %3（UTF + UCP，JIT 加速）</li>"
            "<li>内存映射读取，行索引 + 后台线程解析</li>"
            "</ul>"
            "<p style=\"color:#6a737d\">LgViewer 以 MIT 许可开源；Qt 以 LGPL v3 动态链接使用，"
            "详见「帮助 → 开源许可」。</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                 QString::fromLatin1(buf)));
}

void MainWindow::gotoLine()
{
    if (!m_doc->isLoaded())
        return;
    bool ok = false;
    const int line = QInputDialog::getInt(this, QStringLiteral("跳到行"), QStringLiteral("行号："),
                                          m_view->currentLine() + 1, 1,
                                          qMax(1, m_doc->lineCount()), 1, &ok);
    if (ok)
        m_view->scrollToLine(line - 1, true);
}

void MainWindow::zoomFont(int delta)
{
    m_view->setFontPointSize(m_view->fontPointSize() + delta);
}

void MainWindow::setWindowsStyle(bool on)
{
    const QString winKey = lv::windowsStyleKey();
    const QString key = (on && !winKey.isEmpty()) ? winKey : lv::fusionStyleKey();

    if (!lv::applyUiStyle(key) && on) {
        // 平台没有 Windows 风格：把勾去掉（会再次进到这里，这次走 Fusion 分支）
        if (m_winStyleAction)
            m_winStyleAction->setChecked(false);
        return;
    }

    QSettings().setValue(QStringLiteral("view/uiStyle"),
                         on ? QStringLiteral("windows") : QStringLiteral("fusion"));
    refreshThemedUi();
    statusBar()->showMessage(
        QStringLiteral("控件风格：%1").arg(on ? winKey : lv::fusionStyleKey()), 2000);
}

void MainWindow::refreshThemedUi()
{
    // 视图的行缓存里存了具体颜色，主题重建后必须整体重建
    m_view->invalidateAll();
    m_findBar->refreshStyle();
    m_filterDlg->refreshTheme();
    update();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    saveProfiles();
}
