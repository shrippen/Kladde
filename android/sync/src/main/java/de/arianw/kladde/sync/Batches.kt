package de.arianw.kladde.sync

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * One request body for POST /api/measurements/batch. The Idempotency-Key is new for every
 * batch put in the queue and stored with it: a resend after a lost answer is a replay the
 * server recognises (24 h window). Not a hash of the body: the server answers the same key
 * from its memory for 24 h, so a day total that goes back to an earlier value (1000, 1100,
 * 1000 steps) would never be written.
 */
class Batch(val body: String, val key: String, val size: Int)

/** What triggered a sync; HealthLog records it for diagnostics only. */
enum class Trigger(val wire: String) {
    FOREGROUND("foreground"),
    BACKGROUND("background"),
    MANUAL("manual"),
}

object Batches {

    const val MAX_ENTRIES = 500 // HealthLog's limit

    fun make(entries: List<JSONObject>, trigger: Trigger, max: Int = MAX_ENTRIES): List<Batch> =
        entries.chunked(max).map { chunk ->
            val body = JSONObject()
                .put("entries", JSONArray(chunk))
                .put("syncTrigger", trigger.wire)
                .toString()
            Batch(body, newKey(), chunk.size)
        }

    /** The same batch without the entries at [drop] (indices into its entries). */
    fun without(batch: Batch, drop: Set<Int>): Batch? {
        val root = JSONObject(batch.body)
        val entries = root.getJSONArray("entries")
        val kept = JSONArray()
        for (i in 0 until entries.length()) {
            if (i !in drop) {
                kept.put(entries.get(i))
            }
        }
        if (kept.length() == 0) {
            return null
        }
        val body = root.put("entries", kept).toString()
        return Batch(body, newKey(), kept.length())
    }

    private fun newKey(): String = UUID.randomUUID().toString()
}
