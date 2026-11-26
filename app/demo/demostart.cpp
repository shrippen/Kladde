#include "demostart.h"

#include "../account.h"
#include "demobackend.h"
#include "../history.h"
#include "../syncbridge.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

namespace {

constexpr int FirstShotMs = 2000;
constexpr int GrabAfterMs = 1500;
constexpr int ShotStepMs = 2000;

void loadWorld(Account &account, SyncBridge &sync)
{
    QFile worldFile(QStringLiteral(":/demo/world.json"));
    if (!worldFile.open(QIODevice::ReadOnly)) {
        qFatal("demo world missing");
    }

    // Lives as long as the app: the transport below keeps answering from it.
    static DemoBackend demo(QJsonDocument::fromJson(worldFile.readAll()).object());
    account.session()->setTransport([](const QByteArray &verb, const QString &path, const QByteArray &body,
                                       const kladde::Session::Callback &done) { demo.answer(verb, path, body, done); });

    const QJsonObject status = demo.syncStatus();
    sync.showDemo(QDateTime::fromString(status.value(QLatin1String("lastRun")).toString(), Qt::ISODateWithMs),
                  QDateTime::fromString(status.value(QLatin1String("lastSuccess")).toString(), Qt::ISODateWithMs),
                  status.value(QLatin1String("uploaded")).toInt(), status.value(QLatin1String("pending")).toInt(),
                  QJsonDocument(status.value(QLatin1String("apps")).toObject()).toJson(QJsonDocument::Compact));
    account.setServer(demo.server());
}

// Every shot opens its tab (and metric), waits and saves <name>.png, then the app quits.
void runShots(QGuiApplication &app, QQuickWindow *window, History &history, const QJsonArray &plan,
              const QString &shotDir)
{
    const QStringList tabs{QStringLiteral("home"), QStringLiteral("log"), QStringLiteral("history"), QStringLiteral("more")};
    int delay = FirstShotMs;

    for (const QJsonValue &v : plan) {
        const QJsonObject shot = v.toObject();
        QTimer::singleShot(delay, &app, [window, shot, tabs, &history] {
            if (shot.contains(QLatin1String("kind"))) {
                history.setKind(shot.value(QLatin1String("kind")).toString());
            }
            if (shot.contains(QLatin1String("days"))) {
                history.setDays(shot.value(QLatin1String("days")).toInt());
            }
            window->setProperty("tab", int(tabs.indexOf(shot.value(QLatin1String("page")).toString())));

            // "viewport" [w, h]: this shot's own window size (a whole page, e.g. More).
            const QJsonArray size = shot.value(QLatin1String("viewport")).toArray();
            if (size.size() == 2) {
                window->resize(size[0].toInt(), size[1].toInt());
            }
        });

        QTimer::singleShot(delay + GrabAfterMs, &app, [window, shot, shotDir, size = window->size()] {
            window->grabWindow().save(QDir(shotDir).filePath(shot.value(QLatin1String("name")).toString() + QStringLiteral(".png")));
            window->resize(size);
        });
        delay += ShotStepMs;
    }
    QTimer::singleShot(delay, &app, &QCoreApplication::quit);
}

} // namespace

void startDemo(QGuiApplication &app, QQmlApplicationEngine &engine, Account &account, History &history,
               SyncBridge &sync)
{
    loadWorld(account, sync);

    // Signed in from the start.
    account.login(QStringLiteral("mara"), QStringLiteral("demo"));

    const QJsonArray plan = QJsonDocument::fromJson(qgetenv("SHOT_PLAN")).array();
    const QString shotDir = qEnvironmentVariable("SHOT_DIR");
    if (plan.isEmpty() || shotDir.isEmpty()) {
        return;
    }

    // The window exists only once Main.qml is loaded.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [&app, &history, plan, shotDir](QObject *root) {
                         // A failed load sends no window; main() then exits with 1.
                         auto *window = qobject_cast<QQuickWindow *>(root);
                         if (!window) {
                             return;
                         }
                         runShots(app, window, history, plan, shotDir);
                     },
                     Qt::SingleShotConnection);
}
