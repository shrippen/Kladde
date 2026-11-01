#pragma once

#include "session.h"

#include <QObject>
#include <QStringList>

class SyncBridge;

// The HealthLog account as QML sees it: where the server is, whether it answers, the
// sign-in (password, second factor) and the sign-out. Tokens never reach QML.
//
// After signing in on a platform with the background sync, the account mints its own
// measurements:write token (POST /api/tokens/measurements) and hands it to the sync: the
// background job then needs neither the password nor the session (Lastenheft LF-02).
class Account : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString username READ username NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool recoveryAllowed READ recoveryAllowed NOTIFY stateChanged)
    Q_PROPERTY(Probe probe READ probe NOTIFY probeChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY probeChanged)
    Q_PROPERTY(QString probeError READ probeError NOTIFY probeChanged)
public:
    enum class State { SignedOut, Restoring, SigningIn, NeedsMfa, SignedIn };
    Q_ENUM(State)
    enum class Probe { Unknown, Checking, Reachable, Unreachable };
    Q_ENUM(Probe)

    Account(kladde::SecretStore *secrets, SyncBridge *sync, QObject *parent = nullptr);

    kladde::Session *session() { return &m_session; }

    QString server() const { return m_server; }
    void setServer(const QString &server);
    State state() const;
    QString username() const { return m_session.username(); }
    QString error() const { return m_session.lastError(); }
    bool recoveryAllowed() const { return m_session.mfaMethods().contains(QLatin1String("recovery")); }
    Probe probe() const { return m_probe; }
    QString serverVersion() const { return m_version; }
    QString probeError() const { return m_probeError; }

    // Keeps the session of the last start, if the keychain still has it.
    void restore();

    Q_INVOKABLE void checkServer();
    Q_INVOKABLE void login(const QString &email, const QString &password);
    Q_INVOKABLE void verifyMfa(const QString &code, bool recoveryCode);
    Q_INVOKABLE void logout();

Q_SIGNALS:
    void serverChanged();
    void stateChanged();
    void probeChanged();

private:
    void setProbe(Probe probe, const QString &version = {}, const QString &error = {});
    void mintSyncToken();

    kladde::Session m_session;
    SyncBridge *m_sync;
    QString m_server;
    Probe m_probe = Probe::Unknown;
    QString m_version;
    QString m_probeError;
};
