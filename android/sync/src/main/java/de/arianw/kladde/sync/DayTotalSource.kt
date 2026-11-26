package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.aggregate.AggregateMetric
import androidx.health.connect.client.aggregate.AggregationResult
import androidx.health.connect.client.changes.DeletionChange
import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.ActiveCaloriesBurnedRecord
import androidx.health.connect.client.records.DistanceRecord
import androidx.health.connect.client.records.FloorsClimbedRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.request.AggregateGroupByDurationRequest
import androidx.health.connect.client.request.AggregateGroupByPeriodRequest
import androidx.health.connect.client.request.AggregateRequest
import androidx.health.connect.client.request.ChangesTokenRequest
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.Period
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Reads steps, distance, active energy, flights and heart rate of every app from Health
 * Connect (LF-10 … LF-15); only the [types] the user allowed. Sleep: SleepSource.
 *
 * Day totals and the 10-minute heart-rate buckets come from Health Connect's aggregate: it
 * takes each app by the order in its own settings, so the phone's and the watch's steps
 * never count twice. Every day a change touches is aggregated again, deletions included:
 * the first read notes the days of every record in its window, a later change those of
 * its record. A deleted record Kladde never saw (older than the window) re-aggregates today
 * and yesterday.
 *
 * First run: a changes token, then the backfill window. The cursor holds the types it was
 * made for (as ReadingSource): allowing another one reads the window again.
 */
