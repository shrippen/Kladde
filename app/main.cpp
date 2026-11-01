#include "connection.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("shrippen"));
    QGuiApplication::setApplicationName(QStringLiteral("kladde"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Kladde"));

    QCommandLineParser cli;
    cli.addHelpOption();
    // For CI and screenshots: render the first page, save it and quit.
    QCommandLineOption shotOpt(QStringLiteral("shot"),
                               QStringLiteral("Save a screenshot of the window and quit"),
                               QStringLiteral("png"));
    cli.addOption(shotOpt);
    cli.process(app);

    qmlRegisterUncreatableType<Connection>("Kladde.Core", 1, 0, "Connection",
                                           QStringLiteral("use the connection property"));
    Connection connection;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kladde/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (cli.isSet(shotOpt)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString path = cli.value(shotOpt);
        QTimer::singleShot(1500, &app, [window, path] {
            QCoreApplication::exit(window && window->grabWindow().save(path) ? 0 : 1);
        });
    }
    return app.exec();
}
