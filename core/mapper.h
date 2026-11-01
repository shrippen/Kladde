#pragma once

#include "sample.h"

#include <QJsonObject>
#include <QStringList>
#include <QTimeZone>

namespace kladde {

// Turns samples into entries for HealthLog's POST /api/measurements/batch, which speaks
// HealthKit identifiers in Apple's units (docs/referenzen.md):
//
// - Steps, distance, active energy and flights become one total per local day of the account
//   (`stats:<identifier>:<YYYY-MM-DD>`, a re-post overwrites). The samples of a day must
//   therefore be complete: the caller passes every sample of each day it maps, not a delta.
// - Heart rate becomes 10-minute buckets in UTC with average, minimum and maximum
//   (`stats:HKQuantityTypeIdentifierHeartRate:<bucket start>`), about 144 entries a day.
// - Readings (resting heart rate, weight, blood pressure …) stay one entry each.
// - Each sleep stage becomes one entry with its duration in minutes and the HealthKit stage.
struct MapResult {
    QList<QJsonObject> entries;
    QStringList skipped; // samples left out, with the reason
};

MapResult mapToBatchEntries(const Samples &samples, const QTimeZone &accountZone);

// Health Connect sleep stage to HKCategoryValueSleepAnalysis, or -1 when the stage is no
// part of the night (out of bed; unknown inside a session that has classified stages).
int hkSleepStage(int healthConnectStage, bool sessionHasKnownStages);

} // namespace kladde