class DayTotalSource(
    private val client: HealthConnectClient,
    private val types: Set<KClass<out Record>> = TYPES + OPTIONAL_TYPES,
) {

    /** What one read brought, for the store to keep in one transaction. */
    class Read(
        val samples: List<Sample>,
        val cursor: String?, // null: unchanged
        val recordDays: Map<String, Set<LocalDate>>,
    )

    // The types and the mapping it was made for: 0.2.0 dated day totals a day late, so its
    // cursor (no MAPPING) reads again and the server moves them to their day (migrate).
    private val signature = types.map { it.simpleName }.sortedBy { it }.joinToString(",") + MAPPING

    suspend fun read(cursor: String?, zone: ZoneId, backfillDays: Long, daysOf: (String) -> Set<LocalDate>, now: Instant): Read {
        if (types.isEmpty()) {
            return Read(emptyList(), null, emptyMap())
        }
        val token = cursor?.removePrefix("$signature$SEPARATOR")?.takeIf { it != cursor }
        if (token == null && cursor != null && MAPPING !in cursor) {
            return migrate(zone, backfillDays, now)
        }
        if (token == null) {
            return backfill(zone, backfillDays, now)
        }

        var next: String = token
        val days = mutableSetOf<LocalDate>()
        val recordDays = mutableMapOf<String, Set<LocalDate>>()
        val today = Mapper.dayOf(now, zone)

        while (true) {
            val response = client.getChanges(next)
            if (response.changesTokenExpired) {
                // Older than Health Connect keeps changes (30 days): read the window again.
                return backfill(zone, backfillDays, now)
            }
            for (change in response.changes) {
                if (change is DeletionChange) {
                    days += daysOf(change.recordId).ifEmpty { setOf(today, today.minusDays(1)) }
                    continue
                }
                val record = (change as? UpsertionChange)?.record ?: continue
                // Its days now and before: a record rewritten to another time leaves its old day.
                val current = daysOf(record, zone)
                days += current + daysOf(record.metadata.id)
                recordDays[record.metadata.id] = current
            }
            next = response.nextChangesToken
            if (!response.hasMore) {
                break
            }
        }
        return Read(aggregate(days, zone, now), cursorOf(next), recordDays)
    }

    /**
     * 0.1/0.2 dated day totals a day late. The window as always, and the totals of every day
     * before it they can have sent: a resend moves a total onto the time the old total of the
     * day before holds, and HealthLog retires that row, so the chain must end on a day without
     * one. Heart rate was dated right and stays out; one call per [MIGRATION_CHUNK] days.
     */
    private suspend fun migrate(zone: ZoneId, backfillDays: Long, now: Instant): Read {
        val window = backfill(zone, backfillDays, now)
        val windowFirst = Mapper.dayOf(now, zone).minusDays(backfillDays - 1)
        val totals = TOTALS.filter { it.type in types }
        val samples = mutableListOf<Sample>()

        var from = MIGRATION_FIRST
        while (totals.isNotEmpty() && from.isBefore(windowFirst)) {
            val until = minOf(from.plusDays(MIGRATION_CHUNK), windowFirst)
            val days = client.aggregateGroupByPeriod(AggregateGroupByPeriodRequest(
                totals.map { it.metric }.toSet(),
                TimeRangeFilter.between(from.atStartOfDay(), until.atStartOfDay()),
                Period.ofDays(1),
            ))
            for (d in days) {
                val day = d.startTime.toLocalDate()
                val (start, end) = DayWindow.bounds(day, zone)
                samples += totalSamples(totals, d.result, day, start, end)
            }
            from = until
        }
        return Read(samples + window.samples, window.cursor, window.recordDays)
    }

    private suspend fun backfill(zone: ZoneId, backfillDays: Long, now: Instant): Read {
        // The token first: what changes while we read comes again next time (the server dedups).
        val token = client.getChangesToken(ChangesTokenRequest(types))
        val today = Mapper.dayOf(now, zone)
        val first = today.minusDays(backfillDays - 1)
        val (start, _) = DayWindow.bounds(first, zone)

        // Each record's days, so its deletion later re-aggregates them.
        val recordDays = mutableMapOf<String, Set<LocalDate>>()
        for (type in types) {
            for (r in readAll(type, start, now)) {
                recordDays[r.metadata.id] = daysOf(r, zone)
            }
        }
        return Read(aggregate(DayWindow.range(first, today), zone, now), cursorOf(token), recordDays)
    }

    /**
     * Each day's totals and 10-minute heart-rate buckets as Health Connect aggregates them,
     * one sample each, cut to [days] (DayWindow).
     *
     * Known gap: a day or bucket whose records were all deleted brings no sample, so no
     * entry, and the server keeps its old total; sending 0 would also write zeros for days
     * the watch was simply off.
     */
    private suspend fun aggregate(days: Set<LocalDate>, zone: ZoneId, now: Instant): List<Sample> {
        val totals = TOTALS.filter { it.type in types }
        val samples = mutableListOf<Sample>()

        for (day in days.sorted()) {
            val (start, end) = DayWindow.bounds(day, zone)

            if (totals.isNotEmpty()) {
                val result = client.aggregate(AggregateRequest(totals.map { it.metric }.toSet(), TimeRangeFilter.between(start, end)))
                samples += totalSamples(totals, result, day, start, minOf(end, now))
            }

            if (HeartRateRecord::class !in types) {
                continue
            }
            val from = Mapper.bucketStart(start)
            val buckets = client.aggregateGroupByDuration(AggregateGroupByDurationRequest(
                setOf(HeartRateRecord.BPM_AVG, HeartRateRecord.BPM_MIN, HeartRateRecord.BPM_MAX),
                TimeRangeFilter.between(from, DayWindow.readEnd(Kind.HEART_RATE, end)),
                BUCKET,
            ))
            for (b in buckets) {
                val avg = b.result[HeartRateRecord.BPM_AVG] ?: continue
                samples += Sample(Kind.HEART_RATE, "hr:${b.startTime}", AGGREGATE, b.startTime, b.startTime, avg.toDouble(),
                    min = b.result[HeartRateRecord.BPM_MIN]?.toDouble(), max = b.result[HeartRateRecord.BPM_MAX]?.toDouble())
            }
        }
        return DayWindow.keep(samples, days, zone)
    }

    // One day's total samples from an aggregate result; [until] ends today's total at now.
    private fun totalSamples(totals: List<Total>, result: AggregationResult, day: LocalDate, start: Instant, until: Instant): List<Sample> =
        totals.mapNotNull { t ->
            t.value(result)?.let { Sample(t.kind, "day:$day", AGGREGATE, start, until, it) }
        }

    private suspend fun <T : Record> readAll(type: KClass<T>, start: Instant, end: Instant): List<T> {
        val out = mutableListOf<T>()
        var page: String? = null
        do {
            val response = client.readRecords(
                ReadRecordsRequest(type, TimeRangeFilter.between(start, end), pageSize = PAGE_SIZE, pageToken = page)
            )
            out += response.records
            page = response.pageToken
        } while (page != null)
        return out
    }

    private fun cursorOf(token: String) = "$signature$SEPARATOR$token"

    /** A day total: what Health Connect sums, and how its result reads as the sample's unit. */
    private class Total(val type: KClass<out Record>, val kind: Kind, val metric: AggregateMetric<*>, val value: (AggregationResult) -> Double?)

    companion object {
        /**
         * The days whose totals or buckets a day-kind record goes into, as the Mapper counts
         * them: steps and distance by their start, heart rate by each sample, so every day
         * from the bucket of its start to the bucket of its end. A deletion re-reads these days.
         */
        fun daysOf(record: Record, zone: ZoneId): Set<LocalDate> = when (record) {
            is StepsRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is DistanceRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is ActiveCaloriesBurnedRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is FloorsClimbedRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is HeartRateRecord -> DayWindow.range(
                Mapper.dayOf(Mapper.bucketStart(record.startTime), zone),
                Mapper.dayOf(Mapper.bucketStart(record.endTime), zone),
            )
            else -> emptySet()
        }

        private const val PAGE_SIZE = 5000

        private const val SEPARATOR = "|"
        private const val MAPPING = ";day-at-start"
        // A day before the earliest 0.1/0.2 can have read: 0.1.0 (1 November 2026) minus the
        // largest window the setting allows (365 days, MoreView).
        private val MIGRATION_FIRST: LocalDate = LocalDate.of(2026, 11, 1).minusDays(365)
        private const val MIGRATION_CHUNK = 31L
        private const val AGGREGATE = "health-connect" // the app of a sample Health Connect summed
        private val BUCKET: Duration = Duration.ofMinutes(10)

        private val TOTALS = listOf(
            Total(StepsRecord::class, Kind.STEPS, StepsRecord.COUNT_TOTAL) { it[StepsRecord.COUNT_TOTAL]?.toDouble() },
            Total(DistanceRecord::class, Kind.DISTANCE, DistanceRecord.DISTANCE_TOTAL) { it[DistanceRecord.DISTANCE_TOTAL]?.inMeters },
            Total(ActiveCaloriesBurnedRecord::class, Kind.ACTIVE_ENERGY, ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL) {
                it[ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL]?.inKilocalories
            },
            Total(FloorsClimbedRecord::class, Kind.FLIGHTS_CLIMBED, FloorsClimbedRecord.FLOORS_CLIMBED_TOTAL) {
                it[FloorsClimbedRecord.FLOORS_CLIMBED_TOTAL]
            },
        )

        /** What the watch writes: required (KladdeSync). */
        val TYPES: Set<KClass<out Record>> = setOf(
            StepsRecord::class,
            DistanceRecord::class,
            HeartRateRecord::class,
        )

        /** Day totals read once allowed. */
        val OPTIONAL_TYPES: Set<KClass<out Record>> = setOf(
            ActiveCaloriesBurnedRecord::class,
            FloorsClimbedRecord::class,
        )
    }
}
