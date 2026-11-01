package de.arianw.kladde.sync

import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/**
 * Samples to entries for HealthLog's POST /api/measurements/batch, which speaks HealthKit
 * identifiers in Apple's units. Same rules as core/mapper.cpp (checked by the shared
 * fixtures in tests/fixtures):
 *
 * - Steps and distance: one total per local day (`stats:<id>:<YYYY-MM-DD>`, overwrites), so
 *   the samples of a day must be complete.
 * - Heart rate: 10-minute UTC buckets with average, min and max, about 144 a day.
 * - Resting heart rate: one entry per reading.
 * - Sleep: one entry per stage, duration in minutes, the HealthKit stage.
 */
object Mapper {

    const val STEPS_ID = "HKQuantityTypeIdentifierStepCount"
    const val DISTANCE_ID = "HKQuantityTypeIdentifierDistanceWalkingRunning"
    const val HEART_RATE_ID = "HKQuantityTypeIdentifierHeartRate"
    const val RESTING_ID = "HKQuantityTypeIdentifierRestingHeartRate"
    const val SLEEP_ID = "HKCategoryTypeIdentifierSleepAnalysis"

    private const val BUCKET_MS = 10 * 60 * 1000L
    private const val MAX_EXTERNAL_ID = 120 // HealthLog's limit
    private const val UNIT_PER_MINUTE = "count/min"

    private val ISO: DateTimeFormatter =
        DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'").withZone(ZoneOffset.UTC)

    class Result(val entries: List<JSONObject>, val skipped: List<String>)

    private class Bucket(var sum: Double = 0.0, var min: Double = 0.0, var max: Double = 0.0, var count: Int = 0)

    fun map(samples: List<Sample>, zone: ZoneId): Result {
        val entries = mutableListOf<JSONObject>()
        val skipped = mutableListOf<String>()
        val steps = sortedMapOf<LocalDate, Double>()
        val distance = sortedMapOf<LocalDate, Double>()
        val buckets = sortedMapOf<Long, Bucket>()

        for (s in samples) {
            when (s.kind) {
                Kind.STEPS, Kind.DISTANCE -> {
                    // A sample counts for the local day it starts on.
                    val day = s.start.atZone(zone).toLocalDate()
                    val totals = if (s.kind == Kind.STEPS) steps else distance
                    totals[day] = (totals[day] ?: 0.0) + s.value
                }

                Kind.HEART_RATE -> {
                    val ms = s.start.toEpochMilli()
                    val b = buckets.getOrPut(ms - Math.floorMod(ms, BUCKET_MS)) { Bucket() }
                    b.min = if (b.count > 0) minOf(b.min, s.value) else s.value
                    b.max = if (b.count > 0) maxOf(b.max, s.value) else s.value
                    b.sum += s.value
                    b.count++
                }

                Kind.RESTING_HEART_RATE -> {
                    val id = "hc:${s.id}"
                    if (id.length > MAX_EXTERNAL_ID) {
                        skipped += "resting heart rate ${s.id}: id too long"
                        continue
                    }
                    entries += entry(RESTING_ID, s.value, UNIT_PER_MINUTE, s.start, s.end, id)
                }

                Kind.SLEEP_STAGE -> {
                    val stage = hkSleepStage(s.sleepStage, s.sessionHasKnownStages)
                    if (stage < 0) {
                        skipped += "sleep ${s.id}: stage ${s.sleepStage} is no part of the night"
                        continue
                    }
                    val id = "hc:${s.id}:${s.start.toEpochMilli()}"
                    if (id.length > MAX_EXTERNAL_ID) {
                        skipped += "sleep ${s.id}: id too long"
                        continue
                    }
                    val minutes = (s.end.toEpochMilli() - s.start.toEpochMilli()) / 60000.0
                    entries += entry(SLEEP_ID, minutes, "category", s.start, s.end, id).put("sleepStage", stage)
                }
            }
        }

        addDays(entries, steps, STEPS_ID, "count", zone)
        addDays(entries, distance, DISTANCE_ID, "m", zone)

        for ((startMs, b) in buckets) {
            val start = Instant.ofEpochMilli(startMs)
            entries += entry(HEART_RATE_ID, b.sum / b.count, UNIT_PER_MINUTE, start, start.plusMillis(BUCKET_MS),
                "stats:$HEART_RATE_ID:${ISO.format(start)}")
                .put("valueMin", b.min)
                .put("valueMax", b.max)
        }
        return Result(entries, skipped)
    }

    /**
     * Health Connect sleep stage to HKCategoryValueSleepAnalysis (0 in bed, 1 asleep,
     * 2 awake, 3 core, 4 deep, 5 REM), or -1 when it is no part of the night.
     */
    fun hkSleepStage(healthConnectStage: Int, sessionHasKnownStages: Boolean): Int = when (healthConnectStage) {
        0 -> if (sessionHasKnownStages) -1 else 1 // unknown
        1 -> 2                                     // awake
        2 -> 1                                     // sleeping
        4 -> 3                                     // light
        5 -> 4                                     // deep
        6 -> 5                                     // REM
        7 -> 2                                     // awake in bed
        else -> -1                                 // out of bed (3), future stages
    }

    /** The local day a sample counts for; the source re-reads whole days with it. */
    fun dayOf(t: Instant, zone: ZoneId): LocalDate = t.atZone(zone).toLocalDate()

    private fun addDays(entries: MutableList<JSONObject>, days: Map<LocalDate, Double>, id: String, unit: String, zone: ZoneId) {
        for ((day, total) in days) {
            val start = day.atStartOfDay(zone).toInstant()
            val end = day.plusDays(1).atStartOfDay(zone).toInstant()
            entries += entry(id, total, unit, start, end, "stats:$id:$day")
        }
    }

    private fun entry(id: String, value: Double, unit: String, start: Instant, end: Instant, externalId: String): JSONObject =
        JSONObject()
            .put("hkIdentifier", id)
            .put("value", value)
            .put("unit", unit)
            .put("startDate", ISO.format(start))
            .put("endDate", ISO.format(end))
            .put("externalId", externalId)
}
