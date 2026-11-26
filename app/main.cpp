#include "account.h"
#include "dashboard.h"
#include "entry.h"
#include "history.h"
#include "syncbridge.h"

#ifdef KLADDE_DEMO
#include "demo/demostart.h"
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

// What QML gets as types; Session and EntryKind are the core's enums, passed on as they are.
static void registerTypes()
{
    const char *uri = "Kladde.Core";
    const QString why = QStringLiteral("provided by the app");
    qmlRegisterUncreatableType<Account>(uri, 1, 0, "Account", why);
    qmlRegisterUncreatableType<Entry>(uri, 1, 0, "Entry", why);
    qmlRegisterUncreatableMetaObject(kladde::Session::staticMetaObject, uri, 1, 0, "Session", why);
    qmlRegisterUncreatableMetaObject(kladde::staticMetaObject, uri, 1, 0, "EntryKind", why);
    qmlRegisterUncreatableType<SyncBridge>(uri, 1, 0, "SyncBridge", why);
}

// Names, organisation and icon theme: what the platform and the build need before any window.
static void setupIdentity()
{
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
}

// Makes the C++ objects QML's only way to the core.
static void exposeToQml(QQmlApplicationEngine &engine, Account &account, Dashboard &dashboard, History &history,
                        Entry &entry, SyncBridge &sync)
{
    registerTypes();

    engine.rootContext()->setContextProperty(QStringLiteral("account"), &account);
    engine.rootContext()->setContextProperty(QStringLiteral("dashboard"), &dashboard);
    engine.rootContext()->setContextProperty(QStringLiteral("history"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("entry"), &entry);
    engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
}

int main(int argc, char **argv)
{
#ifdef Q_OS_ANDROID
    // Android has no Kirigami platform theme: Material Dark, coloured by Kante (Main.qml).
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
#endif
    QGuiApplication app(argc, argv);
    setupIdentity();

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

    QQmlApplicationEngine engine;
    exposeToQml(engine, account, dashboard, history, entry, sync);
#ifdef Q_OS_ANDROID
    // Kirigami's style modules sit in the resources there.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
#ifdef KLADDE_DEMO
    startDemo(app, engine, account, history, sync);
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kladde/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef Q_OS_ANDROID
    app.installEventFilter(new AndroidBackFilter(&engine));
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
