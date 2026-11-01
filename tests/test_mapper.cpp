#include "hcjson.h"
#include "mapper.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QTest>

using namespace kladde;

namespace {

Samples fixture(QStringList *warnings = nullptr)
{
    QFile f(QStringLiteral(KLADDE_FIXTURES "/pebble-day.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("fixture missing");
    const ParseResult r = parseHealthConnectJson(f.readAll());
    if (warnings)
        *warnings = r.warnings;
    return r.samples;
}

QList<QJsonObject> withIdentifier(const QList<QJsonObject> &entries, const QString &id)
{
    QList<QJsonObject> out;
    for (const QJsonObject &e : entries) {
        if (e.value(QLatin1String("hkIdentifier")).toString() == id)
            out << e;
    }
    return out;
}

} // namespace

class TestMapper : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parseSkipsWhatItCannotMap()
    {
        QStringList warnings;
        fixture(&warnings);
        QCOMPARE(warnings.size(), 2);
        QVERIFY(warnings.at(0).contains(QLatin1String("Exercise")));
        QVERIFY(warnings.at(1).contains(QLatin1String("no id")));
    }

    void parseRejectsBadDocuments()
    {
        QVERIFY(!parseHealthConnectJson("not json").error.isEmpty());
        QVERIFY(!parseHealthConnectJson("{\"items\": []}").error.isEmpty());
        QVERIFY(parseHealthConnectJson("{\"records\": []}").error.isEmpty());
    }

    void stepsAreLocalDayTotals()
    {
        const MapResult r = mapToBatchEntries(fixture(), QTimeZone("Europe/Berlin"));
        const auto steps = withIdentifier(r.entries, QStringLiteral("HKQuantityTypeIdentifierStepCount"));
        QCOMPARE(steps.size(), 2);
        // 22:50 UTC on 31 October is still 31 October in Berlin, 23:30 UTC is 1 November.
        QCOMPARE(steps.at(0).value(QLatin1String("externalId")).toString(),
                 QStringLiteral("stats:HKQuantityTypeIdentifierStepCount:2026-10-31"));
        QCOMPARE(steps.at(0).value(QLatin1String("value")).toDouble(), 40.0);
        QCOMPARE(steps.at(1).value(QLatin1String("value")).toDouble(), 1020.0);
        QCOMPARE(steps.at(1).value(QLatin1String("startDate")).toString(),
                 QStringLiteral("2026-10-31T23:00:00.000Z"));
        QCOMPARE(steps.at(1).value(QLatin1String("endDate")).toString(),
                 QStringLiteral("2026-11-01T23:00:00.000Z"));
        QCOMPARE(steps.at(1).value(QLatin1String("unit")).toString(), QStringLiteral("count"));

        const auto distance = withIdentifier(
            r.entries, QStringLiteral("HKQuantityTypeIdentifierDistanceWalkingRunning"));
        QCOMPARE(distance.size(), 1);
        QCOMPARE(distance.at(0).value(QLatin1String("value")).toDouble(), 640.5);
        QCOMPARE(distance.at(0).value(QLatin1String("unit")).toString(), QStringLiteral("m"));
    }

    void dayOfTheClockChangeHas25Hours()
    {
        Sample s;
        s.kind = Sample::Kind::Steps;
        s.id = QStringLiteral("x");
        s.start = QDateTime(QDate(2026, 10, 25), QTime(12, 0), QTimeZone::UTC);
        s.end = s.start.addSecs(60);
        s.value = 1;
        const MapResult r = mapToBatchEntries({s}, QTimeZone("Europe/Berlin"));
        QCOMPARE(r.entries.size(), 1);
        QCOMPARE(r.entries.at(0).value(QLatin1String("startDate")).toString(),
                 QStringLiteral("2026-10-24T22:00:00.000Z"));
        QCOMPARE(r.entries.at(0).value(QLatin1String("endDate")).toString(),
                 QStringLiteral("2026-10-25T23:00:00.000Z"));
    }

    void heartRateComesInTenMinuteBuckets()
    {
        const MapResult r = mapToBatchEntries(fixture(), QTimeZone("Europe/Berlin"));
        const auto hr = withIdentifier(r.entries, QStringLiteral("HKQuantityTypeIdentifierHeartRate"));
        QCOMPARE(hr.size(), 2);
        QCOMPARE(hr.at(0).value(QLatin1String("externalId")).toString(),
                 QStringLiteral("stats:HKQuantityTypeIdentifierHeartRate:2026-11-01T14:00:00.000Z"));
        QCOMPARE(hr.at(0).value(QLatin1String("value")).toDouble(), 65.0);
        QCOMPARE(hr.at(0).value(QLatin1String("valueMin")).toDouble(), 60.0);
        QCOMPARE(hr.at(0).value(QLatin1String("valueMax")).toDouble(), 70.0);
        QCOMPARE(hr.at(0).value(QLatin1String("endDate")).toString(),
                 QStringLiteral("2026-11-01T14:10:00.000Z"));
        QCOMPARE(hr.at(1).value(QLatin1String("value")).toDouble(), 90.0);
        QCOMPARE(hr.at(1).value(QLatin1String("unit")).toString(), QStringLiteral("count/min"));

        const auto resting = withIdentifier(
            r.entries, QStringLiteral("HKQuantityTypeIdentifierRestingHeartRate"));
        QCOMPARE(resting.size(), 1);
        QCOMPARE(resting.at(0).value(QLatin1String("externalId")).toString(), QStringLiteral("hc:rhr-1"));
    }

    void sleepStagesMapToHealthKit()
    {
        const MapResult r = mapToBatchEntries(fixture(), QTimeZone("Europe/Berlin"));
        const auto sleep = withIdentifier(r.entries, QStringLiteral("HKCategoryTypeIdentifierSleepAnalysis"));
        // Out of bed and the unknown gap inside a classified session are left out.
        QCOMPARE(sleep.size(), 4);
        QCOMPARE(r.skipped.size(), 2);
        QList<int> stages;
        for (const QJsonObject &e : sleep)
            stages << e.value(QLatin1String("sleepStage")).toInt();
        QCOMPARE(stages, (QList<int>{3, 4, 5, 2}));
        QCOMPARE(sleep.at(0).value(QLatin1String("value")).toDouble(), 150.0);
        QVERIFY(sleep.at(0).value(QLatin1String("externalId")).toString().startsWith(
            QLatin1String("hc:sleep-1:")));
    }

    void sleepStageTable_data()
    {
        QTest::addColumn<int>("hc");
        QTest::addColumn<bool>("known");
        QTest::addColumn<int>("hk");
        QTest::newRow("unknown alone") << 0 << false << 1;
        QTest::newRow("unknown among stages") << 0 << true << -1;
        QTest::newRow("awake") << 1 << true << 2;
        QTest::newRow("sleeping") << 2 << true << 1;
        QTest::newRow("out of bed") << 3 << true << -1;
        QTest::newRow("light") << 4 << true << 3;
        QTest::newRow("deep") << 5 << true << 4;
        QTest::newRow("REM") << 6 << true << 5;
        QTest::newRow("awake in bed") << 7 << true << 2;
        QTest::newRow("future stage") << 8 << true << -1;
    }

    void sleepStageTable()
    {
        QFETCH(int, hc);
        QFETCH(bool, known);
        QFETCH(int, hk);
        QCOMPARE(hkSleepStage(hc, known), hk);
    }

    void sessionWithoutStagesIsPlainSleep()
    {
        const ParseResult p = parseHealthConnectJson(R"({"records": [{"type": "SleepSession",
            "id": "s", "start": "2026-11-01T00:00:00Z", "end": "2026-11-01T07:00:00Z"}]})");
        const MapResult r = mapToBatchEntries(p.samples, QTimeZone::UTC);
        QCOMPARE(r.entries.size(), 1);
        QCOMPARE(r.entries.at(0).value(QLatin1String("sleepStage")).toInt(), 1);
        QCOMPARE(r.entries.at(0).value(QLatin1String("value")).toDouble(), 420.0);
    }

    void matchesTheSharedFixture_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("pebble-day") << QStringLiteral("pebble-day");
        QTest::newRow("readings") << QStringLiteral("readings");
        QTest::newRow("day-totals") << QStringLiteral("day-totals");
    }

    void matchesTheSharedFixture()
    {
        // The same expectation as the Android sync (android/sync MapperTest): both map alike.
        QFETCH(QString, name);
        QFile in(QStringLiteral(KLADDE_FIXTURES "/%1.json").arg(name));
        QVERIFY(in.open(QIODevice::ReadOnly));
        const Samples samples = parseHealthConnectJson(in.readAll()).samples;
        QFile f(QStringLiteral(KLADDE_FIXTURES "/%1.expected.json").arg(name));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject expected = QJsonDocument::fromJson(f.readAll()).object();
        const QTimeZone zone(expected.value(QLatin1String("zone")).toString().toUtf8());

        // Systolic and diastolic share an id: key by identifier and id.
        auto byId = [](const QJsonArray &entries) {
            QMap<QString, QJsonObject> out;
            for (const QJsonValue &v : entries) {
                const QJsonObject o = v.toObject();
                out.insert(o.value(QLatin1String("hkIdentifier")).toString() + QLatin1Char(' ')
                               + o.value(QLatin1String("externalId")).toString(),
                           o);
            }
            return out;
        };
        QJsonArray got;
        for (const QJsonObject &e : mapToBatchEntries(samples, zone).entries)
            got.append(e);
        QCOMPARE(byId(got), byId(expected.value(QLatin1String("entries")).toArray()));
    }

    void sameInputSameEntries()
    {
        // Stable external ids: a second sync of the same data must produce identical entries.
        const auto a = mapToBatchEntries(fixture(), QTimeZone("Europe/Berlin")).entries;
        const auto b = mapToBatchEntries(fixture(), QTimeZone("Europe/Berlin")).entries;
        QCOMPARE(a, b);
    }
};

QTEST_GUILESS_MAIN(TestMapper)
#include "test_mapper.moc"
