package de.arianw.kladde.sync

import java.time.Instant
import java.time.LocalDate

/** A batch waiting for upload. */
class Pending(val batch: Batch, val attempts: Int)

/**
 * The durable side of the sync: batches waiting for upload, the source's cursor, which days
 * each day-total record went into, and what the status page shows.
 *
 * [enqueue] stores batches and the new cursor in one transaction: once the cursor moved,
 * the data is either uploaded or still in the queue, never lost (LF-17, LF-23).
 */
interface QueueStore {

    fun enqueue(batches: List<Batch>, cursor: String?, recordDays: Map<String, Set<LocalDate>>, seen: Map<Kind, Instant>)

    /**
     * The oldest batch if it is due at [now], else null: the queue goes strictly in order,
     * a newer day total must not overtake an older one in backoff.
     */
    fun next(now: Instant): Pending?

    fun done(key: String)

    fun retryAt(key: String, at: Instant)

    /** Swap a batch for a smaller one in its place in the queue (null: drop it). */
    fun replace(key: String, with: Batch?)

    fun cursor(): String?

    fun resetCursor()

    /** The days a record went into, for re-reading them when it is deleted. */
    fun daysOf(recordId: String): Set<LocalDate>

    fun pendingCount(): Int

    /** When each kind last brought data; the status page warns about a silent kind (LF-24). */
    fun lastSeen(): Map<Kind, Instant>

    fun setStatus(status: SyncStatus)

    fun status(): SyncStatus
}

/** The last run as the status page shows it. */
data class SyncStatus(
    val lastRun: Instant? = null,
    val lastSuccess: Instant? = null,
    val lastError: String? = null,
    val uploaded: Int = 0,
)
