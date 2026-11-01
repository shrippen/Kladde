#include "session.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace kladde {

namespace {

constexpr int TransferTimeoutMs = 30000;
// Refresh this long before the access token runs out, not on the first 401.
constexpr qint64 RefreshMarginSecs = 60;

} // namespace

QString ApiReply::error() const
{
    const QString code = root.value(QLatin1String("meta")).toObject()
                             .value(QLatin1String("errorCode")).toString();
    if (!code.isEmpty())
        return code;
    const QJsonValue e = root.value(QLatin1String("error"));
    if (e.isString() && !e.toString().isEmpty())
        return e.toString();
    if (e.isObject() && !e.toObject().value(QLatin1String("message")).toString().isEmpty())
        return e.toObject().value(QLatin1String("message")).toString();
    if (status == 0)
        return transportError.isEmpty() ? QStringLiteral("no connection") : transportError;
    return QStringLiteral("HTTP %1").arg(status);
}

Session::Session(SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_nam(new QNetworkAccessManager(this))
{
}

bool Session::acceptableServer(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    if (url.scheme() == QLatin1String("https"))
        return true;
    return url.scheme() == QLatin1String("http") && QHostAddress(url.host()).isLoopback();
}

void Session::setServer(const QUrl &server)
{
    if (server == m_server)
        return;
    // Tokens belong to one server; a new address starts signed out.
    endGeneration();
    m_server = server;
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessExpires = {};
    setState(State::SignedOut);
}

QString Session::secretKey() const
{
    return QStringLiteral("refresh-token@") + m_server.toString(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash);
}

void Session::setState(State state, const ApiReply &failure)
{
    setState(state, failure.error(), failure.status);
}

void Session::setState(State state, const QString &error, int status)
{
    m_lastError = error;
    m_lastErrorStatus = error.isEmpty() ? -1 : status;
    m_state = state;
    Q_EMIT stateChanged();
}

void Session::raw(const QByteArray &verb, const QString &path, const QByteArray &body, bool withToken,
                  const QByteArray &idempotencyKey, Callback done)
{
    if (m_transport) {
        // Asynchronous like the network, so callers see the same order of events.
        QTimer::singleShot(0, this, [this, verb, path, body, done] { m_transport(verb, path, body, done); });
        return;
    }
    if (!acceptableServer(m_server)) {
        QTimer::singleShot(0, this, [done] {
            ApiReply r;
            r.transportError = QStringLiteral("the server address must start with https://");
            done(r);
        });
        return;
    }
    QNetworkRequest req(m_server.resolved(QUrl(path)));
    req.setTransferTimeout(TransferTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Client-Type", "native");
    if (!m_deviceId.isEmpty())
        req.setRawHeader("X-Device-Id", m_deviceId.toUtf8());
    if (withToken && !m_accessToken.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    if (!idempotencyKey.isEmpty())
        req.setRawHeader("Idempotency-Key", idempotencyKey);

    QNetworkReply *reply = verb == "GET" ? m_nam->get(req) : m_nam->sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        ApiReply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.root = QJsonDocument::fromJson(reply->readAll()).object();
        if (r.status == 0)
            r.transportError = reply->errorString();
        done(r);
    });
}

bool Session::acceptTokens(const QJsonObject &data)
{
    const QString access = data.value(QLatin1String("token")).toString();
    const QString refresh = data.value(QLatin1String("refreshToken")).toString();
    if (access.isEmpty() || refresh.isEmpty())
        return false;
    m_accessToken = access;
    m_accessExpires = QDateTime::fromString(data.value(QLatin1String("tokenExpiresAt")).toString(), Qt::ISODateWithMs);
    m_refreshToken = refresh;
    const QString name = data.value(QLatin1String("user")).toObject().value(QLatin1String("username")).toString();
    if (!name.isEmpty())
        m_username = name;
    // The old refresh token is spent now: the new one has to reach the keychain.
    m_secrets->write(secretKey(), refresh, [this](bool ok) {
        if (!ok) {
            m_lastError = QStringLiteral("kladde.keychain");
            m_lastErrorStatus = -1;
        }
    });
    return true;
}

void Session::dropTokens()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessExpires = {};
    m_secrets->remove(secretKey(), [](bool) {});
}

void Session::restore()
{
    setState(State::Restoring);
    m_secrets->read(secretKey(), [this](bool ok, const QString &value) {
        if (!ok || value.isEmpty()) {
            setState(State::SignedOut);
            return;
        }
        m_refreshToken = value;
        refresh();
    });
}

