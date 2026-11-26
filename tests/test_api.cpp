#include "api.h"

#include <QJsonDocument>
#include <QTest>

using namespace kladde;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

} // namespace

class TestApi : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void dashboardTiles()
    {
        const auto metrics = parseDashboard(json(R"({"metrics": [
            {"id": "m1", "kind": "bloodPressure", "titleKey": "x", "latestValue": 128, "secondaryValue": 82,
             "unitKey": "u", "unit": "mmHg", "trend": "down", "trendSentiment": "positive",
             "sparkline": [131, 129, 128], "updatedAt": "2026-10-31T07:12:00.000Z", "allTimeCount": 40},
            {"id": "m2", "kind": "steps", "titleKey": "x", "latestValue": null, "secondaryValue": null,
             "unitKey": "u", "unit": null, "trend": "unknown", "sparkline": [], "updatedAt": null, "allTimeCount": 0},
            {"id": "m3"}]})"));
        QCOMPARE(metrics.size(), 2);
        QCOMPARE(metrics[0].kind, QStringLiteral("bloodPressure"));
        QCOMPARE(*metrics[0].latest, 128.0);
        QCOMPARE(*metrics[0].secondary, 82.0);
        QCOMPARE(metrics[0].sparkline, (QList<double>{131, 129, 128}));
        QCOMPARE(metrics[0].sentiment, QStringLiteral("positive"));
        QCOMPARE(metrics[0].updatedAt, QDateTime(QDate(2026, 10, 31), QTime(7, 12), QTimeZone::UTC));
        QVERIFY(!metrics[1].latest);
        QVERIFY(!metrics[1].updatedAt.isValid());
    }

    void seriesIsSortedAndKeepsRanges()
    {
        const Series s = parseSeries(json(R"({"kind": "pulse", "unit": "bpm",
            "points": [
              {"id": "b", "at": "2026-10-31T10:00:00.000Z", "value": 70, "secondary": null, "valueMin": 60, "valueMax": 90},
              {"id": "a", "at": "2026-10-30T10:00:00.000Z", "value": 65, "secondary": null},
              {"id": "c", "at": "bad", "value": 1}],
            "stats": {"mean": 67.5, "min": 65, "max": 70, "stdDev": 2.5, "count": 2},
            "change": {"from": 65, "to": 70, "delta": 5, "sentiment": "neutral"},
            "referenceLines": [{"value": 100, "kind": "upper", "source": "clinical", "appliesTo": "value",
                                "labelKey": "k", "label": "Tachykardie"}]})"));
        QCOMPARE(s.unit, QStringLiteral("bpm"));
        QCOMPARE(s.points.size(), 2);
        QCOMPARE(s.points[0].value, 65.0);
        QCOMPARE(*s.points[1].min, 60.0);
        QCOMPARE(*s.points[1].max, 90.0);
        QCOMPARE(s.count, 2);
        QCOMPARE(*s.change, 5.0);
        QCOMPARE(s.referenceLines.size(), 1);
        QVERIFY(s.referenceLines[0].upper);
    }

    void entryRanges()
    {
        QVERIFY(validateEntry(EntryKind::Weight, {{QStringLiteral("value"), 72.4}}).isEmpty());
        QCOMPARE(validateEntry(EntryKind::Weight, {{QStringLiteral("value"), 724}}), QStringList{QStringLiteral("value")});
        QCOMPARE(validateEntry(EntryKind::Weight, {}), QStringList{QStringLiteral("value")});
        QVERIFY(validateEntry(EntryKind::BloodPressure,
                              {{QStringLiteral("systolic"), 128}, {QStringLiteral("diastolic"), 82}}).isEmpty());
        QCOMPARE(validateEntry(EntryKind::BloodPressure,
                               {{QStringLiteral("systolic"), 82}, {QStringLiteral("diastolic"), 128}}),
                 QStringList{QStringLiteral("diastolic")});
        QCOMPARE(validateEntry(EntryKind::BloodPressure, {{QStringLiteral("systolic"), 128},
                                                          {QStringLiteral("diastolic"), 82},
                                                          {QStringLiteral("pulse"), 400}}),
                 QStringList{QStringLiteral("pulse")});
        QCOMPARE(validateEntry(EntryKind::Mood, {{QStringLiteral("value"), 0}}), QStringList{QStringLiteral("value")});
    }

    void bloodPressureIsTwoMeasurements()
    {
        const QDateTime at(QDate(2026, 11, 1), QTime(8, 30), QTimeZone("Europe/Berlin"));
        const auto bodies = measurementBodies(EntryKind::BloodPressure,
                                              {{QStringLiteral("systolic"), 128}, {QStringLiteral("diastolic"), 82},
                                               {QStringLiteral("pulse"), 64}}, at, QStringLiteral("nach dem Laufen"));
        QCOMPARE(bodies.size(), 3);
        QCOMPARE(bodies[0].value(QLatin1String("type")).toString(), QStringLiteral("BLOOD_PRESSURE_SYS"));
        QCOMPARE(bodies[1].value(QLatin1String("type")).toString(), QStringLiteral("BLOOD_PRESSURE_DIA"));
        QCOMPARE(bodies[2].value(QLatin1String("type")).toString(), QStringLiteral("PULSE"));
        QCOMPARE(bodies[0].value(QLatin1String("measuredAt")).toString(), QStringLiteral("2026-11-01T07:30:00.000Z"));
        QCOMPARE(bodies[1].value(QLatin1String("notes")).toString(), QStringLiteral("nach dem Laufen"));
        QVERIFY(!bodies[0].contains(QLatin1String("source")));
    }

    void valuesAsShown()
    {
        const QLocale de(QLocale::German);
        QCOMPARE(formatValue(QStringLiteral("steps"), 8412.4, {}, de), QStringLiteral("8.412"));
        QCOMPARE(formatValue(QStringLiteral("bloodPressure"), 128, 82.0, de), QStringLiteral("128/82"));
        QCOMPARE(formatValue(QStringLiteral("weight"), 72.44, {}, de), QStringLiteral("72,4"));
        QCOMPARE(formatValue(QStringLiteral("weight"), 72.0, {}, de), QStringLiteral("72"));
        QCOMPARE(formatValue(QStringLiteral("pulse"), 64, {}, QLocale(QLocale::English)), QStringLiteral("64"));
    }

    void goodFollowsServerVerdict()
    {
        const QString steps = QStringLiteral("steps");
        QCOMPARE(goodDirection(steps, 1, QStringLiteral("positive")), 1);
        QCOMPARE(goodDirection(steps, -1, QStringLiteral("positive")), -1);
        QCOMPARE(goodDirection(QStringLiteral("weight"), -1, QStringLiteral("positive")), -1);
        QCOMPARE(goodDirection(QStringLiteral("weight"), 1, QStringLiteral("negative")), -1);
        QCOMPARE(goodDirection(QStringLiteral("weight"), -1, QStringLiteral("negative")), 1);
        // The server beats the kind: pulse rising is called good here.
        QCOMPARE(goodDirection(QStringLiteral("pulse"), 1, QStringLiteral("positive")), 1);
    }

    void goodFallsBackToKind()
    {
        for (const char *kind : {"steps", "sleep", "mood"}) {
            QCOMPARE(goodDirection(QString::fromLatin1(kind), 0, {}), 1);
            QCOMPARE(goodDirection(QString::fromLatin1(kind), -1, QStringLiteral("neutral")), 1);
        }
        for (const char *kind : {"pulse", "restingHeartRate", "bloodPressure"}) {
            QCOMPARE(goodDirection(QString::fromLatin1(kind), 1, {}), -1);
            QCOMPARE(goodDirection(QString::fromLatin1(kind), 0, QStringLiteral("positive")), -1);
        }
        QCOMPARE(goodDirection(QStringLiteral("weight"), 1, {}), 0);
        QCOMPARE(goodDirection(QStringLiteral("bodyFat"), 0, QStringLiteral("positive")), 0);
    }

    void bloodPressureSummary()
    {
        const Series s = parseSeries(json(R"({"kind": "bloodPressure", "unit": "mmHg",
            "points": [
              {"at": "2026-10-28T08:00:00.000Z", "value": 140, "secondary": 90},
              {"at": "2026-10-29T08:00:00.000Z", "value": 120, "secondary": 70},
              {"at": "2026-10-30T08:00:00.000Z", "value": 150, "secondary": null},
              {"at": "2026-10-31T08:00:00.000Z", "value": 130, "secondary": 80}],
            "stats": {"mean": 135, "min": 120, "max": 150, "count": 4}})"));
        const SeriesSummary sum = summarize(QStringLiteral("bloodPressure"), s);
        QCOMPARE(sum.points.size(), 3);
        QCOMPARE(sum.count, 3);
        QCOMPARE(sum.mean, QStringLiteral("130/80"));
        QCOMPARE(sum.low, QStringLiteral("120/70"));
        QCOMPARE(sum.high, QStringLiteral("140/90"));
        QCOMPARE(sum.scaleMin, 63.0); // floor(70 * 0.9)
    }

    void singleValueSummary()
    {
        const Series s = parseSeries(json(R"({"kind": "weight", "unit": "kg",
            "points": [
              {"at": "2026-10-30T08:00:00.000Z", "value": 72.0},
              {"at": "2026-10-31T08:00:00.000Z", "value": 73.5}],
            "stats": {"mean": 72.75, "min": 72, "max": 73.5, "count": 2}})"));
        const SeriesSummary sum = summarize(QStringLiteral("weight"), s);
        QCOMPARE(sum.points.size(), 2);
        QCOMPARE(sum.count, 2);
        QCOMPARE(sum.mean, formatValue(QStringLiteral("weight"), 72.75));
        QCOMPARE(sum.low, QStringLiteral("72"));
        QCOMPARE(sum.high, formatValue(QStringLiteral("weight"), 73.5));
        QCOMPARE(sum.scaleMin, 64.0); // floor(72 * 0.9)

        // Totals start at zero; no readings leave the texts empty.
        QCOMPARE(summarize(QStringLiteral("steps"), s).scaleMin, 0.0);
        const SeriesSummary none = summarize(QStringLiteral("weight"), Series());
        QVERIFY(none.mean.isEmpty() && none.low.isEmpty() && none.high.isEmpty());
        QCOMPARE(none.scaleMin, 0.0);
    }

    void moodScale()
    {
        const QDateTime at(QDate(2026, 11, 1), QTime(20, 0), QTimeZone::UTC);
        QCOMPARE(moodBody(5, at).value(QLatin1String("mood")).toString(), QStringLiteral("SUPER_GUT"));
        QCOMPARE(moodBody(1, at).value(QLatin1String("mood")).toString(), QStringLiteral("LAUSIG"));
        QCOMPARE(moodBody(3, at).value(QLatin1String("moodLoggedAt")).toString(), QStringLiteral("2026-11-01T20:00:00.000Z"));
    }
};

QTEST_GUILESS_MAIN(TestApi)
#include "test_api.moc"
