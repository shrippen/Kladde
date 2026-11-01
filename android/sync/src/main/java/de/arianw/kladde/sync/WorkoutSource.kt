package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.DistanceRecord
import androidx.health.connect.client.records.ExerciseSessionRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.request.ChangesTokenRequest
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import org.json.JSONObject
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Reads the exercise sessions of every app (Freeletics, the Pebble app, Google Fit …) for
 * /api/workouts/batch, with a changes token of its own.
 *
 * Each run reads the sessions around the days that changed and since the last read (a
 * training held until it settled comes again, however late the next run), maps them
 * (WorkoutMapper) and adds heart rate, steps and distance of its time. Known gap: a deleted
 * session stays in HealthLog.
 */
class WorkoutSource(
    private val client: HealthConnectClient,
    private val order: List<String> = WorkoutMapper.DEFAULT_ORDER,
) {

    class Read(val entries: List<JSONObject>, val cursor: String)

    /** [since]: when the last read was, null for none. */
    suspend fun read(cursor: String?, since: Instant?, zone: ZoneId, backfillDays: Long, now: Instant): Read {
        val today = Mapper.dayOf(now, zone)
        val backfill = DayWindow.range(today.minusDays(backfillDays - 1), today)
        if (cursor == null) {
            return read(newToken(), backfill, null, zone, now)
        }

        var token: String = cursor
        val days = mutableSetOf<LocalDate>()
        while (true) {
            val response = client.getChanges(token)
            if (response.changesTokenExpired) {
                return read(newToken(), backfill, null, zone, now)
            }
            for (change in response.changes) {
                val s = (change as? UpsertionChange)?.record as? ExerciseSessionRecord ?: continue
                days += DayWindow.range(Mapper.dayOf(s.startTime, zone), Mapper.dayOf(s.endTime, zone))
            }
            token = response.nextChangesToken
            if (!response.hasMore) {
                break
            }
        }
        return read(token, days, since, zone, now)
    }

    // The token first: what changes while we read comes again next time (first write wins).
    private suspend fun newToken(): String = client.getChangesToken(ChangesTokenRequest(TYPES))

    private suspend fun read(token: String, days: Set<LocalDate>, since: Instant?, zone: ZoneId, now: Instant): Read {
        // What was held at the last read ended at most SETTLE before it.
        val held = Mapper.dayOf((since ?: now).minus(WorkoutMapper.SETTLE), zone)
        // A day early: a training over midnight, or one overlapping it, began the day before.
        val (start, _) = DayWindow.bounds((days + held).min().minusDays(1), zone)
        val sessions = readAll(ExerciseSessionRecord::class, start, now).map {
            Session(it.metadata.id, it.metadata.dataOrigin.packageName, it.exerciseType, it.title, it.startTime, it.endTime)
        }

        val workouts = Sessions.select(sessions, order, days, zone, now, WorkoutMapper.SETTLE, since)
        val entries = mutableListOf<JSONObject>()
        for (group in clusters(workouts)) {
            val from = group.minOf { it.start }
            val to = group.maxOf { it.end }
            val samples = STAT_TYPES.flatMap { type -> readAll(type, from, to).flatMap(HealthConnectSource::samplesOf) }
            entries += group.map { WorkoutMapper.entry(it, samples) }
        }
        return Read(entries, token)
    }

    /**
     * Trainings close in time, read together: few calls (Health Connect limits them), but
     * not the months between an old session a cloud sync rewrote and today's.
     */
    private fun clusters(workouts: List<SessionGroup>): List<List<SessionGroup>> {
        val out = mutableListOf<MutableList<SessionGroup>>()
        for (w in workouts.sortedBy { it.start }) {
            val last = out.lastOrNull()
            if (last != null && Duration.between(last.maxOf { it.end }, w.start) <= CLUSTER_GAP) {
                last += w
                continue
            }
            out += mutableListOf(w)
        }
        return out
    }

    /** Every app's records: the mapper picks. */
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

    companion object {
        private const val PAGE_SIZE = 5000
        private val CLUSTER_GAP: Duration = Duration.ofDays(1)

        val TYPES: Set<KClass<out Record>> = setOf(ExerciseSessionRecord::class)

        /** What a training's figures come from; read with HealthConnectSource's permissions. */
        private val STAT_TYPES: List<KClass<out Record>> = listOf(HeartRateRecord::class, StepsRecord::class, DistanceRecord::class)
    }
}
