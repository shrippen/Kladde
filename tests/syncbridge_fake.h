#pragma once

#include <QString>

// What the app asked of the background sync (tests/syncbridge_fake.cpp, the platform side
// of SyncBridge in tests): the sync is available and remembers the last configuration.
namespace FakeSync {

inline int forgets = 0;
inline QString server;
inline QString token;

} // namespace FakeSync
