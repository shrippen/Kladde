#include "syncbridge.h"
#include "syncbridge_fake.h"

// SyncBridge's platform side for tests: available like on Android, records instead of JNI.

void SyncBridge::platformInit()
{
}

bool SyncBridge::available() const
{
    return true;
}

QString SyncBridge::reason() const
{
    return {};
}

QString SyncBridge::deviceName() const
{
    return QStringLiteral("Test");
}

void SyncBridge::storeInterval()
{
}

void SyncBridge::storeBackfill()
{
}

void SyncBridge::configure(const QString &server, const QString &token)
{
    FakeSync::server = server;
    FakeSync::token = token;
    m_configured = true;
    Q_EMIT changed();
}

void SyncBridge::forget()
{
    ++FakeSync::forgets;
    FakeSync::server.clear();
    FakeSync::token.clear();
    m_configured = false;
    Q_EMIT changed();
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
