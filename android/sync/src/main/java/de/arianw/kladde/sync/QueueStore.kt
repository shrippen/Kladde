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
     * Workout batches, the workout reader's cursor and when it read ([readAt]), in one
     * transaction like [enqueue].
     */
    fun enqueueWorkouts(batches: List<Batch>, cursor: String, readAt: Instant)

    /**
     * The oldest batch if it is due at [now], else null: the queue goes strictly in order,
     * a newer day total must not overtake an older one in backoff.
     */
    fun next(now: Instant): Pending?

    fun done(key: String)

    fun retryAt(key: String, at: Instant)

    /**
     * Swap a batch for a smaller one in its place in the queue (null: drop it). [keepAttempts]
     * keeps the backoff growing (entries the server keeps failing), else it starts over.
     */
    fun replace(key: String, with: Batch?, keepAttempts: Boolean = false)

    /** Reading batches, the reading source's cursor and what came, in one transaction. */
    fun enqueueReadings(batches: List<Batch>, cursor: String, seen: Map<Kind, Instant>)

    fun cursor(): String?

    fun readingCursor(): String?

    fun workoutCursor(): String?

    /** When the workouts were last read; trainings held then are due later. */
    fun workoutReadAt(): Instant?

    /** Every cursor: the next run reads everything again. */
    fun resetCursor()

    /** The days a record went into, for re-reading them when it is deleted. */
    fun daysOf(recordId: String): Set<LocalDate>

    fun pendingCount(): Int

    /** When each kind last brought data; the status page warns about a silent kind (LF-24). */
    fun lastSeen(): Map<Kind, Instant>

    fun setStatus(status: SyncStatus)

    fun status(): SyncStatus

    companion object {
        /**
         * A batch in backoff is due at [due]. One due more than the longest backoff ahead was
         * set by a clock that has since gone back (wrong date set by hand): due now, or the
         * strictly ordered queue would wait days behind it.
         */
        fun isDue(due: Instant, now: Instant): Boolean =
            !due.isAfter(now) || due.isAfter(now.plus(Uploader.LAST_RETRY))
    }
}

/** The last run as the status page shows it. */
data class SyncStatus(
    val lastRun: Instant? = null,
    val lastSuccess: Instant? = null,
    val lastError: String? = null,
    val uploaded: Int = 0,
)
