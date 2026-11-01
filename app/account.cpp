#include "account.h"
#include "errors.h"
#include "syncbridge.h"

#include <QDesktopServices>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

using kladde::ApiReply;
using kladde::Session;

namespace {

const auto ServerKey = QStringLiteral("server");
const auto DeviceKey = QStringLiteral("deviceId");
// The scheme HealthLog sends the SSO sign-in back to (fixed on the server).
const auto CallbackScheme = QStringLiteral("healthlog");

// Where the browser's return reaches the app: Android routes healthlog:// to it (manifest).
// Linux has no handler yet.
#ifdef Q_OS_ANDROID
constexpr bool SsoCallback = true;
#else
constexpr bool SsoCallback = false;
#endif

} // namespace

Account::Account(kladde::SecretStore *secrets, SyncBridge *sync, QObject *parent)
    : QObject(parent)
    , m_session(secrets)
    , m_sync(sync)
{
    QSettings settings;
    QString device = settings.value(DeviceKey).toString();
    if (device.isEmpty()) {
        device = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(DeviceKey, device);
    }
    m_session.setDeviceId(device);
    m_server = settings.value(ServerKey).toString();
    m_session.setServer(QUrl(m_server));

    connect(&m_session, &Session::stateChanged, this, [this] {
        if (m_session.state() == Session::State::SignedIn)
            signedIn();
        Q_EMIT stateChanged();
    });
    connect(&m_session, &Session::signedOutByServer, m_sync, &SyncBridge::forget);

    if (SsoCallback) {
        QDesktopServices::setUrlHandler(CallbackScheme, this, "finishSso");
    }
}

QString Account::error() const
{
    const int status = m_session.lastErrorStatus();
    const QString code = m_session.lastError();
    if (m_signingIn)
        return Errors::describeSignIn(status, code, m_session.state() == Session::State::NeedsMfa);
    return Errors::describe(status, code);
}

Account::State Account::state() const
{
    return static_cast<State>(m_session.state());
}

void Account::setServer(const QString &server)
{
    QString s = server.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (!s.isEmpty() && !s.contains(QLatin1String("://")))
        s.prepend(QLatin1String("https://"));
    if (s == m_server)
        return;
    m_server = s;
    QSettings().setValue(ServerKey, s);
    m_session.setServer(QUrl(s));
    m_ssoLabel.clear();
    setProbe(Probe::Unknown);
    Q_EMIT serverChanged();
}

void Account::restore()
{
    m_signingIn = false;
    if (!m_server.isEmpty())
        m_session.restore();
}

void Account::setProbe(Probe probe, const QString &version, const QString &error)
{
    m_probe = probe;
    m_version = version;
    m_probeError = error;
    Q_EMIT probeChanged();
}

void Account::checkServer()
{
    if (!Session::acceptableServer(QUrl(m_server))) {
        setProbe(Probe::Unreachable, {}, tr("The address must start with https://"));
        return;
    }
    setProbe(Probe::Checking);
    // No token needed; a HealthLog answers with its version.
    m_session.get(QStringLiteral("/api/version"), [this, server = m_server](const ApiReply &r) {
        if (server != m_server)
            return;
        const QString version = r.data().toObject().value(QLatin1String("version")).toString();
        if (r.ok() && !version.isEmpty()) {
            setProbe(Probe::Reachable, version);
            checkSso();
        } else {
            setProbe(Probe::Unreachable, {}, r.ok() ? tr("No HealthLog at this address") : Errors::describe(r));
        }
    });
}

void Account::checkSso()
{
    m_ssoLabel.clear();
    if (!SsoCallback) {
        return;
    }
    // e.g. {"enabled": true, "buttonLabel": "authentik"}
    m_session.get(QStringLiteral("/api/auth/oidc/status"), [this, server = m_server](const ApiReply &r) {
        if (server != m_server || !r.ok()) {
            return;
        }
        const QJsonObject sso = r.data().toObject();
        if (!sso.value(QLatin1String("enabled")).toBool()) {
            return;
        }
        m_ssoLabel = sso.value(QLatin1String("buttonLabel")).toString();
        if (m_ssoLabel.isEmpty()) {
            m_ssoLabel = QStringLiteral("SSO");
        }
        Q_EMIT probeChanged();
    });
}

void Account::login(const QString &email, const QString &password)
{
    m_signingIn = true;
    m_session.login(email.trimmed(), password);
}

void Account::loginSso()
{
    m_signingIn = true;
    QDesktopServices::openUrl(m_session.startSso());
}

void Account::finishSso(const QUrl &callback)
{
    // Qt on Android calls this on Android's UI thread; the session lives on ours.
    QMetaObject::invokeMethod(this, [this, callback] { m_session.finishSso(callback); }, Qt::QueuedConnection);
}

void Account::verifyMfa(const QString &code, bool recoveryCode)
{
    m_session.verifyMfa(code, recoveryCode ? QStringLiteral("recovery") : QStringLiteral("totp"));
}

void Account::logout()
{
    m_signingIn = false;
    // The session first: it reads the refresh token the vault holds and revokes it, then
    // empties the vault (remove, endSession); forget only stops the work and drops the queue.
    m_session.logout();
    m_sync->forget();
}

void Account::signedIn()
{
    // From here on a 401 means the session ended, not a wrong password.
    const SyncBridge::Start start = m_signingIn ? SyncBridge::Start::Fresh : SyncBridge::Start::Resume;
    m_signingIn = false;
    if (!m_sync->available()) {
        return;
    }

    // The sync uploads with this very session (owner 1.11.2026: no token of its own); the
    // vault keeps it. It needs where and as which device: HealthLog renews a session only
    // for the device it was signed in from.
    m_sync->configure(m_server, m_session.deviceId(), start);
}
