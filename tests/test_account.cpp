#include "account.h"
#include "secretstore.h"
#include "syncbridge.h"
#include "syncbridge_fake.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>
#include <QThread>

#include <thread>

using kladde::ApiReply;
using kladde::MemorySecretStore;
using kladde::Session;

namespace {

const auto ServerA = QStringLiteral("https://a.example.org");
const auto ServerB = QStringLiteral("https://b.example.org");
const auto MintPath = QLatin1String("/api/tokens/measurements");
constexpr int Unauthorized = 401;

struct Pending {
    QString path;
    Session::Callback done;
    QByteArray body;
};

ApiReply tokens()
{
    ApiReply r;
    r.status = 200;
    r.root = QJsonObject{{QStringLiteral("data"), QJsonObject{
        {QStringLiteral("user"), QJsonObject{{QStringLiteral("username"), QStringLiteral("weber")}}},
        {QStringLiteral("token"), QStringLiteral("access")},
        {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTimeUtc().addSecs(900).toString(Qt::ISODateWithMs)},
        {QStringLiteral("refreshToken"), QStringLiteral("refresh")}}}};
    return r;
}

ApiReply refused(int status, const QString &code)
{
    ApiReply r;
    r.status = status;
    r.root = QJsonObject{{QStringLiteral("meta"), QJsonObject{{QStringLiteral("errorCode"), code}}}};
    return r;
}

} // namespace

class TestAccount : public QObject
{
    Q_OBJECT

    // Requests the account sends, answered by the test.
    QList<Pending> m_pending;

    void capture(Account &account)
    {
        account.session()->setTransport([this](const QByteArray &, const QString &path, const QByteArray &body, Session::Callback done) {
            m_pending << Pending{path, done, body};
        });
    }

    qsizetype count(const QString &path) const
    {
        return std::count_if(m_pending.begin(), m_pending.end(), [&](const Pending &p) { return p.path == path; });
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        m_pending.clear();
        FakeSync::forgets = 0;
        FakeSync::server.clear();
        FakeSync::device.clear();
        FakeSync::onForget = nullptr;
        FakeSync::queueDrops = 0;
    }

    void aWrongPasswordIsNamedAsSuch()
    {
        // HealthLog refuses a wrong password with 401 and an auth.* code; at the sign-in that
        // is wrong input, not an ended session.
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);

        account.login(QStringLiteral("weber@x"), QStringLiteral("wrong"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(refused(Unauthorized, QStringLiteral("auth.invalid_credentials")));

        QCOMPARE(account.state(), Account::State::SignedOut);
        QCOMPARE(account.error(), QStringLiteral("Not accepted: the e-mail, user name or password is not right."));
    }

    void aSignInMintsNoToken()
    {
        // HealthLog mints measurement tokens only for a browser session with a fresh sign-in
        // (v1.46); the app's attempt was answered 401 and spent a refresh token.
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);

        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());
        QCOMPARE(account.state(), Account::State::SignedIn);
        QTest::qWait(50);
        QCOMPARE(count(MintPath), 0);
    }

    void aSignInConfiguresTheSync()
    {
        // The sync uploads with this session: it gets the server and the device id the
        // session signed in with (HealthLog renews only for that device).
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerB);
        capture(account);

        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());
        QCOMPARE(account.state(), Account::State::SignedIn);
        QVERIFY(sync.configured());
        QCOMPARE(FakeSync::server, ServerB);
        QVERIFY(!FakeSync::device.isEmpty());
        QCOMPARE(FakeSync::device, account.session()->deviceId());
    }

    void aSignInDropsAnEarlierQueue()
    {
        // The vault may have lost an earlier session (refused in the background) while its
        // queue stayed: a new sign-in, maybe another account or server, must not upload it.
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);
        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());
        QCOMPARE(FakeSync::queueDrops, 1);

        // A restart keeps it.
        Account again(&secrets, &sync);
        again.setServer(ServerA);
        capture(again);
        again.restore();
        QVERIFY(QTest::qWaitFor([&] { return count(QStringLiteral("/api/auth/refresh")) == 1; }, 5000));
        m_pending.last().done(tokens());
        QCOMPARE(again.state(), Account::State::SignedIn);
        QCOMPARE(FakeSync::queueDrops, 1);
    }

    void aSignOutStopsTheSync()
    {
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);
        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());

        account.logout();
        QVERIFY(!sync.configured());
        QCOMPARE(FakeSync::forgets, 1);
    }

    void aSignOutRevokesTheVaultsToken()
    {
        // Android: the vault holds the live refresh token (the background job renewed, ours
        // is spent), and forgetting the sync empties it. Revoke first, then forget.
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);
        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());

        const QString key = QStringLiteral("refresh-token@") + ServerA;
        secrets.write(key, QStringLiteral("r9"), [](bool) {});
        FakeSync::onForget = [&] { secrets.remove(key, [](bool) {}); };
        account.logout();

        QVERIFY(QTest::qWaitFor([&] { return count(QStringLiteral("/api/auth/refresh")) == 1; }, 5000));
        const auto revoke = std::find_if(m_pending.begin(), m_pending.end(), [](const Pending &p) { return p.path == QLatin1String("/api/auth/refresh"); });
        QCOMPARE(QJsonDocument::fromJson(revoke->body).object().value(QLatin1String("refreshToken")).toString(), QStringLiteral("r9"));
    }

    void anSsoCallbackFromAnotherThreadIsHandledOnOurs()
    {
        // Qt on Android hands healthlog:// to the URL handler on Android's UI thread. The
        // session lives on the main thread: a network call from there never finishes.
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);
        QThread *sessionThread = nullptr;
        connect(account.session(), &Session::stateChanged, this, [&] {
            if (account.session()->state() == Session::State::SigningIn) {
                sessionThread = QThread::currentThread();
            }
        }, Qt::DirectConnection);

        account.session()->startSso();
        std::thread android([&] {
            QMetaObject::invokeMethod(&account, "finishSso", Qt::DirectConnection,
                                      Q_ARG(QUrl, QUrl(QStringLiteral("healthlog://oidc-callback?code=hlh_code"))));
        });
        android.join();

        QVERIFY(QTest::qWaitFor([&] { return sessionThread != nullptr; }, 5000));
        QVERIFY2(sessionThread == QThread::currentThread(), "the session ran on Android's thread");
    }
};

QTEST_GUILESS_MAIN(TestAccount)
#include "test_account.moc"
