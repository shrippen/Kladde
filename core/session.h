#pragma once

#include "secretstore.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace kladde {

// One answer of the HealthLog API: {data, error, meta}.
struct ApiReply {
    int status = 0; // 0: no answer (network, TLS, timeout)
    QJsonObject root;
    QString transportError;

    bool ok() const { return status >= 200 && status < 300; }
    QJsonValue data() const { return root.value(QLatin1String("data")); }
    // meta.errorCode, else the error text, else the transport error or "HTTP <status>".
    QString error() const;
};

// A signed-in user of one HealthLog instance, as the native apps do it (X-Client-Type:
// native): password login, second factor, a short-lived access token and a one-time
// refresh token that rotates on every use.
//
//   login ──▶ (NeedsMfa ──verifyMfa──▶) SignedIn ──logout──▶ SignedOut
//   restore: refresh token from the keychain ──refresh──▶ SignedIn | SignedOut
//
// Every call goes through send(): an access token close to expiry is refreshed first, a
// 401 refreshes once and repeats the call. Only one refresh runs at a time; calls arriving
// meanwhile wait for it (the refresh token is single-use, two refreshes would sign out).
class Session : public QObject
{
    Q_OBJECT
public:
    enum class State { SignedOut, Restoring, SigningIn, NeedsMfa, SignedIn };
    Q_ENUM(State)

    using Callback = std::function<void(const ApiReply &)>;
    // Answers a request without the network (the demo build); verb, path with query, body.
    using Transport = std::function<void(const QByteArray &, const QString &, const QByteArray &, Callback)>;

    explicit Session(SecretStore *secrets, QObject *parent = nullptr);

    // https only; plain http is accepted for the loopback address (tests).
    static bool acceptableServer(const QUrl &url);

    void setServer(const QUrl &server);
    QUrl server() const { return m_server; }
    void setDeviceId(const QString &id) { m_deviceId = id; }
    void setTransport(Transport transport) { m_transport = std::move(transport); }

    State state() const { return m_state; }
    QString username() const { return m_username; }
    QStringList mfaMethods() const { return m_mfaMethods; }
    QString lastError() const { return m_lastError; }

    void restore();
    void login(const QString &email, const QString &password);
    void verifyMfa(const QString &code, const QString &method = QStringLiteral("totp"));
    void logout();

    void get(const QString &path, Callback done);
    // idempotencyKey: sent as Idempotency-Key when not empty (entries a user typed in).
    void post(const QString &path, const QJsonValue &body, Callback done,
              const QByteArray &idempotencyKey = {});

Q_SIGNALS:
    void stateChanged();
    // The refresh token was refused: the user has to sign in again.
    void signedOutByServer();

private:
    struct Call {
        QByteArray verb;
        QString path;
        QByteArray body;
        QByteArray idempotencyKey;
        Callback done;
        bool retried = false;
    };

    void send(Call call);
    void transmit(Call call);
    void refresh();
    void finishRefresh(const ApiReply &reply);
    void signIn(const ApiReply &reply);
    bool acceptTokens(const QJsonObject &data);
    void dropTokens();
    void setState(State state, const QString &error = {});
    void raw(const QByteArray &verb, const QString &path, const QByteArray &body, bool withToken,
             const QByteArray &idempotencyKey, Callback done);
    QString secretKey() const;

    SecretStore *m_secrets;
    QNetworkAccessManager *m_nam;
    Transport m_transport;
    QUrl m_server;
    QString m_deviceId;
    State m_state = State::SignedOut;
    QString m_username;
    QStringList m_mfaMethods;
    QString m_mfaTicket;
    QString m_lastError;

    QString m_accessToken;
    QDateTime m_accessExpires;
    QString m_refreshToken;
    bool m_refreshing = false;
    QList<Call> m_waiting;
};

} // namespace kladde
