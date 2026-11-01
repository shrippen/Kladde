#include "account.h"
#include "dashboard.h"
#include "entry.h"
#include "history.h"
#include "syncbridge.h"

#ifdef KLADDE_DEMO
#include "demo/demobackend.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#else
#include "keychainstore.h"
#ifdef Q_OS_ANDROID
#include "sessionvault.h"
#endif
#endif

#ifdef Q_OS_ANDROID
#include "platform/androidbackfilter.h"
#endif

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QtQml/qqml.h>

int main(int argc, char **argv)
{
#ifdef Q_OS_ANDROID
    // Android has no Kirigami platform theme: Material Dark, coloured by Kante (Main.qml).
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("shrippen"));
#ifdef KLADDE_DEMO
    // Own settings file: the demo never touches the real server address.
    QGuiApplication::setApplicationName(QStringLiteral("kladde-demo"));
#else
    QGuiApplication::setApplicationName(QStringLiteral("kladde"));
#endif
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Kladde"));
#ifdef Q_OS_ANDROID
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("breeze-dark"));
#elif defined(KLADDE_DEMO)
    // The screenshots run offscreen, without the desktop's icon theme; Breeze for the light page.
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths()
                               + QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"),
                                                           QStandardPaths::LocateDirectory));
    QIcon::setThemeName(QStringLiteral("breeze"));
#endif

    // German where the system speaks it, English otherwise.
    QTranslator translator;
#ifdef KLADDE_DEMO
    // The screenshot tool picks the language.
    if (const QString lang = qEnvironmentVariable("DEMO_LANG"); !lang.isEmpty())
        QLocale::setDefault(QLocale(lang));
#endif
    if (translator.load(QLocale(), QStringLiteral("kladde"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&translator);

    QCommandLineParser cli;
    cli.addHelpOption();
    // For CI and screenshots: render the first page, save it and quit.
    QCommandLineOption shotOpt(QStringLiteral("shot"),
                               QStringLiteral("Save a screenshot of the window and quit"),
                               QStringLiteral("png"));
    cli.addOption(shotOpt);
    cli.process(app);

#ifdef KLADDE_DEMO
    // Nothing goes to the keychain.
    kladde::MemorySecretStore secrets;
#elif defined(Q_OS_ANDROID)
    // The sync module keeps and renews the session for the app and the background job.
    SessionVault secrets;
#else
    KeychainStore secrets;
#endif
    SyncBridge sync;
    Account account(&secrets, &sync);
#if defined(Q_OS_ANDROID) && !defined(KLADDE_DEMO)
    account.session()->setRenewer([&secrets](const QString &stale, kladde::Session::Callback done) {
        secrets.renew(stale, std::move(done));
    });
#endif
    Dashboard dashboard(account.session());
    History history(account.session());
    Entry entry(account.session());

#ifdef KLADDE_DEMO
    QFile worldFile(QStringLiteral(":/demo/world.json"));
    if (!worldFile.open(QIODevice::ReadOnly))
        qFatal("demo world missing");
    DemoBackend demo(QJsonDocument::fromJson(worldFile.readAll()).object());
    account.session()->setTransport([&demo](const QByteArray &verb, const QString &path, const QByteArray &body,
                                            const kladde::Session::Callback &done) { demo.answer(verb, path, body, done); });
    const QJsonObject status = demo.syncStatus();
    sync.showDemo(QDateTime::fromString(status.value(QLatin1String("lastRun")).toString(), Qt::ISODateWithMs),
                  QDateTime::fromString(status.value(QLatin1String("lastSuccess")).toString(), Qt::ISODateWithMs),
                  status.value(QLatin1String("uploaded")).toInt(), status.value(QLatin1String("pending")).toInt(),
                  QJsonDocument(status.value(QLatin1String("apps")).toObject()).toJson(QJsonDocument::Compact));
    account.setServer(demo.server());
#endif

    const char *uri = "Kladde.Core";
    const QString why = QStringLiteral("provided by the app");
    qmlRegisterUncreatableType<Account>(uri, 1, 0, "Account", why);
    qmlRegisterUncreatableType<Entry>(uri, 1, 0, "Entry", why);
    qmlRegisterUncreatableType<SyncBridge>(uri, 1, 0, "SyncBridge", why);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("account"), &account);
    engine.rootContext()->setContextProperty(QStringLiteral("dashboard"), &dashboard);
    engine.rootContext()->setContextProperty(QStringLiteral("history"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("entry"), &entry);
    engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
#ifdef Q_OS_ANDROID
    // Kirigami's style modules sit in the resources there.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kladde/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef Q_OS_ANDROID
    app.installEventFilter(new AndroidBackFilter(&engine));
#endif

#ifdef KLADDE_DEMO
    // Signed in from the start; with SHOT_PLAN/SHOT_DIR (demo/tools/screenshots.py) every
    // shot opens its tab (and metric), waits and saves <name>.png, then the app quits.
    account.login(QStringLiteral("mara"), QStringLiteral("demo"));
    const QJsonArray plan = QJsonDocument::fromJson(qgetenv("SHOT_PLAN")).array();
    const QString shotDir = qEnvironmentVariable("SHOT_DIR");
    if (!plan.isEmpty() && !shotDir.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QStringList tabs{QStringLiteral("home"), QStringLiteral("log"), QStringLiteral("history"), QStringLiteral("more")};
        int delay = 2000;
        for (const QJsonValue &v : plan) {
            const QJsonObject shot = v.toObject();
            QTimer::singleShot(delay, &app, [window, shot, tabs, &history] {
                if (shot.contains(QLatin1String("kind")))
                    history.setKind(shot.value(QLatin1String("kind")).toString());
                if (shot.contains(QLatin1String("days")))
                    history.setDays(shot.value(QLatin1String("days")).toInt());
                window->setProperty("tab", int(tabs.indexOf(shot.value(QLatin1String("page")).toString())));
                // "viewport" [w, h]: this shot's own window size (a whole page, e.g. More).
                const QJsonArray size = shot.value(QLatin1String("viewport")).toArray();
                if (size.size() == 2) {
                    window->resize(size[0].toInt(), size[1].toInt());
                }
            });

            QTimer::singleShot(delay + 1500, &app, [window, shot, shotDir, size = window->size()] {
                window->grabWindow().save(QDir(shotDir).filePath(shot.value(QLatin1String("name")).toString() + QStringLiteral(".png")));
                window->resize(size);
            });
            delay += 2000;
        }
        QTimer::singleShot(delay, &app, &QCoreApplication::quit);
    }
#endif

    if (cli.isSet(shotOpt)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString path = cli.value(shotOpt);
        QTimer::singleShot(1500, &app, [window, path] {
            QCoreApplication::exit(window && window->grabWindow().save(path) ? 0 : 1);
        });
    } else {
#ifndef KLADDE_DEMO
        account.restore();
#endif
    }
    return app.exec();
}
