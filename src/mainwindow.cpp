#include "mainwindow.h"

#include "common.h"
#include "filterdialog.h"
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
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cstdio>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {

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
    m_view = new LogView(this);
    setCentralWidget(m_view);

    // 筛选项在独立窗口里，主界面只留日志视图
    m_filterDlg = new FilterDialog(this);
    m_filterDlg->setDocument(nullptr);

    buildUi();
    buildMenus();

    connect(m_doc, &LogDocument::loadStarted, this, [this](const QString &) {
        m_reloading = true;
        m_progress->show();
        m_progress->setValue(0);
    });
    connect(m_doc, &LogDocument::loadProgress, this, &MainWindow::onLoadProgress);
    connect(m_doc, &LogDocument::loadFinished, this, &MainWindow::onLoadFinished);
    connect(m_doc, &LogDocument::filterChanged, this, &MainWindow::onFilterChanged);
    connect(m_filterDlg, &FilterDialog::filterChanged, this, &MainWindow::applyFilterFromPanel);
    connect(m_view, &LogView::fontPointSizeChanged, this, [this](int pt) {
        statusBar()->showMessage(QStringLiteral("字号 %1").arg(pt), 1500);
    });

    loadProfiles();

    m_view->setDocument(m_doc);
    resize(1400, 860);
    updateStatus();
    m_progress->hide();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));

    QAction *openAction = toolbar->addAction(lv::icon(lv::Icon::Open), QStringLiteral("打开"));
    QAction *reloadAction = toolbar->addAction(lv::icon(lv::Icon::Reload), QStringLiteral("重新加载"));
    reloadAction->setToolTip(QStringLiteral("重新加载 (F5)"));
    toolbar->addSeparator();
    QAction *filterAction = toolbar->addAction(lv::icon(lv::Icon::Filter), QStringLiteral("筛选"));
    filterAction->setToolTip(QStringLiteral("筛选条件 (Ctrl+F)"));
    QAction *clearFilterAction = toolbar->addAction(lv::icon(lv::Icon::ClearFilter), QStringLiteral("清除筛选"));
    clearFilterAction->setToolTip(QStringLiteral("清除所有筛选条件 (Ctrl+Shift+F)"));
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
        m_doc->closeFile();
        m_filterDlg->setDocument(nullptr);
        m_view->setDocument(m_doc);
        m_activeSpec = FilterSpec();
        updateStatus();
    });

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
    QAction *actFind = editMenu->addAction(lv::icon(lv::Icon::Filter), QStringLiteral("筛选条件…"));
    actFind->setShortcut(QKeySequence::Find);
    connect(actFind, &QAction::triggered, this, [this] { showFilterDialog(); });

    QAction *actClearFilter = editMenu->addAction(lv::icon(lv::Icon::ClearFilter), QStringLiteral("清除筛选"));
    actClearFilter->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
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

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开日志文件"), QString(),
        QStringLiteral("日志文件 (*.txt *.log *.log.*);;所有文件 (*.*)"));
    if (!path.isEmpty())
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

    if (!m_shotPath.isEmpty()) {
        const QString path = m_shotPath;
        if (m_shotPrompt) {
            QTimer::singleShot(250, this, [this] { showRegexPrompt(); });
            return;
        }
        m_shotPath.clear();
        if (m_shotFilter) {
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

void MainWindow::setScrollCheck(bool enabled)
{
    m_scrollCheck = enabled;
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
    m_statusFilter->setToolTip(QStringLiteral("当前筛选条件，Ctrl+F 修改 / Ctrl+Shift+F 清除"));
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    saveProfiles();
}
