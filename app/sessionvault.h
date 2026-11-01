#pragma once

#include "secretstore.h"
#include "serialqueue.h"
#include "session.h"

#include <QObject>

// Android: the session's tokens live in the sync module's vault (KladdeSync, TokenVault),
// not in a keychain of the app's own. The vault renews them for the app and the background
// job alike, one renewal at a time: HealthLog revokes every token of the user when a spent
// refresh token comes back.
//
//   Session ──write/read/remove──▶ SessionVault ──JNI──▶ TokenVault (Keystore)
//   Session ──renew(stale)───────▶ SessionVault ──JNI──▶ TokenVault.current ◀── SyncWorker
class SessionVault : public QObject, public kladde::SecretStore
{
    Q_OBJECT
public:
    explicit SessionVault(QObject *parent = nullptr);

    // One session per vault: `key` (the server) is remembered next to it, another one finds
    // nothing.
    void read(const QString &key, ReadDone done) override;
    void write(const QString &key, const QString &value, Done done) override;
    void remove(const QString &key, Done done) override;

    // Session::Renewer: asks the vault off the UI thread, answers on this object's thread.
    void renew(const QString &stale, kladde::Session::Callback done);

private:
    // Every call to the vault: off the UI thread (its lock may wait for the background job's
    // renewal), in order (sign-out reads the kept token before it removes it).
    SerialQueue m_calls;
};
