package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.changes.DeletionChange
import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.DistanceRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.RestingHeartRateRecord
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.metadata.DataOrigin
import androidx.health.connect.client.request.ChangesTokenRequest
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Reads what one app (the Pebble app by default) wrote to Health Connect (LF-10 … LF-15).
 *
 * First run: a changes token, then every record of the backfill window. Later runs: the
 * changes since the token. Sleep and resting heart rate go as they are; for steps,
 * distance and heart rate the whole local day of every change is re-read (DayWindow),
 * deletions included, so the day total and buckets come out right.
 */
class HealthConnectSource(
    private val client: HealthConnectClient,
    private val app: String = PEBBLE_APP,
) {

    /** What one read brought, for the store to keep in one transaction. */
    class Read(
        val samples: List<Sample>,
        val cursor: String,
        val recordDays: Map<String, LocalDate>,
    )

    suspend fun read(cursor: String?, zone: ZoneId, backfillDays: Long, dayOf: (String) -> LocalDate?, now: Instant): Read {
        if (cursor == null) {
            return backfill(zone, backfillDays, now)
        }

        var token: String = cursor
        val samples = mutableListOf<Sample>()
        val days = mutableSetOf<LocalDate>()

        while (true) {
            val response = client.getChanges(token)
            if (response.changesTokenExpired) {
                // Older than Health Connect keeps changes (30 days): read the window again.
                return backfill(zone, backfillDays, now)
            }
            for (change in response.changes) {
                when (change) {
                    is UpsertionChange -> collect(change.record, zone, samples, days)
                    is DeletionChange -> dayOf(change.recordId)?.let { days += it }
                }
            }
            token = response.nextChangesToken
            if (!response.hasMore) {
                break
            }
        }

        val dayRead = readDays(days, zone)
        return Read(samples + dayRead.first, token, dayRead.second)
    }

    private suspend fun backfill(zone: ZoneId, backfillDays: Long, now: Instant): Read {
        // The token first: what changes while we read comes again next time (the server dedups).
        val token = client.getChangesToken(ChangesTokenRequest(TYPES, setOf(DataOrigin(app))))
        val today = Mapper.dayOf(now, zone)
        val first = today.minusDays(backfillDays - 1)
        val (start, _) = DayWindow.bounds(first, zone)

        val samples = mutableListOf<Sample>()
        for (type in RECORD_TYPES) {
            for (r in readAll(type, start, now)) {
                samples += samplesOf(r)
            }
        }
        val dayRead = readDays(DayWindow.range(first, today), zone)
        return Read(samples + dayRead.first, token, dayRead.second)
    }

    /** Sleep and resting heart rate as they are; the day of every day-kind record. */
    private fun collect(record: Record, zone: ZoneId, samples: MutableList<Sample>, days: MutableSet<LocalDate>) {
        when (record) {
            is SleepSessionRecord, is RestingHeartRateRecord -> samples += samplesOf(record)
            is StepsRecord -> days += Mapper.dayOf(record.startTime, zone)
            is DistanceRecord -> days += Mapper.dayOf(record.startTime, zone)
            is HeartRateRecord -> {
                days += Mapper.dayOf(record.startTime, zone)
                days += Mapper.dayOf(record.endTime, zone)
            }
        }
    }

    /** Every day-kind record of [days], cut to those days (DayWindow), with each record's day. */
    private suspend fun readDays(days: Set<LocalDate>, zone: ZoneId): Pair<List<Sample>, Map<String, LocalDate>> {
        val samples = mutableListOf<Sample>()
        val recordDays = mutableMapOf<String, LocalDate>()
        for (day in days.sorted()) {
            val (start, end) = DayWindow.bounds(day, zone)
            for (type in DAY_TYPES) {
                for (r in readAll(type, start, end)) {
                    samples += samplesOf(r)
                    recordDays[r.metadata.id] = day
                }
            }
        }
        return DayWindow.keep(samples, days, zone) to recordDays
    }

    private suspend fun <T : Record> readAll(type: KClass<T>, start: Instant, end: Instant): List<T> {
        val out = mutableListOf<T>()
        var page: String? = null
        do {
            val response = client.readRecords(
                ReadRecordsRequest(type, TimeRangeFilter.between(start, end), setOf(DataOrigin(app)),
                    pageSize = PAGE_SIZE, pageToken = page)
            )
            out += response.records
            page = response.pageToken
        } while (page != null)
        return out
    }

    companion object {
        const val PEBBLE_APP = "coredevices.coreapp"
        private const val PAGE_SIZE = 5000
        private const val STAGE_UNKNOWN = 0
        private const val STAGE_OUT_OF_BED = 3

        /** Sent as they are. */
        private val RECORD_TYPES: List<KClass<out Record>> = listOf(SleepSessionRecord::class, RestingHeartRateRecord::class)

        /** Sent as day totals or buckets: re-read by whole days (DayWindow.DAY_KINDS). */
        private val DAY_TYPES: List<KClass<out Record>> = listOf(StepsRecord::class, DistanceRecord::class, HeartRateRecord::class)

        val TYPES: Set<KClass<out Record>> = setOf(
            StepsRecord::class,
            DistanceRecord::class,
            HeartRateRecord::class,
            RestingHeartRateRecord::class,
            SleepSessionRecord::class,
        )

        fun samplesOf(r: Record): List<Sample> {
            val id = r.metadata.id
            val app = r.metadata.dataOrigin.packageName
            return when (r) {
                is StepsRecord -> listOf(Sample(Kind.STEPS, id, app, r.startTime, r.endTime, r.count.toDouble()))
                is DistanceRecord -> listOf(Sample(Kind.DISTANCE, id, app, r.startTime, r.endTime, r.distance.inMeters))
                is HeartRateRecord -> r.samples.map {
                    Sample(Kind.HEART_RATE, id, app, it.time, it.time, it.beatsPerMinute.toDouble())
                }
                is RestingHeartRateRecord -> listOf(
                    Sample(Kind.RESTING_HEART_RATE, id, app, r.time, r.time, r.beatsPerMinute.toDouble())
                )
                is SleepSessionRecord -> sleepSamples(r, id, app)
                else -> emptyList()
            }
        }

        private fun sleepSamples(r: SleepSessionRecord, id: String, app: String): List<Sample> {
            val known = r.stages.any { it.stage != STAGE_UNKNOWN && it.stage != STAGE_OUT_OF_BED }
            if (r.stages.isEmpty()) {
                // A session without stages is one block of plain sleep.
                return listOf(Sample(Kind.SLEEP_STAGE, id, app, r.startTime, r.endTime, 0.0, STAGE_UNKNOWN, false))
            }
            return r.stages.filter { it.endTime.isAfter(it.startTime) }.map {
                Sample(Kind.SLEEP_STAGE, id, app, it.startTime, it.endTime, 0.0, it.stage, known)
            }
        }
    }
}
