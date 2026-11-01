#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

// The background sync as the app sees it (Lastenheft LF-10 … LF-24). On Android it drives
// the Kotlin module `android/sync` (KladdeSync) through JNI: Health Connect, WorkManager,
// the queue and the token in the Android Keystore all live there, so the sync runs without
// the app. Everywhere else there is no Health Connect: `available` is false and `reason`
// says why (Plasma Mobile gets its own source, phase 3).
class SyncBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString reason READ reason CONSTANT)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(bool permitted READ permitted NOTIFY changed)
    // Permitted, and every optional kind (weight, trainings …) allowed too.
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QDateTime lastRun READ lastRun NOTIFY changed)
    Q_PROPERTY(QDateTime lastSuccess READ lastSuccess NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int uploaded READ uploaded NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
    Q_PROPERTY(QStringList silentKinds READ silentKinds NOTIFY changed)
    Q_PROPERTY(int intervalMinutes READ intervalMinutes WRITE setIntervalMinutes NOTIFY changed)
    Q_PROPERTY(int backfillDays READ backfillDays WRITE setBackfillDays NOTIFY changed)
    // The apps that wrote sleep or trainings lately, in the user's order, each {app, label}:
    // where two apps saw the same night or training, the first one's goes to HealthLog.
    Q_PROPERTY(QVariantList sleepApps READ sleepApps NOTIFY appsChanged)
    Q_PROPERTY(QVariantList workoutApps READ workoutApps NOTIFY appsChanged)
public:
    // The kinds whose sessions overlap between apps (KladdeSync's Ranked).
    enum class Ranked { Sleep, Workouts };
    Q_ENUM(Ranked)

    // How configure treats what an earlier session left (queue, cursor, status).
    enum class Start {
        Resume, // the same session again (a restart)
        Fresh,  // a new sign-in: maybe another account or server, drop it
    };

    explicit SyncBridge(QObject *parent = nullptr);

    bool available() const;
    QString reason() const;
    bool configured() const { return m_configured; }
    bool permitted() const { return m_permitted; }
    bool complete() const { return m_complete; }
    // A sync is under way or about to start; the status follows it until it ends.
    bool running() const { return m_running; }
    QDateTime lastRun() const { return m_lastRun; }
    QDateTime lastSuccess() const { return m_lastSuccess; }
    QString lastError() const { return m_lastError; }
    int uploaded() const { return m_uploaded; }
    int pending() const { return m_pending; }
    QStringList silentKinds() const { return m_silent; }
    int intervalMinutes() const { return m_interval; }
    void setIntervalMinutes(int minutes);
    int backfillDays() const { return m_backfill; }
    void setBackfillDays(int days);
    QVariantList sleepApps() const { return m_apps.value(Ranked::Sleep); }
    QVariantList workoutApps() const { return m_apps.value(Ranked::Workouts); }

    // Where the session lives and the device it was signed in from; the background sync
    // uploads with the session the vault keeps (SessionVault).
    void configure(const QString &server, const QString &deviceId, Start start);
    // Stop the background work and drop its queue (sign-out); the session ends the vault.
    Q_INVOKABLE void forget();

    Q_INVOKABLE void requestPermissions();
    Q_INVOKABLE void syncNow();
    // Read the backfill window again (after a wrong time zone, a new phone …).
    Q_INVOKABLE void resync();
    // Read the status the background work left (on showing the page).
    Q_INVOKABLE void refresh();
    // Read which apps wrote sleep and trainings (asks Health Connect, on showing the order).
    Q_INVOKABLE void loadApps();
    // Move the app at `from` to `to` in the kind's order; the next run picks by it.
    Q_INVOKABLE void moveApp(Ranked kind, int from, int to);

#ifdef KLADDE_DEMO
    // The demo build shows the Android status from the demo world.
    void showDemo(const QDateTime &lastRun, const QDateTime &lastSuccess, int uploaded, int pending);
#endif

Q_SIGNALS:
    void changed();
    void appsChanged();

private:
    // Platform side (syncbridge_android.cpp, syncbridge_none.cpp).
    void platformInit();
    void storeInterval();
    void storeBackfill();
    void startSync();
    // Takes KladdeSync's statusJson and permissionsJson (see there).
    void applyStatus(const QByteArray &status, const QByteArray &permissions);
    void storeOrder(Ranked kind, const QStringList &apps);
    // The kind's name in KladdeSync (Ranked.key): "sleep", "workouts".
    static QString keyOf(Ranked kind);
    // Takes KladdeSync's appsJson (see there).
    void applyApps(const QByteArray &apps);

    bool m_demo = false;
    bool m_configured = false;
    bool m_permitted = false;
    bool m_complete = false;
    bool m_running = false;
    QDateTime m_lastRun;
    QDateTime m_lastSuccess;
    QString m_lastError;
    int m_uploaded = 0;
    int m_pending = 0;
    QStringList m_silent;
    QHash<Ranked, QVariantList> m_apps;
    int m_interval = 60;
    int m_backfill = 30;
    // Asks again while a sync runs (applyStatus).
    QTimer m_watch;
};
