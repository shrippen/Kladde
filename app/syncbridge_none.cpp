#include "syncbridge.h"

#include <QSysInfo>

// No Health Connect here: the sync reports itself unavailable (Plasma Mobile gets its own
// source, plan phase 3). The demo build shows the Android status from the demo world.

void SyncBridge::platformInit()
{
}

bool SyncBridge::available() const
{
    return m_demo;
}

QString SyncBridge::reason() const
{
    return tr("Health Connect exists only on Android. On Linux the watch will connect directly (planned).");
}

QString SyncBridge::deviceName() const
{
    return QSysInfo::machineHostName();
}

void SyncBridge::storeInterval()
{
}

void SyncBridge::storeBackfill()
{
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
