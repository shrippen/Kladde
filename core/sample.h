#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace kladde {

// One reading as a data source delivers it, before any HealthLog mapping. Every source
// (Health Connect on Android, later the Pebble daemon on Plasma Mobile) produces these,
// so mapping, batching and upload are the same for all of them.
struct Sample {
    enum class Kind {
        Steps,            // count over [start, end)
        Distance,         // metres over [start, end)
        ActiveEnergy,     // kcal over [start, end)
        FlightsClimbed,   // floors over [start, end)
        HeartRate,        // bpm at start (end == start)
        RestingHeartRate, // bpm at start
        SleepStage,       // one stage of a sleep session over [start, end)

        // Readings at start, one entry each.
        Weight,                 // kg
        BodyFat,                // percent
        LeanBodyMass,           // kg
        BloodPressureSystolic,  // mmHg
        BloodPressureDiastolic, // mmHg
        BloodGlucose,           // mg/dL
        OxygenSaturation,       // percent
        HrvRmssd,               // ms
        RespiratoryRate,        // breaths/min
        Vo2Max,                 // mL/(kg·min)
        BodyTemperature,        // °C
    };

    Kind kind = Kind::Steps;
    QString id;        // stable id from the source (Health Connect record id)
    QString sourceApp; // package that wrote it, e.g. coredevices.coreapp
    QDateTime start;   // UTC
    QDateTime end;     // UTC
    double value = 0;

    // Heart rate only: set when the sample is already a bucket (Health Connect's aggregate),
    // value its average.
    std::optional<double> min;
    std::optional<double> max;

    // Sleep only: the Health Connect stage (SleepSessionRecord.STAGE_TYPE_*), and whether
    // the session holds at least one classified stage (not unknown, not out of bed).
    int sleepStage = 0;
    bool sessionHasKnownStages = false;
};

using Samples = QList<Sample>;

} // namespace kladde
