#include "mainwindow.h"
#include "store.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("todomd"));
    QApplication::setApplicationName(QStringLiteral("todomd"));
    QApplication::setApplicationVersion(QStringLiteral(TODOMD_VERSION));

    theme::init();

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Manage markdown tasks found recursively in directories."));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("directories"),
                              QStringLiteral("Directories to add to the remembered list."), QStringLiteral("[dir...]"));
    QCommandLineOption bench(QStringLiteral("bench"),
                             QStringLiteral("Load only the given directories, print timing and exit."));
    cli.addOption(bench);
    QCommandLineOption shot(QStringLiteral("screenshot"),
                            QStringLiteral("Load only the given directories, save a PNG of the window and exit "
                                           "(works with QT_QPA_PLATFORM=offscreen)."),
                            QStringLiteral("file"));
    cli.addOption(shot);
    QCommandLineOption tab(QStringLiteral("tab"), QStringLiteral("Tab to show: tasks, agenda or templates."), QStringLiteral("name"));
    cli.addOption(tab);
    QCommandLineOption themeOpt(QStringLiteral("theme"),
                                QStringLiteral("Theme for this run only: light, soft, dark, system, clean, midnight or paper."),
                                QStringLiteral("name"));
    cli.addOption(themeOpt);
    cli.process(app);

    theme::apply(theme::fromString(cli.isSet(themeOpt)
                                       ? cli.value(themeOpt)
                                       : QSettings().value(QStringLiteral("view/theme"), QStringLiteral("light")).toString()));

    const QStringList given = cli.positionalArguments();
    for (const QString &d : given) {
        if (!QDir(d).exists()) {
            QTextStream(stderr) << "todomd: no such directory: " << d << '\n';
            return 1;
        }
    }
    const bool oneShot = cli.isSet(bench) || cli.isSet(shot);

    QSettings settings;
    QStringList dirs = settings.value(QStringLiteral("directories")).toStringList();
    if (!settings.contains(QStringLiteral("directories"))) {
        // Migrate the single-directory setting of earlier versions.
        const QString last = settings.value(QStringLiteral("lastDirectory")).toString();
        if (!last.isEmpty())
            dirs << last;
        settings.remove(QStringLiteral("lastDirectory"));
    }
    if (oneShot)
        dirs = given;
    else
        dirs += given;

    TaskStore store;
    MainWindow window(&store);
    if (cli.isSet(tab))
        {
            const QString t = cli.value(tab);
            window.showTab(t == QLatin1String("agenda") ? 1 : t == QLatin1String("templates") ? 2 : 0);
        }

    if (cli.isSet(bench)) {
        QElapsedTimer total;
        total.start();
        QObject::connect(&store, &TaskStore::loadFinished, &app, [&](int files, int tasks, qint64 ms) {
            QTextStream(stdout) << tasks << " tasks in " << files << " files, scanned+parsed in " << ms
                                << " ms, models+views " << total.elapsed() - ms << " ms";
            // Next event loop pass: after the window has laid out and painted.
            QTimer::singleShot(0, &app, [&] {
                QTextStream(stdout) << ", first paint at " << total.elapsed() << " ms total\n";
                app.quit();
            });
        });
        window.show();
        window.setDirectories(dirs, false);
        return app.exec();
    }

    if (cli.isSet(shot)) {
        QObject::connect(&store, &TaskStore::loadFinished, &app, [&] {
            QTimer::singleShot(50, &app, [&] {
                window.grab().save(cli.value(shot));
                app.quit();
            });
        });
        window.show();
        window.setDirectories(dirs, false);
        return app.exec();
    }

    window.show();
    window.setDirectories(dirs);
    if (dirs.isEmpty())
        window.chooseDirectory();
    return app.exec();
}
