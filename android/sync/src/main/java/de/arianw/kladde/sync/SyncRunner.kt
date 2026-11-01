package de.arianw.kladde.sync

import kotlinx.coroutines.CancellationException
import java.time.Duration
import java.time.Instant
import java.time.ZoneId

/**
 * One sync run: read the source, map, queue (with the new cursor, in one transaction),
 * upload, record the status.
 *
 *   Health Connect ──read──▶ samples ──Mapper──▶ batches ──QueueStore──▶ Uploader ──▶ HealthLog
 *                     cursor ───────────────────────────────┘ (same transaction)
 *                 └──read──▶ trainings (WorkoutSource, own cursor) ──▶ workout batches ┘
 */
class SyncRunner(
    private val store: QueueStore,
    private val transport: Transport,
    private val zone: ZoneId = ZoneId.systemDefault(),
) {

    /** Reads new data; the source is a function so tests and other sources can stand in. */
    fun interface Reader {
        suspend fun read(cursor: String?, zone: ZoneId, now: Instant): HealthConnectSource.Read
    }

    fun interface WorkoutReader {
        suspend fun read(cursor: String?, since: Instant?, zone: ZoneId, now: Instant): WorkoutSource.Read
    }

    /** [workouts] null: not read (no permission for exercise yet). */
    suspend fun run(reader: Reader, trigger: Trigger, now: Instant = Instant.now(), workouts: WorkoutReader? = null): SyncStatus {
        val before = store.status()
        var error: String? = null

        try {
            val read = reader.read(store.cursor(), zone, now)
            val mapped = Mapper.map(read.samples, zone)
            val seen = read.samples.groupBy { it.kind }.mapValues { (_, list) -> list.maxOf { it.end } }
            store.enqueue(Batches.make(mapped.entries, trigger), read.cursor, read.recordDays, seen)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            error = describe(e)
        }

        // Apart from the measurements: a failing training read must not hold them back.
        try {
            workouts?.read(store.workoutCursor(), store.workoutReadAt(), zone, now)?.let {
                store.enqueueWorkouts(Batches.make(it.entries, trigger, Target.WORKOUTS), it.cursor, now)
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            error = error ?: describe(e)
        }

        val upload = Uploader(store, transport).flush(now)
        val ok = error == null && upload.outcome == Uploader.Outcome.EMPTY && upload.error == null
        val status = SyncStatus(
            lastRun = now,
            lastSuccess = if (ok) now else before.lastSuccess,
            lastError = error ?: upload.error,
            uploaded = upload.uploaded,
        )
        store.setStatus(status)
        return status
    }

    /**
     * Permission withdrawn (SecurityException), or Health Connect busy or rate-limited
     * (IllegalStateException, RemoteException …): the cursor did not move, the next run reads
     * again; what is queued is uploaded anyway.
     */
    private fun describe(e: Exception): String {
        if (e is SecurityException) {
            return "Health Connect: ${e.message ?: "no permission"}"
        }
        return "Health Connect: ${e.javaClass.simpleName}: ${e.message ?: ""}".trimEnd(' ', ':')
    }

    companion object {
        /** A kind that brought nothing for this long while others did is worth a warning (LF-24). */
        val SILENT_AFTER: Duration = Duration.ofHours(48)

        /** Kinds that went silent while at least one other kind still delivers. */
        fun silentKinds(lastSeen: Map<Kind, Instant>): Set<Kind> {
            val newest = lastSeen.values.maxOrNull() ?: return emptySet()
            return lastSeen.filterValues { Duration.between(it, newest) > SILENT_AFTER }.keys
        }
    }
}
