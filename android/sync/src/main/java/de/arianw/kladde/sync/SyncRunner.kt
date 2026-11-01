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

    suspend fun run(reader: Reader, trigger: Trigger, now: Instant = Instant.now()): SyncStatus {
        val before = store.status()
        var error: String? = null

        try {
            val read = reader.read(store.cursor(), zone, now)
            val mapped = Mapper.map(read.samples, zone)
            val seen = read.samples.groupBy { it.kind }.mapValues { (_, list) -> list.maxOf { it.end } }
            store.enqueue(Batches.make(mapped.entries, trigger), read.cursor, read.recordDays, seen)
        } catch (e: CancellationException) {
            throw e
        } catch (e: SecurityException) {
            // Permission withdrawn in Health Connect: upload what is queued, report it.
            error = "Health Connect: ${e.message ?: "no permission"}"
        } catch (e: Exception) {
            // Health Connect busy or rate-limited (IllegalStateException, RemoteException …):
            // the cursor did not move, the next run reads again; upload what is queued.
            error = "Health Connect: ${e.javaClass.simpleName}: ${e.message ?: ""}".trimEnd(' ', ':')
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
