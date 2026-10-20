#include "common.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QLocale>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LgViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("LgViewer"));

    // 版本号由 CMake 注入；dev 构建在版本号后加 -dev，便于区分本地调试包
#ifdef LG_VERSION
    QString appVersion = QStringLiteral(LG_VERSION);
#else
    QString appVersion = QStringLiteral("0.1.0");
#endif
#ifdef LG_BUILD_MARK
    if (qstrcmp(LG_BUILD_MARK, "release") != 0)
        appVersion += QStringLiteral("-") + QStringLiteral(LG_BUILD_MARK);
#endif
    QCoreApplication::setApplicationVersion(appVersion);

    // --version：打印版本后退出（打包脚本 / CI 用）
    if (app.arguments().contains(QStringLiteral("--version"))) {
        std::printf("%s\n", qPrintable(appVersion));
        return 0;
    }

    // 任务栏 / 窗口图标
    app.setWindowIcon(QIcon(QStringLiteral(":/app/app_icon.png")));

    QLocale::setDefault(QLocale::system());

    // 用 Fusion 保证三个平台的观感一致，同时保留系统配色
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    lv::refreshTheme();

    MainWindow window;
    window.show();

    QString fileToOpen;
    int profileIndex = 0;
    bool openTester = false;
    bool openQtAbout = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a.startsWith(QStringLiteral("--shot=")))
            window.setAutoScreenshot(a.mid(7));
        else if (a == QStringLiteral("--bench"))
            window.setBenchMode(true);
        else if (a == QStringLiteral("--tester"))
            openTester = true;
        else if (a == QStringLiteral("--qtabout"))
            openQtAbout = true;
        else if (a == QStringLiteral("--prompt"))
            window.setShotPromptDialog(true);
        else if (a == QStringLiteral("--filter"))
            window.setShotFilterDialog(true);
        else if (a == QStringLiteral("--scrollcheck"))
            window.setScrollCheck(true);
        else if (a.startsWith(QStringLiteral("--profile=")))
            profileIndex = a.mid(10).toInt();
        else if (a.startsWith(QStringLiteral("--findword=")))
            window.setShotFind(a.mid(11), true, false);
        else if (a.startsWith(QStringLiteral("--findcase=")))
            window.setShotFind(a.mid(11), false, true);
        else if (a.startsWith(QStringLiteral("--find=")))
            window.setShotFind(a.mid(7), false, false);
        else if (QFileInfo::exists(a))
            fileToOpen = a;
    }
    if (profileIndex > 0)
        window.selectProfile(profileIndex);
    if (!fileToOpen.isEmpty())
        window.openPath(fileToOpen);
    if (openTester)
        window.showTester();
    if (openQtAbout)
        QTimer::singleShot(400, &window, [&window] { window.showLicenseInfo(); });

    return app.exec();
}
