package de.arianw.kladde.sync

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * One request body for the [target]'s batch endpoint. The Idempotency-Key is new for every
 * batch put in the queue and stored with it: a resend after a lost answer is a replay the
 * server recognises (24 h window). Not a hash of the body: the server answers the same key
 * from its memory for 24 h, so a day total that goes back to an earlier value (1000, 1100,
 * 1000 steps) would never be written.
 */
class Batch(val body: String, val key: String, val size: Int, val target: Target = Target.MEASUREMENTS)

/** A batch endpoint: its path, the array the entries go in, HealthLog's limit per call. */
enum class Target(val path: String, val key: String, val max: Int) {
    MEASUREMENTS("/api/measurements/batch", "entries", 500),
    WORKOUTS("/api/workouts/batch", "workouts", 100),
}

/** What triggered a sync; HealthLog records it for diagnostics only. */
enum class Trigger(val wire: String) {
    FOREGROUND("foreground"),
    BACKGROUND("background"),
    MANUAL("manual"),
}

object Batches {

    fun make(entries: List<JSONObject>, trigger: Trigger, target: Target = Target.MEASUREMENTS, max: Int = target.max): List<Batch> =
        entries.chunked(max).map { chunk ->
            val body = JSONObject().put(target.key, JSONArray(chunk))
            // Only the measurement endpoint knows the trigger.
            if (target == Target.MEASUREMENTS) {
                body.put("syncTrigger", trigger.wire)
            }
            Batch(body.toString(), newKey(), chunk.size, target)
        }

    /** The same batch without the entries at [drop] (indices into its entries). */
    fun without(batch: Batch, drop: Set<Int>): Batch? = filter(batch) { it !in drop }

    /** Only the entries at [keep]; null if none of them is in the batch. */
    fun only(batch: Batch, keep: Set<Int>): Batch? = filter(batch) { it in keep }

    private fun filter(batch: Batch, keep: (Int) -> Boolean): Batch? {
        val root = JSONObject(batch.body)
        val entries = root.getJSONArray(batch.target.key)
        val kept = JSONArray()
        for (i in 0 until entries.length()) {
            if (keep(i)) {
                kept.put(entries.get(i))
            }
        }
        if (kept.length() == 0) {
            return null
        }
        val body = root.put(batch.target.key, kept).toString()
        return Batch(body, newKey(), kept.length(), batch.target)
    }

    private fun newKey(): String = UUID.randomUUID().toString()
}
