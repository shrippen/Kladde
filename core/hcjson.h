#pragma once

#include "sample.h"

#include <QByteArray>
#include <QStringList>

namespace kladde {

// The exchange format between a data source and the core: what the Health Connect probe
// (spikes/hc-probe) exports, and what the Kotlin reader hands over in the app. Layout:
//
//   {"records": [
//     {"type": "Steps", "id": "...", "app": "...", "start": ISO, "end": ISO, "count": 120},
//     {"type": "Distance", "id": "...", "app": "...", "start": ISO, "end": ISO, "meters": 80.5},
//     {"type": "ActiveCaloriesBurned", "id": "...", "app": "...", "start": ISO, "end": ISO, "kcal": 210},
//     {"type": "FloorsClimbed", "id": "...", "app": "...", "start": ISO, "end": ISO, "floors": 3},
//     {"type": "HeartRate", "id": "...", "app": "...", "samples": [{"time": ISO, "bpm": 61}]},
//       (a sample may carry "min" and "max": a 10-minute bucket, "bpm" its average)
//     {"type": "RestingHeartRate", "id": "...", "app": "...", "time": ISO, "bpm": 52},
//     {"type": "Weight", "id": "...", "app": "...", "time": ISO, "value": 72.4},
//       (also BodyFat, LeanBodyMass, BloodGlucose, OxygenSaturation, HeartRateVariabilityRmssd,
//        RespiratoryRate, Vo2Max, BodyTemperature; units as in sample.h)
//     {"type": "BloodPressure", "id": "...", "app": "...", "time": ISO, "systolic": 120, "diastolic": 80},
//     {"type": "SleepSession", "id": "...", "app": "...", "start": ISO, "end": ISO,
//      "stages": [{"start": ISO, "end": ISO, "stage": 4}]},
//     {"type": "Exercise", ...}            // read, not mapped yet (workouts come later)
//   ]}
struct ParseResult {
    Samples samples;
    QStringList warnings; // skipped records, with the reason
    QString error;        // set when the document itself is unusable
};

ParseResult parseHealthConnectJson(const QByteArray &json);

} // namespace kladde
