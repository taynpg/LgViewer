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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LgViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("LgViewer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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
