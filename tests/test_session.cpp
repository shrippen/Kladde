#include "fakeserver.h"
#include "pkce.h"
#include "session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>
#include <QRegularExpression>

using namespace kladde;

namespace {

// A HealthLog stand-in: password "pw", second factor "123456" for "mfa@x", one-time
// refresh tokens r1, r2 … that each give access token a1, a2 …
struct Auth {
    int generation = 0;
    QString access;
    QString refresh;
    int refreshCalls = 0;
    bool down = false;
    int downStatus = 503;
    QString ssoCode = QStringLiteral("hlh_code");
    QByteArray ssoChallenge; // from the address the app opens in the browser

    // The sign-ins name the user, a refresh does not (HealthLog's /api/auth/refresh).
    FakeServer::Answer tokens(bool withUser = true)
    {
        ++generation;
        access = QStringLiteral("a%1").arg(generation);
        refresh = QStringLiteral("r%1").arg(generation);
        QJsonObject data{
            {QStringLiteral("token"), access},
            {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTimeUtc().addSecs(900).toString(Qt::ISODateWithMs)},
            {QStringLiteral("refreshToken"), refresh},
            {QStringLiteral("refreshTokenExpiresAt"), QStringLiteral("2099-01-01T00:00:00.000Z")}};
        if (withUser) {
            data.insert(QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), QStringLiteral("u")},
                                                            {QStringLiteral("username"), QStringLiteral("weber")}});
        }
        return {200, QJsonObject{{QStringLiteral("data"), data}}};
    }

    static FakeServer::Answer refused(int status, const char *code)
    {
        return {status, QJsonObject{{QStringLiteral("data"), QJsonValue()},
                                    {QStringLiteral("error"), QStringLiteral("no")},
                                    {QStringLiteral("meta"), QJsonObject{{QStringLiteral("errorCode"), QLatin1String(code)}}}}};
    }

    FakeServer::Answer handle(const FakeServer::Request &r)
    {
        if (down)
            return {downStatus, {}};
        if (r.path == QLatin1String("/api/auth/login")) {
            if (r.body.value(QLatin1String("password")).toString() != QLatin1String("pw"))
                return refused(401, "auth.invalid_credentials");
            if (r.body.value(QLatin1String("email")).toString() == QLatin1String("mfa@x"))
                return {200, QJsonObject{{QStringLiteral("data"), QJsonValue()},
                                         {QStringLiteral("meta"), QJsonObject{
                                             {QStringLiteral("mfaRequired"), true},
                                             {QStringLiteral("mfaTicket"), QStringLiteral("t")},
                                             {QStringLiteral("methods"), QJsonArray{QStringLiteral("totp"), QStringLiteral("recovery")}}}}}};
            return tokens();
        }
        if (r.path == QLatin1String("/api/auth/mfa/verify")) {
            if (r.body.value(QLatin1String("code")).toString() != QLatin1String("123456"))
                return refused(401, "auth.mfa.invalid_code");
            return tokens();
        }
        if (r.path == QLatin1String("/api/auth/oidc/native/token")) {
            // The code from the callback, locked to the challenge of the browser leg.
            const QByteArray verifier = r.body.value(QLatin1String("codeVerifier")).toString().toLatin1();
            if (r.body.value(QLatin1String("code")).toString() != ssoCode || kladde::pkceChallenge(verifier) != ssoChallenge)
                return refused(401, "auth.unauthorized");
            return tokens();
        }
        if (r.path == QLatin1String("/api/auth/refresh")) {
            ++refreshCalls;
            if (r.body.value(QLatin1String("refreshToken")).toString() != refresh || refresh.isEmpty())
                return refused(401, "auth.refresh.invalid");
            return tokens(false);
        }
        if (r.headers.value("authorization") != "Bearer " + access.toUtf8() || access.isEmpty())
            return refused(401, "auth.unauthorized");
        if (r.path == QLatin1String("/api/auth/me"))
            return {200, QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("username"), QStringLiteral("weber")}}}}};
        return {200, QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("path"), r.path}}}}};
    }
};

struct Rig {
    Auth auth;
    FakeServer server{[this](const FakeServer::Request &r) { return auth.handle(r); }};
    MemorySecretStore secrets;
    Session session{&secrets};

    Rig() { session.setServer(server.url()); }

    bool waitFor(Session::State s)
    {
        return QTest::qWaitFor([&] { return session.state() == s; }, 5000);
    }

