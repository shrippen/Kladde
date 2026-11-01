#include "account.h"
#include "secretstore.h"
#include "syncbridge.h"
#include "syncbridge_fake.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

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

ApiReply minted(const QString &token)
{
    ApiReply r;
    r.status = 201;
    r.root = QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("token"), token}}}};
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
        account.session()->setTransport([this](const QByteArray &, const QString &path, const QByteArray &, Session::Callback done) {
            m_pending << Pending{path, done};
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
        FakeSync::token.clear();
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

    void aNewSignInGetsItsOwnSyncToken()
    {
        // The sync still carries the token of an earlier sign-in (the keychain lost the
        // session, so no sign-out forgot it). Signing in to another server must not leave
        // the sync uploading to the old one.
        MemorySecretStore secrets;
        SyncBridge sync;
        sync.configure(ServerA, QStringLiteral("old"));
        Account account(&secrets, &sync);
        account.setServer(ServerB);
        capture(account);

        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());
        QCOMPARE(account.state(), Account::State::SignedIn);

        QVERIFY(QTest::qWaitFor([&] { return count(MintPath) == 1; }, 5000));
        m_pending.last().done(minted(QStringLiteral("new")));
        QCOMPARE(FakeSync::server, ServerB);
        QCOMPARE(FakeSync::token, QStringLiteral("new"));
    }

    void aKeychainFailureMintsNoSecondToken()
    {
        // The keychain refuses the refresh token while the sync token is being minted: the
        // state change that reports it must not mint a second, unused token.
        struct Late : MemorySecretStore {
            Done pending;
            void write(const QString &, const QString &, Done done) override { pending = std::move(done); }
        };
        Late secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        account.setServer(ServerA);
        capture(account);

        account.login(QStringLiteral("weber@x"), QStringLiteral("pw"));
        QVERIFY(QTest::qWaitFor([&] { return m_pending.size() == 1; }, 5000));
        m_pending[0].done(tokens());
        QVERIFY(QTest::qWaitFor([&] { return count(MintPath) == 1; }, 5000));

        QVERIFY(secrets.pending);
        secrets.pending(false);
        QTest::qWait(50);
        QCOMPARE(count(MintPath), 1);
    }
};

QTEST_GUILESS_MAIN(TestAccount)
#include "test_account.moc"
