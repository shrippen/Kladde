#include "batch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace kladde;

class TestBatch : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void splitsAtTheServerLimit()
    {
        QList<QJsonObject> entries;
        for (int i = 0; i < 1201; ++i)
            entries << QJsonObject{{QStringLiteral("externalId"), QString::number(i)}};
        const QList<Batch> batches = makeBatches(entries, QStringLiteral("background"));
        QCOMPARE(batches.size(), 3);
        QCOMPARE(batches.at(0).size, 500);
        QCOMPARE(batches.at(2).size, 201);

        const QJsonObject last = QJsonDocument::fromJson(batches.at(2).body).object();
        QCOMPARE(last.value(QLatin1String("entries")).toArray().size(), 201);
        QCOMPARE(last.value(QLatin1String("syncTrigger")).toString(), QStringLiteral("background"));
        QCOMPARE(last.value(QLatin1String("entries")).toArray().at(0).toObject()
                     .value(QLatin1String("externalId")).toString(),
                 QStringLiteral("1000"));
    }

    void sameBodyNewKey()
    {
        // The server answers a known key from memory for 24 h: a day total that returns to an
        // earlier value must still be written, so the same body gets a new key.
        const QList<QJsonObject> a{QJsonObject{{QStringLiteral("value"), 1}}};
        QCOMPARE(makeBatches(a).at(0).body, makeBatches(a).at(0).body);
        QVERIFY(makeBatches(a).at(0).idempotencyKey != makeBatches(a).at(0).idempotencyKey);
        QCOMPARE(makeBatches(a).at(0).idempotencyKey.size(), 36);
    }

    void noTriggerNoField()
    {
        const QList<QJsonObject> a{QJsonObject{{QStringLiteral("value"), 1}}};
        QVERIFY(!QJsonDocument::fromJson(makeBatches(a).at(0).body).object()
                     .contains(QLatin1String("syncTrigger")));
        QVERIFY(makeBatches({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBatch)
#include "test_batch.moc"
