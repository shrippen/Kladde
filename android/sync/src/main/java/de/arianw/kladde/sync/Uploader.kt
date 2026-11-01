package de.arianw.kladde.sync

import org.json.JSONObject
import java.io.IOException
import java.time.Duration
import java.time.Instant

/** One HTTP answer; status 0 means no answer (network down, timeout). */
class Response(val status: Int, val body: String)

/** Sends a batch to HealthLog. HttpTransport in the app, a fake in the tests. */
fun interface Transport {
    fun postBatch(body: String, key: String): Response
}

/**
 * Empties the queue into HealthLog, strictly oldest batch first: a re-posted day total or
 * heart-rate bucket overwrites the server's row, so a newer batch must never overtake an
 * older one waiting in backoff.
 *
 *   200          -> done; entries reported `failed` (a passing database fault) stay queued
 *                   as a smaller batch with backoff
 *   0, 409, 429, 5xx -> try again later (backoff 1 min, doubling, at most 6 h); stop
 *   401, 403     -> stop, the token is wrong; the queue stays
 *   422          -> drop the named entries and send the rest; naming none, drop the batch
 *   anything else -> stop, the queue stays (a wrong URL or a proxy answering 404 or 308
 *                    must not empty the queue; only 422 is final per HealthLog's contract)
 */
class Uploader(private val store: QueueStore, private val transport: Transport) {

    enum class Outcome { EMPTY, RETRY_LATER, AUTH_FAILED, STOPPED }

    class Result(val outcome: Outcome, val uploaded: Int, val inserted: Int, val error: String?)

    fun flush(now: Instant): Result {
        var uploaded = 0
        var inserted = 0
        var dropped: String? = null

        while (true) {
            val pending = store.next(now)
            if (pending == null) {
                return if (store.pendingCount() == 0) {
                    Result(Outcome.EMPTY, uploaded, inserted, dropped)
                } else {
                    Result(Outcome.RETRY_LATER, uploaded, inserted, dropped ?: WAITING)
                }
            }
            val response = post(pending.batch)

            if (response.status == HTTP_OK) {
                val failed = failedEntries(response.body)
                val rest = if (failed.isEmpty()) null else Batches.only(pending.batch, failed)
                uploaded += pending.batch.size - (rest?.size ?: 0)
                inserted += insertedOf(response.body)
                if (rest == null) {
                    store.done(pending.batch.key)
                    continue
                }
                store.replace(pending.batch.key, rest)
                store.retryAt(rest.key, now.plus(backoff(pending.attempts)))
                return Result(Outcome.RETRY_LATER, uploaded, inserted, "HealthLog: ${rest.size} entries failed, retrying")
            }

            if (retryable(response.status)) {
                store.retryAt(pending.batch.key, now.plus(backoff(pending.attempts)))
                return Result(Outcome.RETRY_LATER, uploaded, inserted, describe(response))
            }

            if (response.status == HTTP_UNAUTHORIZED || response.status == HTTP_FORBIDDEN) {
                return Result(Outcome.AUTH_FAILED, uploaded, inserted, describe(response))
            }

            if (response.status != HTTP_UNPROCESSABLE) {
                return Result(Outcome.STOPPED, uploaded, inserted, describe(response))
            }

            // Refused for good: keep what the server did not name, drop the rest.
            // A smaller batch only; one that names no entry of this batch would loop forever.
            val named = invalidEntries(response.body)
            val rest = if (named.isEmpty()) null else Batches.without(pending.batch, named)
            store.replace(pending.batch.key, rest?.takeIf { it.size < pending.batch.size })
            dropped = describe(response)
        }
    }

    private fun post(batch: Batch): Response = try {
        transport.postBatch(batch.body, batch.key)
    } catch (e: IOException) {
        Response(0, e.message ?: "network error")
    }

    companion object {
        const val HTTP_OK = 200
        const val HTTP_UNAUTHORIZED = 401
        const val HTTP_FORBIDDEN = 403
        const val HTTP_CONFLICT = 409 // the same Idempotency-Key is still being processed
        const val HTTP_UNPROCESSABLE = 422
        const val HTTP_TOO_MANY = 429
        const val HTTP_SERVER_ERROR = 500

        private val FIRST_RETRY: Duration = Duration.ofMinutes(1)
        private val LAST_RETRY: Duration = Duration.ofHours(6)

        const val WAITING = "upload waiting for retry"

        /** Indices of entries a 200 reports as `failed`: retryable on the server's side. */
        fun failedEntries(body: String): Set<Int> {
            val entries = try {
                JSONObject(body).optJSONObject("data")?.optJSONArray("entries")
            } catch (e: org.json.JSONException) {
                null
            } ?: return emptySet()
            val out = mutableSetOf<Int>()
            for (i in 0 until entries.length()) {
                val e = entries.optJSONObject(i) ?: continue
                if (e.optString("status") == "failed" && e.has("index")) {
                    out += e.optInt("index")
                }
            }
            return out
        }

        private fun insertedOf(body: String): Int = try {
            JSONObject(body).optJSONObject("data")?.optInt("inserted") ?: 0
        } catch (e: org.json.JSONException) {
            0
        }

        fun retryable(status: Int): Boolean =
            status == 0 || status == HTTP_CONFLICT || status == HTTP_TOO_MANY || status >= HTTP_SERVER_ERROR

        /** 1 min, 2 min, 4 min … at most 6 h. */
        fun backoff(attempts: Int): Duration {
            val factor = 1L shl attempts.coerceIn(0, 20)
            return FIRST_RETRY.multipliedBy(factor).coerceAtMost(LAST_RETRY)
        }

        /**
         * The entries a 422 names: issues whose path starts `entries.<n>`. An issue about the
         * batch itself (path "", "entries", "syncTrigger") names none.
         */
        fun invalidEntries(body: String): Set<Int> {
            val issues = try {
                JSONObject(body).optJSONObject("details")?.optJSONArray("issues")
            } catch (e: org.json.JSONException) {
                null
            } ?: return emptySet()

            val out = mutableSetOf<Int>()
            for (i in 0 until issues.length()) {
                val path = issues.optJSONObject(i)?.optString("path") ?: continue
                val match = ENTRY_PATH.find(path) ?: continue
                out += match.groupValues[1].toInt()
            }
            return out
        }

        private val ENTRY_PATH = Regex("""^entries\.(\d+)(\.|$)""")

        private fun describe(r: Response): String {
            if (r.status == 0) {
                return "no connection: ${r.body}"
            }
            val code = try {
                JSONObject(r.body).optJSONObject("meta")?.optString("errorCode")
            } catch (e: org.json.JSONException) {
                null
            }
            return if (code.isNullOrEmpty()) "HTTP ${r.status}" else "HTTP ${r.status} $code"
        }
    }
}
