#pragma once

#include "session.h"

#include <QObject>
#include <QStringList>

class SyncBridge;

// The HealthLog account as QML sees it: where the server is, whether it answers, the
// sign-in (password, second factor, SSO) and the sign-out. The session's tokens never
// reach QML.
//
// The background sync uploads with this session (owner 1.11.2026: no token of its own). On
// Android the sync module keeps the session's tokens (SessionVault) and renews them for both.
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
    // The server's SSO button text ("authentik"), empty where there is no SSO sign-in.
    Q_PROPERTY(QString ssoLabel READ ssoLabel NOTIFY probeChanged)
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
    QString error() const;
    bool recoveryAllowed() const { return m_session.mfaMethods().contains(QLatin1String("recovery")); }
    Probe probe() const { return m_probe; }
    QString serverVersion() const { return m_version; }
    QString probeError() const { return m_probeError; }
    QString ssoLabel() const { return m_ssoLabel; }

    // Keeps the session of the last start, if the keychain still has it.
    void restore();

    Q_INVOKABLE void checkServer();
    Q_INVOKABLE void login(const QString &email, const QString &password);
    // Opens the server's SSO sign-in in the browser; it comes back on healthlog://.
    Q_INVOKABLE void loginSso();
    Q_INVOKABLE void verifyMfa(const QString &code, bool recoveryCode);
    Q_INVOKABLE void logout();

Q_SIGNALS:
    void serverChanged();
    void stateChanged();
    void probeChanged();

private Q_SLOTS:
    // QDesktopServices hands it the healthlog:// address the browser opened.
    void finishSso(const QUrl &callback);

private:
    void checkSso();
    void setProbe(Probe probe, const QString &version = {}, const QString &error = {});
    void signedIn();

    kladde::Session m_session;
    SyncBridge *m_sync;
    QString m_server;
    Probe m_probe = Probe::Unknown;
    QString m_version;
    QString m_probeError;
    QString m_ssoLabel;
    // A sign-in with password or code is under way: a 401 means wrong input, not an ended session.
    bool m_signingIn = false;
};
