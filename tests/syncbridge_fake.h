#pragma once

#include <QByteArray>
#include <QString>

// What the app asked of the background sync (tests/syncbridge_fake.cpp, the platform side
// of SyncBridge in tests): the sync is available and remembers the last configuration.
namespace FakeSync {

inline int forgets = 0;
inline QString server;
inline QString device;
// What KladdeSync's statusJson and permissionsJson answer; refresh() reads them.
inline QByteArray status;
inline QByteArray permissions;
inline int refreshes = 0;
inline int syncs = 0;
// Queues of an earlier session dropped at configure (a fresh sign-in).
inline int queueDrops = 0;

} // namespace FakeSync
