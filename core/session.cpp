#include "session.h"
#include "pkce.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

namespace kladde {

namespace {

constexpr int TransferTimeoutMs = 30000;
// Error codes of the SSO callback (?error=…) become "sso.<reason>".
const auto SsoErrorPrefix = QStringLiteral("sso.");

// e.g. "Kladde/0.1.0"
QString userAgent()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("Kladde") : QStringLiteral("Kladde/") + version;
}
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
    if (error.isEmpty() && m_keychainFailed && state == State::SignedIn) {
        m_lastError = QStringLiteral("kladde.keychain");
        m_lastErrorStatus = -1;
        m_state = state;
        Q_EMIT stateChanged();
        return;
    }
    m_lastError = error;
    m_lastErrorStatus = error.isEmpty() ? -1 : status;
    m_state = state;
    Q_EMIT stateChanged();
}

void Session::raw(const QByteArray &verb, const QString &path, const QByteArray &body, bool withToken,
                  const QByteArray &idempotencyKey, Callback done, const QUrl &server)
{
    const QUrl base = server.isEmpty() ? m_server : server;
    if (m_transport) {
        // Asynchronous like the network, so callers see the same order of events.
        QTimer::singleShot(0, this, [this, verb, path, body, done] { m_transport(verb, path, body, done); });
        return;
    }
    if (!acceptableServer(base)) {
        QTimer::singleShot(0, this, [done] {
            ApiReply r;
            r.transportError = QStringLiteral("the server address must start with https://");
            done(r);
        });
        return;
    }
    QNetworkRequest req(base.resolved(QUrl(path)));
    req.setTransferTimeout(TransferTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Client-Type", "native");
    // Not Qt's "Mozilla/5.0" and no cookies: HealthLog takes either for a browser and
    // gives it no refresh token.
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
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
    m_keychainFailed = false;
    // A failure shows (stays signed in, but not across a restart). The store may answer at
    // once, before the caller's setState: keep it from being cleared there.
    m_secrets->write(secretKey(), refresh, [this, g = m_generation](bool ok) {
        if (ok || g != m_generation)
            return;
        m_keychainFailed = true;
        m_lastError = QStringLiteral("kladde.keychain");
        m_lastErrorStatus = -1;
        Q_EMIT stateChanged();
    });
    return true;
}

void Session::dropTokens()
{
    m_keychainFailed = false;
    m_ssoVerifier.clear();
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

QUrl Session::startSso()
{
    endGeneration();
    dropTokens();
    m_mfaTicket.clear();
    // The state stays: the browser may never come back (closed, cancelled).
    m_ssoVerifier = pkceVerifier();

    // The native arm of HealthLog's SSO start; the callback address is fixed on the server.
    QUrl url = m_server.resolved(QUrl(QStringLiteral("/api/auth/oidc/login")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("native"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(pkceChallenge(m_ssoVerifier)));
    url.setQuery(query);
    return url;
}

void Session::finishSso(const QUrl &callback)
{
    // Only the answer to our own start: a stray or repeated callback finds no verifier.
    if (m_ssoVerifier.isEmpty()) {
        return;
    }
    const QByteArray verifier = std::exchange(m_ssoVerifier, {});
    const QUrlQuery query(callback);

    // e.g. ?error=oidc_link_required: the account is linked to the SSO once on the web.
    const QString error = query.queryItemValue(QStringLiteral("error"));
    if (!error.isEmpty()) {
        setState(State::SignedOut, SsoErrorPrefix + error);
        return;
    }

    // An account with a second factor: the ticket continues at verifyMfa.
    const QString ticket = query.queryItemValue(QStringLiteral("mfa_ticket"));
    if (!ticket.isEmpty()) {
        m_mfaTicket = ticket;
        m_mfaMethods.clear();
        setState(State::NeedsMfa);
        return;
    }

    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        setState(State::SignedOut, QStringLiteral("kladde.no_tokens"));
        return;
    }
    setState(State::SigningIn);
    const QJsonObject body{{QStringLiteral("code"), code}, {QStringLiteral("codeVerifier"), QString::fromLatin1(verifier)}};
    raw("POST", QStringLiteral("/api/auth/oidc/native/token"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [this, g = m_generation](const ApiReply &r) {
            if (g == m_generation) {
                signIn(r);
            }
        });
}

void Session::logout()
{
    // End the session on the server and spend the refresh token; the answers do not matter,
    // the tokens are gone here either way. A refresh still under way spends the token
    // itself: its answer is dropped and the token it brings revoked (finishRefresh).
    if (!m_accessToken.isEmpty())
        raw("POST", QStringLiteral("/api/auth/logout"), "{}", true, {}, [](const ApiReply &) {});

    // The store may hold a newer token than ours (Android: the background job renewed);
    // ours is spent then, and a spent one coming back revokes every token of the user.
    if (!m_refreshToken.isEmpty() && !m_refreshing) {
        m_secrets->read(secretKey(), [this, ours = m_refreshToken, server = m_server](bool ok, const QString &kept) {
            revoke(ok && !kept.isEmpty() ? kept : ours, server);
        });
    }
    endGeneration();
    dropTokens();
    m_username.clear();
    setState(State::SignedOut);
}

void Session::revoke(const QString &refreshToken, const QUrl &server)
{
    const QJsonObject body{{QStringLiteral("refreshToken"), refreshToken}, {QStringLiteral("revoke"), true}};
    raw("POST", QStringLiteral("/api/auth/refresh"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, [](const ApiReply &) {}, server);
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
    Q_EMIT generationChanged();
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
    raw(verb, path, body, true, key, [this, g = m_generation, call = std::move(call)](const ApiReply &r) mutable {
        // Only a call of this session may use its refresh token; an old one gets its answer.
        if (r.status == 401 && !call.retried && g == m_generation && !m_refreshToken.isEmpty()) {
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
    auto done = [this, g = m_generation, server = m_server](const ApiReply &r) {
            if (g == m_generation) {
                finishRefresh(r);
                return;
            }
            // Signed out (or another server) meanwhile: never sign in again with this answer,
            // and do not leave the token it brings valid on the server.
            const QString late = r.data().toObject().value(QLatin1String("refreshToken")).toString();
            if (r.ok() && !late.isEmpty())
                revoke(late, server);
        };
    if (m_renewer) {
        m_renewer(m_accessToken, done);
        return;
    }
    const QJsonObject body{{QStringLiteral("refreshToken"), m_refreshToken}};
    raw("POST", QStringLiteral("/api/auth/refresh"), QJsonDocument(body).toJson(QJsonDocument::Compact),
        false, {}, done);
}

void Session::fetchUser()
{
    // A refresh does not name the user (restart): ask who it is.
    get(QStringLiteral("/api/auth/me"), [this, g = m_generation](const ApiReply &r) {
        if (g != m_generation || !r.ok()) {
            return;
        }
        m_username = r.data().toObject().value(QLatin1String("username")).toString();
        Q_EMIT stateChanged();
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
        if (m_username.isEmpty()) {
            fetchUser();
        }
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
