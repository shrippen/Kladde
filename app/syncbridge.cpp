#include "syncbridge.h"

// The Android side (JNI into de.arianw.kladde.sync.KladdeSync) comes with the Android
// build; until then every platform takes this path and reports the sync as unavailable.

SyncBridge::SyncBridge(QObject *parent)
    : QObject(parent)
{
}

bool SyncBridge::available() const
{
    return m_demo;
}

#ifdef KLADDE_DEMO
void SyncBridge::showDemo(const QDateTime &lastRun, const QDateTime &lastSuccess, int uploaded, int pending)
{
    m_demo = true;
    m_configured = true;
    m_permitted = true;
    m_lastRun = lastRun;
    m_lastSuccess = lastSuccess;
    m_uploaded = uploaded;
    m_pending = pending;
    Q_EMIT changed();
}
#endif

QString SyncBridge::reason() const
{
#ifdef Q_OS_ANDROID
    return tr("This build has no Health Connect sync yet.");
#else
    return tr("Health Connect exists only on Android. On Linux the watch will connect directly (planned).");
#endif
}

void SyncBridge::setIntervalMinutes(int minutes)
{
    minutes = std::max(15, minutes); // WorkManager's shortest period
    if (minutes == m_interval)
        return;
    m_interval = minutes;
    Q_EMIT changed();
}

void SyncBridge::setBackfillDays(int days)
{
    days = std::clamp(days, 1, 365);
    if (days == m_backfill)
        return;
    m_backfill = days;
    Q_EMIT changed();
}

void SyncBridge::configure(const QString &, const QString &)
{
}

void SyncBridge::forget()
{
}

void SyncBridge::requestPermissions()
{
}

void SyncBridge::syncNow()
{
}

void SyncBridge::resync()
{
}

void SyncBridge::refresh()
{
}
