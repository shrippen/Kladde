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

void SyncBridge::storeInterval()
{
}

void SyncBridge::storeBackfill()
{
}

void SyncBridge::configure(const QString &server, const QString &deviceId, Start start)
{
    if (start == Start::Fresh) {
        ++FakeSync::queueDrops;
    }
    FakeSync::server = server;
    FakeSync::device = deviceId;
    m_configured = true;
    Q_EMIT changed();
}

void SyncBridge::forget()
{
    ++FakeSync::forgets;
    if (FakeSync::onForget) {
        FakeSync::onForget();
    }
    FakeSync::server.clear();
    FakeSync::device.clear();
    m_configured = false;
    Q_EMIT changed();
}

void SyncBridge::requestPermissions()
{
}

void SyncBridge::startSync()
{
    ++FakeSync::syncs;
}

void SyncBridge::resync()
{
}

void SyncBridge::refresh()
{
    ++FakeSync::refreshes;
    applyStatus(FakeSync::status, FakeSync::permissions);
}
