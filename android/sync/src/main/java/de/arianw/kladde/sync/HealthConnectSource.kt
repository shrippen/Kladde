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
        val recordDays: Map<String, Set<LocalDate>>,
    )

    suspend fun read(cursor: String?, zone: ZoneId, backfillDays: Long, daysOf: (String) -> Set<LocalDate>, now: Instant): Read {
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
                    is UpsertionChange -> collect(change.record, zone, samples, days, daysOf)
                    is DeletionChange -> days += daysOf(change.recordId)
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

    /**
     * Sleep and resting heart rate as they are; for a day-kind record its days now and the
     * days it went into before (a record rewritten to another time leaves its old day).
     */
    private fun collect(
        record: Record,
        zone: ZoneId,
        samples: MutableList<Sample>,
        days: MutableSet<LocalDate>,
        daysBefore: (String) -> Set<LocalDate>,
    ) {
        when (record) {
            is SleepSessionRecord, is RestingHeartRateRecord -> samples += samplesOf(record)
            else -> days += daysOf(record, zone) + daysBefore(record.metadata.id)
        }
    }

    /**
     * Every day-kind record of [days], cut to those days (DayWindow), with each record's days.
     *
     * Known gap: a day or 10-minute bucket whose records were all deleted brings no sample,
     * so no entry, and the server keeps its old total. Sending 0 would also write zeros for
     * days the watch was simply off; deleting by external id needs a session token.
     */
    private suspend fun readDays(days: Set<LocalDate>, zone: ZoneId): Pair<List<Sample>, Map<String, Set<LocalDate>>> {
        // By id: a record over midnight comes back for both days and must count once.
        val records = linkedMapOf<String, Record>()
        for (day in days.sorted()) {
            val (start, end) = DayWindow.bounds(day, zone)
            for (type in DAY_TYPES) {
                val kind = if (type == HeartRateRecord::class) Kind.HEART_RATE else Kind.STEPS
                for (r in readAll(type, start, DayWindow.readEnd(kind, end))) {
                    records[r.metadata.id] = r
                }
            }
        }
        val samples = records.values.flatMap { samplesOf(it) }
        // Not the day being read: the days the Mapper counts the record into.
        val recordDays = records.mapValues { (_, r) -> daysOf(r, zone) }
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
        /**
         * The days whose totals or buckets a day-kind record goes into, as the Mapper counts
         * them: steps and distance by their start, heart rate by each sample, so every day
         * from the bucket of its start to the bucket of its end. A deletion re-reads these days.
         */
        fun daysOf(record: Record, zone: ZoneId): Set<LocalDate> = when (record) {
            is StepsRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is DistanceRecord -> setOf(Mapper.dayOf(record.startTime, zone))
            is HeartRateRecord -> DayWindow.range(
                Mapper.dayOf(Mapper.bucketStart(record.startTime), zone),
                Mapper.dayOf(Mapper.bucketStart(record.endTime), zone),
            )
            else -> emptySet()
        }

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
