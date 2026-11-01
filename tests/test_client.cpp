#include "healthlogclient.h"

#include <QTest>

using namespace kladde;

class TestClient : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readsTheCounts()
    {
        const BatchResult r = parseBatchResponse(200, R"({"data": {"processed": 4, "inserted": 2,
            "updated": 1, "duplicates": 1, "failed": 0,
            "skipped": [{"index": 3, "reason": "unmappable_identifier"}], "entries": []},
            "error": null})");
        QVERIFY(r.ok);
        QCOMPARE(r.inserted, 2);
        QCOMPARE(r.updated, 1);
        QCOMPARE(r.duplicates, 1);
        QCOMPARE(r.skipped, QStringList{QStringLiteral("3: unmappable_identifier")});
    }

    void errorCodeWinsOverProse()
    {
        const BatchResult r = parseBatchResponse(422, R"({"data": null, "error": "Not allowed",
            "meta": {"errorCode": "measurement.batch.source_not_permitted"}})");
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("measurement.batch.source_not_permitted"));
        QVERIFY(!r.retryable());
    }

    void whatIsWorthARetry()
    {
        QVERIFY(parseBatchResponse(429, "{}").retryable());
        QVERIFY(parseBatchResponse(503, "").retryable());
        QVERIFY(parseBatchResponse(0, "").retryable());
        QVERIFY(!parseBatchResponse(401, "{}").retryable());
        QCOMPARE(parseBatchResponse(401, "").error, QStringLiteral("HTTP 401"));
    }

    void okNeedsData()
    {
        QVERIFY(!parseBatchResponse(200, R"({"data": null})").ok);
    }
};

QTEST_GUILESS_MAIN(TestClient)
#include "test_client.moc"
