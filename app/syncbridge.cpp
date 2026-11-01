#include "syncbridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// How often the status is read again while a sync runs.
constexpr int WatchMs = 2000;

} // namespace

// What every platform shares; the platform side is in syncbridge_android.cpp (JNI into
// de.arianw.kladde.sync.KladdeSync) and syncbridge_none.cpp.

SyncBridge::SyncBridge(QObject *parent)
    : QObject(parent)
{
    m_watch.setSingleShot(true);
    m_watch.setInterval(WatchMs);
    connect(&m_watch, &QTimer::timeout, this, &SyncBridge::refresh);
    platformInit();
}

#ifdef KLADDE_DEMO
void SyncBridge::showDemo(const QDateTime &lastRun, const QDateTime &lastSuccess, int uploaded, int pending)
{
    m_demo = true;
    m_configured = true;
    m_permitted = true;
    m_complete = true;
    m_lastRun = lastRun;
    m_lastSuccess = lastSuccess;
    m_uploaded = uploaded;
    m_pending = pending;
    Q_EMIT changed();
}
#endif

void SyncBridge::setIntervalMinutes(int minutes)
{
    minutes = std::max(15, minutes); // WorkManager's shortest period
    if (minutes == m_interval)
        return;
    m_interval = minutes;
    if (!m_demo)
        storeInterval();
    Q_EMIT changed();
}

void SyncBridge::setBackfillDays(int days)
{
    days = std::clamp(days, 1, 365);
    if (days == m_backfill)
        return;
    m_backfill = days;
    if (!m_demo)
        storeBackfill();
    Q_EMIT changed();
}

void SyncBridge::syncNow()
{
    startSync();
    refresh();
}

void SyncBridge::applyStatus(const QByteArray &status, const QByteArray &permissions)
{
    const QJsonObject s = QJsonDocument::fromJson(status).object();
    const QJsonObject p = QJsonDocument::fromJson(permissions).object();
    m_lastRun = QDateTime::fromString(s.value(QLatin1String("lastRun")).toString(), Qt::ISODateWithMs);
    m_lastSuccess = QDateTime::fromString(s.value(QLatin1String("lastSuccess")).toString(), Qt::ISODateWithMs);
    m_lastError = s.value(QLatin1String("lastError")).toString();
    m_uploaded = s.value(QLatin1String("uploaded")).toInt();
    m_pending = s.value(QLatin1String("pending")).toInt();
    m_running = s.value(QLatin1String("running")).toBool();
    m_silent.clear();
    for (const QJsonValue &k : s.value(QLatin1String("kinds")).toArray()) {
        if (k.toObject().value(QLatin1String("silent")).toBool())
            m_silent << k.toObject().value(QLatin1String("kind")).toString();
    }
    // The required kinds or nothing; an optional kind left out is read once allowed.
    m_permitted = p.value(QLatin1String("available")).toBool()
        && p.value(QLatin1String("missing")).toArray().isEmpty();
    m_complete = m_permitted && p.value(QLatin1String("optional")).toArray().isEmpty();

    // The background job writes its status into its own database and tells no one: while it
    // runs, read again until it is done.
    if (m_running) {
        m_watch.start();
    }
    Q_EMIT changed();
}
