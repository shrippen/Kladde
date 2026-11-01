#include "mapper.h"

#include <QHash>
#include <QMap>

#include <algorithm>

namespace kladde {

namespace {

const QString StepsId = QStringLiteral("HKQuantityTypeIdentifierStepCount");
const QString DistanceId = QStringLiteral("HKQuantityTypeIdentifierDistanceWalkingRunning");
const QString HeartRateId = QStringLiteral("HKQuantityTypeIdentifierHeartRate");
const QString SleepId = QStringLiteral("HKCategoryTypeIdentifierSleepAnalysis");

constexpr qint64 BucketMs = 10 * 60 * 1000;
constexpr qsizetype MaxExternalId = 120; // HealthLog's limit

QString iso(const QDateTime &t)
{
    return t.toUTC().toString(Qt::ISODateWithMs);
}

QJsonObject entry(const QString &identifier, double value, const QString &unit,
                  const QDateTime &start, const QDateTime &end, const QString &externalId)
{
    return QJsonObject{
        {QStringLiteral("hkIdentifier"), identifier},
        {QStringLiteral("value"), value},
        {QStringLiteral("unit"), unit},
        {QStringLiteral("startDate"), iso(start)},
        {QStringLiteral("endDate"), iso(end)},
        {QStringLiteral("externalId"), externalId},
    };
}

// A reading's HealthKit identifier and unit; the sample's value / divisor is in it.
struct Quantity {
    QString id;
    QString unit;
    double divisor = 1;
};

constexpr double Percent = 100; // HealthKit sends percentages as fractions

// Same table as android/sync Mapper.READINGS; units as HealthLog reads them.
const QHash<Sample::Kind, Quantity> &readings()
{
    const QString perMinute = QStringLiteral("count/min");
    static const QHash<Sample::Kind, Quantity> table{
        {Sample::Kind::RestingHeartRate, {QStringLiteral("HKQuantityTypeIdentifierRestingHeartRate"), perMinute}},
        {Sample::Kind::Weight, {QStringLiteral("HKQuantityTypeIdentifierBodyMass"), QStringLiteral("kg")}},
        {Sample::Kind::BodyFat, {QStringLiteral("HKQuantityTypeIdentifierBodyFatPercentage"), QStringLiteral("%"), Percent}},
        {Sample::Kind::LeanBodyMass, {QStringLiteral("HKQuantityTypeIdentifierLeanBodyMass"), QStringLiteral("kg")}},
        {Sample::Kind::BloodPressureSystolic, {QStringLiteral("HKQuantityTypeIdentifierBloodPressureSystolic"), QStringLiteral("mmHg")}},
        {Sample::Kind::BloodPressureDiastolic, {QStringLiteral("HKQuantityTypeIdentifierBloodPressureDiastolic"), QStringLiteral("mmHg")}},
        {Sample::Kind::BloodGlucose, {QStringLiteral("HKQuantityTypeIdentifierBloodGlucose"), QStringLiteral("mg/dL")}},
        {Sample::Kind::OxygenSaturation, {QStringLiteral("HKQuantityTypeIdentifierOxygenSaturation"), QStringLiteral("fraction"), Percent}},
        {Sample::Kind::HrvRmssd, {QStringLiteral("HKQuantityTypeIdentifierHeartRateVariabilityRMSSD"), QStringLiteral("ms")}},
        {Sample::Kind::RespiratoryRate, {QStringLiteral("HKQuantityTypeIdentifierRespiratoryRate"), perMinute}},
        {Sample::Kind::Vo2Max, {QStringLiteral("HKQuantityTypeIdentifierVO2Max"), QStringLiteral("mL/(kg*min)")}},
        {Sample::Kind::BodyTemperature, {QStringLiteral("HKQuantityTypeIdentifierBodyTemperature"), QStringLiteral("degC")}},
    };
    return table;
}

struct Bucket {
    double sum = 0;
    double min = 0;
    double max = 0;
    int count = 0;
};

} // namespace

int hkSleepStage(int healthConnectStage, bool sessionHasKnownStages)
{
    // HKCategoryValueSleepAnalysis: 0 in bed, 1 asleep (unspecified), 2 awake, 3 core,
    // 4 deep, 5 REM. Light sleep is Apple's core sleep. Same rules as HealthLog's own
    // Health Connect import (src/lib/import/health-connect/sleep-stage-map.ts).
    switch (healthConnectStage) {
    case 0: return sessionHasKnownStages ? -1 : 1; // unknown
    case 1: return 2;                              // awake
    case 2: return 1;                              // sleeping
    case 3: return -1;                             // out of bed
    case 4: return 3;                              // light
    case 5: return 4;                              // deep
    case 6: return 5;                              // REM
    case 7: return 2;                              // awake in bed
    default: return -1;
    }
}

MapResult mapToBatchEntries(const Samples &samples, const QTimeZone &accountZone)
{
    MapResult result;
    QMap<QDate, double> steps;
    QMap<QDate, double> distance;
    QMap<qint64, Bucket> heartRate; // bucket start (ms since epoch, UTC) -> stats

    for (const Sample &s : samples) {
        switch (s.kind) {
        case Sample::Kind::Steps:
        case Sample::Kind::Distance: {
            // A sample counts for the local day it starts on.
            const QDate day = s.start.toTimeZone(accountZone).date();
            (s.kind == Sample::Kind::Steps ? steps : distance)[day] += s.value;
            break;
        }
        case Sample::Kind::HeartRate: {
            const qint64 ms = s.start.toMSecsSinceEpoch();
            Bucket &b = heartRate[ms - ((ms % BucketMs) + BucketMs) % BucketMs];
            b.min = b.count ? std::min(b.min, s.value) : s.value;
            b.max = b.count ? std::max(b.max, s.value) : s.value;
            b.sum += s.value;
            ++b.count;
            break;
        }
        case Sample::Kind::SleepStage: {
            const int stage = hkSleepStage(s.sleepStage, s.sessionHasKnownStages);
            if (stage < 0) {
                result.skipped << QStringLiteral("sleep %1: stage %2 is no part of the night")
                                      .arg(s.id)
                                      .arg(s.sleepStage);
                break;
            }
            const QString id = QStringLiteral("hc:%1:%2").arg(s.id).arg(s.start.toMSecsSinceEpoch());
            if (id.size() > MaxExternalId) {
                result.skipped << QStringLiteral("sleep %1: id too long").arg(s.id);
                break;
            }
            QJsonObject e = entry(SleepId, s.start.msecsTo(s.end) / 60000.0,
                                  QStringLiteral("category"), s.start, s.end, id);
            e.insert(QStringLiteral("sleepStage"), stage);
            result.entries << e;
            break;
        }
        default: {
            // Systolic and diastolic share the record's id: HealthLog keys by type and id.
            const Quantity q = readings().value(s.kind);
            const QString id = QStringLiteral("hc:%1").arg(s.id);
            if (id.size() > MaxExternalId) {
                result.skipped << QStringLiteral("reading %1: id too long").arg(s.id);
                break;
            }
            result.entries << entry(q.id, s.value / q.divisor, q.unit, s.start, s.end, id);
            break;
        }
        }
    }

    auto addDays = [&](const QMap<QDate, double> &days, const QString &identifier,
                       const QString &unit) {
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            const QDateTime start = it.key().startOfDay(accountZone);
            const QDateTime end = it.key().addDays(1).startOfDay(accountZone);
            result.entries << entry(identifier, it.value(), unit, start, end,
                                    QStringLiteral("stats:%1:%2")
                                        .arg(identifier, it.key().toString(Qt::ISODate)));
        }
    };
    addDays(steps, StepsId, QStringLiteral("count"));
    addDays(distance, DistanceId, QStringLiteral("m"));

    for (auto it = heartRate.cbegin(); it != heartRate.cend(); ++it) {
        const QDateTime start = QDateTime::fromMSecsSinceEpoch(it.key(), QTimeZone::UTC);
        const Bucket &b = it.value();
        QJsonObject e = entry(HeartRateId, b.sum / b.count, QStringLiteral("count/min"), start,
                              start.addMSecs(BucketMs),
                              QStringLiteral("stats:%1:%2").arg(HeartRateId, iso(start)));
        e.insert(QStringLiteral("valueMin"), b.min);
        e.insert(QStringLiteral("valueMax"), b.max);
        result.entries << e;
    }
    return result;
}

} // namespace kladde