    ApiReply get(const QString &path)
    {
        ApiReply out;
        bool done = false;
        session.get(path, [&](const ApiReply &r) {
            out = r;
            done = true;
        });
        (void)QTest::qWaitFor([&] { return done; }, 5000);
        return out;
    }

    QString storedRefresh() const
    {
        return secrets.values().value(QStringLiteral("refresh-token@") + server.url().toString());
    }
};

} // namespace

class TestSession : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void onlyHttpsLeavesTheMachine()
    {
        QVERIFY(Session::acceptableServer(QUrl(QStringLiteral("https://hl.example.org"))));
        QVERIFY(Session::acceptableServer(QUrl(QStringLiteral("http://127.0.0.1:8080"))));
        QVERIFY(!Session::acceptableServer(QUrl(QStringLiteral("http://hl.example.org"))));
        QVERIFY(!Session::acceptableServer(QUrl(QStringLiteral("hl.example.org"))));
    }

    void passwordLoginKeepsTheRefreshTokenInTheKeychain()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QCOMPARE(rig.session.username(), QStringLiteral("weber"));
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r1"));
        const auto &login = rig.server.requests.constFirst();
        QCOMPARE(login.headers.value("x-client-type"), QByteArray("native"));
        QVERIFY(!login.headers.contains("authorization"));
        QVERIFY(rig.get(QStringLiteral("/api/x")).ok());
    }

    void wrongPasswordSaysSo()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("nope"));
        QVERIFY(QTest::qWaitFor([&] { return !rig.session.lastError().isEmpty(); }, 5000));
        QCOMPARE(rig.session.state(), Session::State::SignedOut);
        QCOMPARE(rig.session.lastError(), QStringLiteral("auth.invalid_credentials"));
        QVERIFY(rig.secrets.values().isEmpty());
    }

    void secondFactor()
    {
        Rig rig;
        rig.session.login(QStringLiteral("mfa@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::NeedsMfa));
        QCOMPARE(rig.session.mfaMethods(), (QStringList{QStringLiteral("totp"), QStringLiteral("recovery")}));

        rig.session.verifyMfa(QStringLiteral("000000"));
        QVERIFY(QTest::qWaitFor([&] { return !rig.session.lastError().isEmpty(); }, 5000));
        QCOMPARE(rig.session.state(), Session::State::NeedsMfa);
        QCOMPARE(rig.session.lastError(), QStringLiteral("auth.mfa.invalid_code"));

        rig.session.verifyMfa(QStringLiteral("123 456"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QCOMPARE(rig.server.requests.last().body.value(QLatin1String("mfaTicket")).toString(), QStringLiteral("t"));
    }

    void a401RefreshesOnceAndRepeatsTheCall()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        rig.auth.access = QStringLiteral("revoked"); // the server forgot a1

        const ApiReply r = rig.get(QStringLiteral("/api/x"));
        QVERIFY(r.ok());
        QCOMPARE(rig.auth.refreshCalls, 1);
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r2")); // rotated
    }

    void concurrentCallsShareOneRefresh()
    {
        // The refresh token is single-use: two refreshes at once would sign the user out.
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        rig.auth.access = QStringLiteral("revoked");

        int ok = 0;
        int done = 0;
        for (int i = 0; i < 3; ++i) {
            rig.session.get(QStringLiteral("/api/x"), [&](const ApiReply &r) {
                ok += r.ok();
                ++done;
            });
        }
        QVERIFY(QTest::qWaitFor([&] { return done == 3; }, 5000));
        QCOMPARE(ok, 3);
        QCOMPARE(rig.auth.refreshCalls, 1);
        QCOMPARE(rig.session.state(), Session::State::SignedIn);
    }

    void aRefusedRefreshSignsOut()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QSignalSpy out(&rig.session, &Session::signedOutByServer);
        rig.auth.access = QStringLiteral("revoked");
        rig.auth.refresh = QStringLiteral("revoked");

        QCOMPARE(rig.get(QStringLiteral("/api/x")).status, 401);
        QCOMPARE(rig.session.state(), Session::State::SignedOut);
        QCOMPARE(out.count(), 1);
        QVERIFY(rig.storedRefresh().isEmpty());
    }

    void restoreFromTheKeychain()
    {
        Rig rig;
        rig.auth.refresh = QStringLiteral("r0");
        rig.secrets.write(QStringLiteral("refresh-token@") + rig.server.url().toString(), QStringLiteral("r0"), [](bool) {});
        rig.session.restore();
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r1"));
        QVERIFY(rig.get(QStringLiteral("/api/x")).ok());
    }

    void restoreKnowsTheUser()
    {
        // A refresh does not name the user: after a restart the account showed no name
        // (seen on the phone, 1.11.2026).
        Rig rig;
        rig.auth.refresh = QStringLiteral("r0");
        rig.secrets.write(QStringLiteral("refresh-token@") + rig.server.url().toString(), QStringLiteral("r0"), [](bool) {});
        rig.session.restore();
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QTRY_COMPARE(rig.session.username(), QStringLiteral("weber"));
    }

    void restoreWhileTheServerIsDownKeepsTheToken()
    {
        Rig rig;
        rig.auth.refresh = QStringLiteral("r0");
        rig.auth.down = true;
        rig.secrets.write(QStringLiteral("refresh-token@") + rig.server.url().toString(), QStringLiteral("r0"), [](bool) {});
        rig.session.restore();
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r0"));

        // Back up: the next call refreshes with the kept token.
        rig.auth.down = false;
        QVERIFY(rig.get(QStringLiteral("/api/x")).ok());
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r1"));
    }

    void logoutDuringARefreshStaysSignedOut()
    {
        // The access token runs out, Home reloads, the user taps "sign out": the refresh
        // answer that comes after must neither sign in again nor stay valid on the server.
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(QUrl(QStringLiteral("https://hl.example.org")));
        QList<QPair<QString, Session::Callback>> pending;
        QList<QJsonObject> bodies;
        session.setTransport([&](const QByteArray &, const QString &path, const QByteArray &body, Session::Callback done) {
            bodies << QJsonDocument::fromJson(body).object();
            pending << qMakePair(path, done);
        });
        const QString key = QStringLiteral("refresh-token@https://hl.example.org");
        secrets.write(key, QStringLiteral("r0"), [](bool) {});
        session.restore();
        QVERIFY(QTest::qWaitFor([&] { return pending.size() == 1; }, 5000));
        QCOMPARE(pending[0].first, QStringLiteral("/api/auth/refresh"));

        session.logout();
        QCOMPARE(session.state(), Session::State::SignedOut);
        Auth auth;
        auth.refresh = QStringLiteral("r0");
        const FakeServer::Answer late = auth.tokens();
        ApiReply r;
        r.status = late.status;
        r.root = late.body;
        pending[0].second(r);

        QCOMPARE(session.state(), Session::State::SignedOut);
        QVERIFY(!secrets.values().contains(key));
        QVERIFY(QTest::qWaitFor([&] {
            return std::any_of(bodies.begin(), bodies.end(), [](const QJsonObject &b) {
                return b.value(QLatin1String("refreshToken")).toString() == QLatin1String("r1")
                    && b.value(QLatin1String("revoke")).toBool();
            });
        }, 5000));
    }

    void aProxyAnsweringTheRefreshDoesNotSignOut()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        rig.auth.access = QStringLiteral("revoked");
        rig.auth.down = true;
        rig.auth.downStatus = 404; // a proxy or a maintenance page never saw the token
        QVERIFY(!rig.get(QStringLiteral("/api/x")).ok());
        QCOMPARE(rig.session.state(), Session::State::SignedIn);
        QCOMPARE(rig.storedRefresh(), QStringLiteral("r1"));
    }

    void logoutRevokesAndForgets()
    {
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        rig.session.logout();
        QCOMPARE(rig.session.state(), Session::State::SignedOut);
        QVERIFY(rig.storedRefresh().isEmpty());
        QVERIFY(QTest::qWaitFor([&] {
            for (const auto &r : rig.server.requests) {
                if (r.path == QLatin1String("/api/auth/refresh") && r.body.value(QLatin1String("revoke")).toBool())
                    return true;
            }
            return false;
        }, 5000));
    }

    void logoutRevokesTheKeptTokenNotASpentOne()
    {
        // Android: the background job renewed meanwhile, the vault holds r9 and our r1 is
        // spent. A spent token coming back revokes every token of the user.
        Rig rig;
        rig.session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(rig.waitFor(Session::State::SignedIn));
        rig.secrets.write(QStringLiteral("refresh-token@") + rig.server.url().toString(), QStringLiteral("r9"), [](bool) {});

        rig.session.logout();
        QStringList revoked;
        QVERIFY(QTest::qWaitFor([&] {
            for (const auto &r : rig.server.requests) {
                if (r.path == QLatin1String("/api/auth/refresh") && r.body.value(QLatin1String("revoke")).toBool())
                    revoked << r.body.value(QLatin1String("refreshToken")).toString();
            }
            return !revoked.isEmpty();
        }, 5000));
        QCOMPARE(revoked, QStringList{QStringLiteral("r9")});
    }

    void aRenewerTakesOverTheRefresh()
    {
        // Android: the sync module keeps the session and renews it for app and background
        // job alike. The session asks it, with the access token that was refused.
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(QUrl(QStringLiteral("https://hl.example.org")));
        Auth auth;
        QStringList paths;
        session.setTransport([&](const QByteArray &, const QString &path, const QByteArray &, Session::Callback done) {
            paths << path;
            const FakeServer::Answer a = path == QLatin1String("/api/auth/login") ? auth.tokens()
                : path == QLatin1String("/api/x") && paths.count(path) == 1 ? Auth::refused(401, "auth.unauthorized")
                : FakeServer::Answer{200, QJsonObject{{QStringLiteral("data"), QJsonObject{}}}};
            ApiReply r;
            r.status = a.status;
            r.root = a.body;
            done(r);
        });
        QStringList staleSeen;
        session.setRenewer([&](const QString &stale, Session::Callback done) {
            staleSeen << stale;
            ApiReply r;
            r.status = 200;
            r.root = auth.tokens().body;
            done(r);
        });

        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedIn; }, 5000));
        bool ok = false;
        session.get(QStringLiteral("/api/x"), [&](const ApiReply &r) { ok = r.ok(); });
        QVERIFY(QTest::qWaitFor([&] { return ok; }, 5000));

        QCOMPARE(staleSeen, QStringList{QStringLiteral("a1")});
        QVERIFY(!paths.contains(QStringLiteral("/api/auth/refresh")));
    }

    void aRenewerRefusalSignsOut()
    {
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(QUrl(QStringLiteral("https://hl.example.org")));
        session.setTransport([](const QByteArray &, const QString &, const QByteArray &, Session::Callback) {});
        session.setRenewer([](const QString &, Session::Callback done) {
            ApiReply r;
            r.status = 401;
            done(r);
        });
        secrets.write(QStringLiteral("refresh-token@https://hl.example.org"), QStringLiteral("r1"), [](bool) {});
        QSignalSpy out(&session, &Session::signedOutByServer);
        session.restore();
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedOut; }, 5000));
        QCOMPARE(out.size(), 1);
    }

    void pkceFollowsRfc7636()
    {
        // RFC 7636, appendix B.
        QCOMPARE(kladde::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        const QByteArray v = kladde::pkceVerifier();
        QVERIFY(v.size() >= 43 && v.size() <= 128);
        QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z0-9._~-]+$")).match(QString::fromLatin1(v)).hasMatch());
        QVERIFY(kladde::pkceVerifier() != v);
    }

    void ssoSignsIn()
    {
        // The browser leg ends in healthlog://oidc-callback?code=…; the app trades the code
        // and its verifier for the token pair.
        Auth auth;
        FakeServer server([&](const FakeServer::Request &r) { return auth.handle(r); });
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(server.url());

        const QUrl browser = session.startSso();
        QCOMPARE(browser.path(), QStringLiteral("/api/auth/oidc/login"));
        const QUrlQuery q(browser);
        QCOMPARE(q.queryItemValue(QStringLiteral("client")), QStringLiteral("native"));
        auth.ssoChallenge = q.queryItemValue(QStringLiteral("code_challenge")).toLatin1();
        // Waiting for the browser is not "signing in": it may never come back.
        QCOMPARE(session.state(), Session::State::SignedOut);

        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?code=hlh_code")));
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedIn; }, 5000));
        QCOMPARE(session.username(), QStringLiteral("weber"));
    }

    void requestsLookNative()
    {
        // HealthLog treats a "Mozilla/" user agent or a session cookie as a browser and gives
        // it no refresh token (the SSO exchange answers 401). Qt sends "Mozilla/5.0" unless
        // told otherwise.
        Auth auth;
        FakeServer server([&](const FakeServer::Request &r) { return auth.handle(r); });
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(server.url());
        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedIn; }, 5000));

        QVERIFY(!server.requests.isEmpty());
        for (const FakeServer::Request &r : std::as_const(server.requests)) {
            const QByteArray ua = r.headers.value("user-agent");
            QVERIFY2(!ua.isEmpty() && !ua.contains("Mozilla/"), ua.constData());
            QVERIFY(!r.headers.contains("cookie"));
        }
    }

    void ssoWithSecondFactorAsksForTheCode()
    {
        // An account with a second factor gets a ticket instead of a code.
        Auth auth;
        FakeServer server([&](const FakeServer::Request &r) { return auth.handle(r); });
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(server.url());

        session.startSso();
        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?mfa_ticket=t")));
        QCOMPARE(session.state(), Session::State::NeedsMfa);
        session.verifyMfa(QStringLiteral("123456"));
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedIn; }, 5000));
    }

    void ssoErrorsAndStrayCallbacks()
    {
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(QUrl(QStringLiteral("https://hl.example.org")));
        int calls = 0;
        session.setTransport([&](const QByteArray &, const QString &, const QByteArray &, Session::Callback) { ++calls; });

        // A callback nobody started (another app, an old browser tab) is ignored.
        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?code=hlh_code")));
        QCOMPARE(session.state(), Session::State::SignedOut);

        // The server's reason comes through as an error code.
        session.startSso();
        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?error=oidc_link_required")));
        QCOMPARE(session.state(), Session::State::SignedOut);
        QCOMPARE(session.lastError(), QStringLiteral("sso.oidc_link_required"));

        // The verifier is single-use: a second callback after the error does nothing.
        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?code=hlh_code")));
        QTest::qWait(20);
        QCOMPARE(calls, 0);

        // A password sign-in meanwhile drops the SSO start.
        session.startSso();
        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        session.finishSso(QUrl(QStringLiteral("healthlog://oidc-callback?code=hlh_code")));
        QTest::qWait(20);
        QCOMPARE(calls, 1);
    }

    void aKeychainThatFailsIsReported()
    {
        // A store that refuses at once, before login's own setState(SignedIn).
        struct Refusing : MemorySecretStore {
            void write(const QString &, const QString &, Done done) override { done(false); }
        };
        Auth auth;
        FakeServer server([&](const FakeServer::Request &r) { return auth.handle(r); });
        Refusing secrets;
        Session session(&secrets);
        session.setServer(server.url());
        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return session.state() == Session::State::SignedIn; }, 5000));
        QCOMPARE(session.lastError(), QStringLiteral("kladde.keychain"));
    }

    void anOldCallsAnswerDoesNotRefreshTheNewSession()
    {
        // A request of the first sign-in is still under way when the user signs out and in
        // again; its 401 arrives afterwards. It is answered as is, without spending the new
        // session's refresh token.
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setServer(QUrl(QStringLiteral("https://hl.example.org")));
        QList<QPair<QString, Session::Callback>> pending;
        session.setTransport([&](const QByteArray &, const QString &path, const QByteArray &, Session::Callback done) {
            pending << qMakePair(path, done);
        });
        Auth auth;
        auto answer = [&](int i, const FakeServer::Answer &a) {
            ApiReply r;
            r.status = a.status;
            r.root = a.body;
            pending[i].second(r);
        };
        QSignalSpy generations(&session, &Session::generationChanged);

        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return pending.size() == 1; }, 5000));
        answer(0, auth.tokens());
        QCOMPARE(session.state(), Session::State::SignedIn);

        ApiReply old;
        bool oldDone = false;
        session.get(QStringLiteral("/api/x"), [&](const ApiReply &r) { old = r; oldDone = true; });
        QVERIFY(QTest::qWaitFor([&] { return pending.size() == 2; }, 5000));

        session.logout();
        session.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] {
            return std::any_of(pending.begin(), pending.end(), [](const auto &p) {
                return p.first == QLatin1String("/api/auth/login");
            }) && pending.last().first == QLatin1String("/api/auth/login");
        }, 5000));
        answer(int(pending.size()) - 1, auth.tokens());
        QCOMPARE(session.state(), Session::State::SignedIn);
        QVERIFY(generations.size() >= 2);

        const qsizetype before = pending.size();
        answer(1, Auth::refused(401, "auth.unauthorized"));
        QVERIFY(oldDone);
        QCOMPARE(old.status, 401);
        QTest::qWait(50);
        for (qsizetype i = before; i < pending.size(); ++i)
            QVERIFY(pending[i].first != QLatin1String("/api/auth/refresh"));
        QCOMPARE(session.state(), Session::State::SignedIn);
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "test_session.moc"