void Session::login(const QString &email, const QString &password)
{
    endGeneration();
    dropTokens();
    m_mfaTicket.clear();
    setState(State::SigningIn);
    const QJsonObject body{{QStringLiteral("email"), email}, {QStringLiteral("password"), password}};
    raw("POST", QStringLiteral("/api/auth/login"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [this, g = m_generation](const ApiReply &r) {
            if (g == m_generation)
                signIn(r);
        });
}

void Session::verifyMfa(const QString &code, const QString &method)
{
    if (m_state != State::NeedsMfa)
        return;
    setState(State::SigningIn);
    const QJsonObject body{{QStringLiteral("mfaTicket"), m_mfaTicket},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("code"), code.simplified().remove(QLatin1Char(' '))}};
    raw("POST", QStringLiteral("/api/auth/mfa/verify"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [this, g = m_generation](const ApiReply &r) {
            if (g != m_generation)
                return;
            if (r.ok() && acceptTokens(r.data().toObject())) {
                m_mfaTicket.clear();
                setState(State::SignedIn);
                return;
            }
            // A wrong code: stay at the second factor and say so.
            if (r.ok())
                setState(State::NeedsMfa, QStringLiteral("kladde.no_tokens"));
            else
                setState(State::NeedsMfa, r);
        });
}

void Session::signIn(const ApiReply &r)
{
    if (!r.ok()) {
        setState(State::SignedOut, r);
        return;
    }
    const QJsonObject meta = r.root.value(QLatin1String("meta")).toObject();
    if (meta.value(QLatin1String("mfaRequired")).toBool()) {
        m_mfaTicket = meta.value(QLatin1String("mfaTicket")).toString();
        m_mfaMethods.clear();
        for (const QJsonValue &m : meta.value(QLatin1String("methods")).toArray())
            m_mfaMethods << m.toString();
        setState(State::NeedsMfa);
        return;
    }
    if (!acceptTokens(r.data().toObject())) {
        setState(State::SignedOut, QStringLiteral("kladde.no_tokens"));
        return;
    }
    setState(State::SignedIn);
}

void Session::logout()
{
    // End the session on the server and spend the refresh token; the answers do not matter,
    // the tokens are gone here either way. A refresh still under way spends the token
    // itself: its answer is dropped and the token it brings revoked (finishRefresh).
    if (!m_accessToken.isEmpty())
        raw("POST", QStringLiteral("/api/auth/logout"), "{}", true, {}, [](const ApiReply &) {});
    if (!m_refreshToken.isEmpty() && !m_refreshing)
        revoke(m_refreshToken);
    endGeneration();
    dropTokens();
    m_username.clear();
    setState(State::SignedOut);
}

void Session::revoke(const QString &refreshToken)
{
    const QJsonObject body{{QStringLiteral("refreshToken"), refreshToken}, {QStringLiteral("revoke"), true}};
    raw("POST", QStringLiteral("/api/auth/refresh"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [](const ApiReply &) {});
}

void Session::endGeneration()
{
    ++m_generation;
    m_refreshing = false;
    const QList<Call> waiting = std::exchange(m_waiting, {});
    for (const Call &c : waiting) {
        ApiReply r;
        r.status = 401;
        c.done(r);
    }
}

void Session::get(const QString &path, Callback done)
{
    send({"GET", path, {}, {}, std::move(done)});
}

void Session::post(const QString &path, const QJsonValue &body, Callback done, const QByteArray &idempotencyKey)
{
    const QByteArray bytes = body.isArray() ? QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact)
                                            : QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact);
    send({"POST", path, bytes, idempotencyKey, std::move(done)});
}

void Session::send(Call call)
{
    const bool stale = m_accessToken.isEmpty()
        || (m_accessExpires.isValid()
            && m_accessExpires.addSecs(-RefreshMarginSecs) <= QDateTime::currentDateTimeUtc());
    if (m_refreshing || (stale && !m_refreshToken.isEmpty())) {
        m_waiting << std::move(call);
        refresh();
        return;
    }
    transmit(std::move(call));
}

void Session::transmit(Call call)
{
    const QByteArray verb = call.verb;
    const QString path = call.path;
    const QByteArray body = call.body;
    const QByteArray key = call.idempotencyKey;
    raw(verb, path, body, true, key, [this, call = std::move(call)](const ApiReply &r) mutable {
        if (r.status == 401 && !call.retried && !m_refreshToken.isEmpty()) {
            call.retried = true;
            m_waiting << std::move(call);
            refresh();
            return;
        }
        call.done(r);
    });
}

void Session::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    const QJsonObject body{{QStringLiteral("refreshToken"), m_refreshToken}};
    raw("POST", QStringLiteral("/api/auth/refresh"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [this, g = m_generation](const ApiReply &r) {
            if (g == m_generation) {
                finishRefresh(r);
                return;
            }
            // Signed out (or another server) meanwhile: never sign in again with this answer,
            // and do not leave the token it brings valid on the server.
            const QString late = r.data().toObject().value(QLatin1String("refreshToken")).toString();
            if (r.ok() && !late.isEmpty())
                revoke(late);
        });
}

void Session::finishRefresh(const ApiReply &r)
{
    m_refreshing = false;
    QList<Call> waiting = std::exchange(m_waiting, {});

    if (r.ok() && acceptTokens(r.data().toObject())) {
        if (m_state != State::SignedIn)
            setState(State::SignedIn);
        for (Call &c : waiting)
            transmit(std::move(c));
        return;
    }

    // Only the server's no signs out. Anything else (no answer, 429, 5xx, a proxy's 404 or
    // 308, a maintenance page) never judged the token: keep it, try again with the next call.
    const bool refused = r.status == 401 || r.status == 403;
    if (!refused) {
        if (m_state == State::Restoring)
            setState(State::SignedIn, r);
    } else {
        // Refused (expired, revoked, already used): sign in again.
        dropTokens();
        setState(State::SignedOut, r);
        Q_EMIT signedOutByServer();
    }
    for (const Call &c : waiting)
        c.done(r);
}

} // namespace kladde
