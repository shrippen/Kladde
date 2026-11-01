package de.arianw.kladde.sync

import java.time.Instant

/** What a sample measures. Mirrors kladde::Sample::Kind in core/sample.h. */
enum class Kind {
    STEPS,              // count over [start, end)
    DISTANCE,           // metres over [start, end)
    HEART_RATE,         // bpm at start
    RESTING_HEART_RATE, // bpm at start
    SLEEP_STAGE,        // one stage of a sleep session over [start, end)
}

/**
 * One reading as the source delivers it, before any HealthLog mapping.
 *
 * Sleep only: [sleepStage] is the Health Connect stage (SleepSessionRecord.STAGE_TYPE_*),
 * [sessionHasKnownStages] whether the session holds a classified stage (not unknown, not
 * out of bed).
 */
data class Sample(
    val kind: Kind,
    val id: String,
    val app: String,
    val start: Instant,
    val end: Instant,
    val value: Double,
    val sleepStage: Int = 0,
    val sessionHasKnownStages: Boolean = false,
)
