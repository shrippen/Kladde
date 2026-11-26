package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.request.ChangesTokenRequest
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Reads the sleep of every app (the Pebble app, Sleep as Android, Google Fit …) with a
 * changes token of its own. Where two apps saw the same night, the one first in [ranking]
 * stays (Sessions.select); a night goes once it ended [SETTLE] ago, so the watch's night,
 * synced later than the phone's, still wins.
 *
 * Each run reads the sessions around the days that changed and since the last read. Known
 * gaps: HealthLog keeps a stage's first write, so a night changed or deleted later stays as
 * it was; a night of a later app that loses to one already sent is not taken back.
 */
class SleepSource(
    private val client: HealthConnectClient,
    private val ranking: Ranking = Ranking(DEFAULT_ORDER),
) {

    private val reader = HealthReader(client)

    class Read(val samples: List<Sample>, val cursor: String)

    /** [since]: when the last read was, null for none. */
    suspend fun read(cursor: String?, since: Instant?, zone: ZoneId, backfillDays: Long, now: Instant): Read {
        val today = Mapper.dayOf(now, zone)
        val backfill = DayWindow.range(today.minusDays(backfillDays - 1), today)
        if (cursor == null) {
            return read(newToken(), backfill, null, zone, now)
        }

        val changes = reader.changesSince(cursor) ?: return read(newToken(), backfill, null, zone, now)
        val days = mutableSetOf<LocalDate>()
        for (change in changes.list) {
            val s = (change as? UpsertionChange)?.record as? SleepSessionRecord ?: continue
            days += DayWindow.range(Mapper.dayOf(s.startTime, zone), Mapper.dayOf(s.endTime, zone))
        }
        return read(changes.next, days, since, zone, now)
    }

    // The token first: what changes while we read comes again next time (first write wins).
    private suspend fun newToken(): String = client.getChangesToken(ChangesTokenRequest(TYPES))

    private suspend fun read(token: String, days: Set<LocalDate>, since: Instant?, zone: ZoneId, now: Instant): Read {
        // What was held at the last read ended at most SETTLE before it.
        val held = Mapper.dayOf((since ?: now).minus(SETTLE), zone)
        // A day early: a night begins the evening before.
        val (start, _) = DayWindow.bounds((days + held).min().minusDays(1), zone)
        val records = reader.all(SleepSessionRecord::class, start, now).associateBy { it.metadata.id }
        val sessions = records.values.map {
            Session(it.metadata.id, it.metadata.dataOrigin.packageName, NO_TYPE, it.title, it.startTime, it.endTime)
        }

        val nights = Sessions.select(sessions, ranking, days, zone, now, SETTLE, since)
        val samples = nights.flatMap { night -> night.sessions.flatMap { RecordSamples.samplesOf(records.getValue(it.id)) } }
        return Read(samples, token)
    }

    /** The apps that wrote sleep from [start] on: the ones the user may put in order. */
    suspend fun apps(start: Instant, now: Instant): Set<String> =
        reader.all(SleepSessionRecord::class, start, now).map { it.metadata.dataOrigin.packageName }.toSet()

    companion object {
        private const val NO_TYPE = 0 // sleep has no sport

        const val SLEEP_AS_ANDROID_APP = "com.urbandroid.sleep"

        /** Owner, 1.11.2026: the Pebble app, then Sleep as Android, then Google Fit. */
        val DEFAULT_ORDER = listOf(RecordSamples.PEBBLE_APP, SLEEP_AS_ANDROID_APP, WorkoutMapper.FIT_APP)

        /** The Pebble app syncs the night from the watch later than the phone writes its own. */
        val SETTLE: Duration = Duration.ofHours(4)

        val TYPES: Set<KClass<out Record>> = setOf(SleepSessionRecord::class)
    }
}
