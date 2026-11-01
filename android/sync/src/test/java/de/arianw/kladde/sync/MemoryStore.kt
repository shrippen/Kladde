package de.arianw.kladde.sync

import java.time.Instant
import java.time.LocalDate

/** QueueStore in memory for the tests; same contract as SqliteQueueStore. */
class MemoryStore : QueueStore {

    private class Row(val batch: Batch, val order: Long, var attempts: Int = 0, var due: Instant = Instant.EPOCH)

    private val rows = linkedMapOf<String, Row>()
    private var order = 0L
    private var cursor: String? = null
    private val days = mutableMapOf<String, Set<LocalDate>>()
    private val seen = mutableMapOf<Kind, Instant>()
    private var status = SyncStatus()

    val keys: List<String> get() = rows.values.sortedBy { it.order }.map { it.batch.key }

    fun batch(key: String): Batch? = rows[key]?.batch

    override fun enqueue(batches: List<Batch>, cursor: String?, recordDays: Map<String, Set<LocalDate>>, seen: Map<Kind, Instant>) {
        for (b in batches) {
            rows.putIfAbsent(b.key, Row(b, order++))
        }
        days.putAll(recordDays)
        for ((k, t) in seen) {
            this.seen[k] = maxOf(this.seen[k] ?: t, t)
        }
        if (cursor != null) {
            this.cursor = cursor
        }
    }

    override fun next(now: Instant): Pending? =
        rows.values.minByOrNull { it.order }?.takeIf { !it.due.isAfter(now) }?.let { Pending(it.batch, it.attempts) }

    override fun done(key: String) {
        rows.remove(key)
    }

    override fun retryAt(key: String, at: Instant) {
        rows[key]?.let {
            it.attempts++
            it.due = at
        }
    }

    override fun replace(key: String, with: Batch?) {
        val old = rows.remove(key) ?: return
        if (with != null) {
            rows[with.key] = Row(with, old.order)
        }
    }

    override fun cursor(): String? = cursor

    override fun resetCursor() {
        cursor = null
    }

    override fun daysOf(recordId: String): Set<LocalDate> = days[recordId].orEmpty()

    override fun pendingCount(): Int = rows.values.sumOf { it.batch.size }

    override fun lastSeen(): Map<Kind, Instant> = seen.toMap()

    override fun setStatus(status: SyncStatus) {
        this.status = status
    }

    override fun status(): SyncStatus = status
}
