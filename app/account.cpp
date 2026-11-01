#include "account.h"
#include "errors.h"
#include "syncbridge.h"

#include <QJsonObject>
#include <QSettings>
#include <QUuid>

using kladde::ApiReply;
using kladde::Session;

namespace {

const auto ServerKey = QStringLiteral("server");
const auto DeviceKey = QStringLiteral("deviceId");

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
        if (r.ok() && !version.isEmpty())
            setProbe(Probe::Reachable, version);
        else
            setProbe(Probe::Unreachable, {}, r.ok() ? tr("No HealthLog at this address") : Errors::describe(r));
    });
}

void Account::login(const QString &email, const QString &password)
{
    m_signingIn = true;
    m_session.login(email.trimmed(), password);
}

void Account::verifyMfa(const QString &code, bool recoveryCode)
{
    m_session.verifyMfa(code, recoveryCode ? QStringLiteral("recovery") : QStringLiteral("totp"));
}

void Account::logout()
{
    m_signingIn = false;
    m_sync->forget();
    m_session.logout();
}

void Account::signedIn()
{
    // From here on a 401 means the session ended, not a wrong password.
    const bool fresh = m_signingIn;
    m_signingIn = false;
    if (!m_sync->available() || m_minting == m_session.generation()) {
        return;
    }

    // A sign-in with password or code gets a sync token of its own: the sync may still
    // carry one of an earlier sign-in (another account or server) that no sign-out forgot,
    // e.g. when the keychain lost the session.
    if (fresh && m_sync->configured()) {
        m_sync->forget();
    }
    if (!m_sync->configured()) {
        mintSyncToken();
    }
}

void Account::mintSyncToken()
{
    m_minting = m_session.generation();
    // A token of its own for the background sync, named after the device, so the user can
    // see and revoke it in HealthLog's settings. No expiry: the sync runs unattended.
    const QJsonObject body{{QStringLiteral("name"), QStringLiteral("Kladde (%1)").arg(m_sync->deviceName())}};
    m_session.post(QStringLiteral("/api/tokens/measurements"), body,
                   [this, g = m_session.generation()](const ApiReply &r) {
        // Signed out meanwhile: the sync must not get a token of the old session.
        if (g != m_session.generation())
            return;
        m_minting = 0;
        const QString token = r.data().toObject().value(QLatin1String("token")).toString();
        if (r.ok() && !token.isEmpty())
            m_sync->configure(m_server, token);
    });
}
