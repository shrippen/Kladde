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
#elif !defined(Q_OS_ANDROID)
#include "keychainstore.h"
#endif

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <QtQml/qqml.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("shrippen"));
#ifdef KLADDE_DEMO
    // Own settings file: the demo never touches the real server address.
    QGuiApplication::setApplicationName(QStringLiteral("kladde-demo"));
#else
    QGuiApplication::setApplicationName(QStringLiteral("kladde"));
#endif
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Kladde"));

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

#if defined(KLADDE_DEMO) || defined(Q_OS_ANDROID)
    // Demo: nothing goes to the keychain. Android: the Keystore store comes with the
    // Android build (JNI into android/sync).
    kladde::MemorySecretStore secrets;
#else
    KeychainStore secrets;
#endif
    SyncBridge sync;
    Account account(&secrets, &sync);
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
                  status.value(QLatin1String("uploaded")).toInt(), status.value(QLatin1String("pending")).toInt());
    account.setServer(demo.server());
#endif

    const char *uri = "Kladde.Core";
    const QString why = QStringLiteral("provided by the app");
    qmlRegisterUncreatableType<Account>(uri, 1, 0, "Account", why);
    qmlRegisterUncreatableType<Entry>(uri, 1, 0, "Entry", why);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("account"), &account);
    engine.rootContext()->setContextProperty(QStringLiteral("dashboard"), &dashboard);
    engine.rootContext()->setContextProperty(QStringLiteral("history"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("entry"), &entry);
    engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kladde/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

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
            });
            QTimer::singleShot(delay + 1500, &app, [window, shot, shotDir] {
                window->grabWindow().save(QDir(shotDir).filePath(shot.value(QLatin1String("name")).toString() + QStringLiteral(".png")));
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
