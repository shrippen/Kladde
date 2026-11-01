#include "fakeserver.h"
#include "session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

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

    FakeServer::Answer tokens()
    {
        ++generation;
        access = QStringLiteral("a%1").arg(generation);
        refresh = QStringLiteral("r%1").arg(generation);
        return {200, QJsonObject{{QStringLiteral("data"), QJsonObject{
            {QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), QStringLiteral("u")},
                                                 {QStringLiteral("username"), QStringLiteral("weber")}}},
            {QStringLiteral("token"), access},
            {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTimeUtc().addSecs(900).toString(Qt::ISODateWithMs)},
            {QStringLiteral("refreshToken"), refresh},
            {QStringLiteral("refreshTokenExpiresAt"), QStringLiteral("2099-01-01T00:00:00.000Z")}}}}};
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
        if (r.path == QLatin1String("/api/auth/refresh")) {
            ++refreshCalls;
            if (r.body.value(QLatin1String("refreshToken")).toString() != refresh || refresh.isEmpty())
                return refused(401, "auth.refresh.invalid");
            return tokens();
        }
        if (r.headers.value("authorization") != "Bearer " + access.toUtf8() || access.isEmpty())
            return refused(401, "auth.unauthorized");
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
