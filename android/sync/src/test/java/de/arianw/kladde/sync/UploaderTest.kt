package de.arianw.kladde.sync

import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

class UploaderTest {

    private val now = Instant.parse("2026-11-01T12:00:00Z")

    private fun entries(n: Int) = (0 until n).map { JSONObject().put("externalId", "e$it").put("value", it) }

    private fun ok(inserted: Int) =
        Response(200, """{"data":{"processed":$inserted,"inserted":$inserted,"updated":0,"duplicates":0,"failed":0,"skipped":[],"entries":[]},"error":null}""")

    @Test
    fun batchesSplitAtTheLimitEachWithItsOwnKey() {
        val batches = Batches.make(entries(1201), Trigger.BACKGROUND)
        assertEquals(listOf(500, 500, 201), batches.map { it.size })
        // Same body, new key: a day total back at an earlier value must not be a replay.
        val again = Batches.make(entries(1201), Trigger.BACKGROUND)[0]
        assertEquals(batches[0].body, again.body)
        assertNotEquals(batches[0].key, again.key)
        assertNotEquals(batches[0].key, batches[1].key)
        assertEquals("background", JSONObject(batches[2].body).getString("syncTrigger"))
    }

    @Test
    fun uploadsInOrderAndEmptiesTheQueue() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(700), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        val sent = mutableListOf<Int>()
        val result = Uploader(store) { body, _ ->
            val n = JSONObject(body).getJSONArray("entries").length()
            sent += n
            ok(n)
        }.flush(now)

        assertEquals(Uploader.Outcome.EMPTY, result.outcome)
        assertEquals(listOf(500, 200), sent)
        assertEquals(700, result.uploaded)
        assertEquals(0, store.pendingCount())
    }

    @Test
    fun networkTroubleKeepsTheBatchAndBacksOff() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(3), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        val result = Uploader(store) { _, _ -> throw IOException("offline") }.flush(now)

        assertEquals(Uploader.Outcome.RETRY_LATER, result.outcome)
        assertEquals(3, store.pendingCount())
        assertNull("not due before the backoff", store.next(now))
        assertTrue(store.next(now.plus(Duration.ofMinutes(1))) != null)
        assertEquals(Duration.ofMinutes(4), Uploader.backoff(2))
        assertEquals(Duration.ofHours(6), Uploader.backoff(30))
    }

    @Test
    fun wrongTokenStopsAndKeepsTheQueue() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(3), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        val result = Uploader(store) { _, _ -> Response(401, """{"meta":{"errorCode":"auth.invalid"}}""") }.flush(now)

        assertEquals(Uploader.Outcome.AUTH_FAILED, result.outcome)
        assertEquals("HTTP 401 auth.invalid", result.error)
        assertEquals(3, store.pendingCount())
    }

    @Test
    fun anUnexpectedAnswerStopsAndKeepsTheQueue() {
        // A wrong URL path or a proxy (404, 308, 400, 413) must not empty the queue.
        for (status in listOf(308, 400, 404, 413)) {
            val store = MemoryStore()
            store.enqueue(Batches.make(entries(700), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
            var calls = 0
            val result = Uploader(store) { _, _ -> calls++; Response(status, "<html>not here</html>") }.flush(now)

            assertEquals(Uploader.Outcome.STOPPED, result.outcome)
            assertEquals("HTTP $status", result.error)
            assertEquals(1, calls)
            assertEquals(700, store.pendingCount())
        }
    }

    @Test
    fun aNewerBatchNeverOvertakesOneInBackoff() {
        // A re-posted day total overwrites: 3000 (A) arriving after 4000 (B) would stick.
        val store = MemoryStore()
        val a = Batches.make(entries(1), Trigger.BACKGROUND)
        store.enqueue(a, "t1", emptyMap(), emptyMap())
        Uploader(store) { _, _ -> Response(503, "") }.flush(now)
        store.enqueue(Batches.make(entries(2), Trigger.BACKGROUND), "t2", emptyMap(), emptyMap())

        val sent = mutableListOf<String>()
        val later = now.plus(Duration.ofSeconds(30))
        val result = Uploader(store) { _, key -> sent += key; ok(1) }.flush(later)
        assertEquals(emptyList<String>(), sent)
        assertEquals(Uploader.Outcome.RETRY_LATER, result.outcome)
        assertEquals(Uploader.WAITING, result.error)

        Uploader(store) { _, key -> sent += key; ok(1) }.flush(now.plus(Duration.ofMinutes(2)))
        assertEquals(a[0].key, sent.first())
        assertEquals(2, sent.size)
    }

    @Test
    fun entriesFailedOnTheServerStayQueued() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(3), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        val bodies = mutableListOf<String>()
        val result = Uploader(store) { body, _ ->
            bodies += body
            Response(200, """{"data":{"processed":3,"inserted":2,"failed":1,"entries":[
                {"index":0,"status":"inserted"},{"index":1,"status":"failed"},{"index":2,"status":"inserted"}]}}""")
        }.flush(now)

        assertEquals(Uploader.Outcome.RETRY_LATER, result.outcome)
        assertEquals(2, result.uploaded)
        assertEquals(1, store.pendingCount())
        assertNull("in backoff", store.next(now))
        val again = store.next(now.plus(Duration.ofMinutes(1)))!!.batch
        assertEquals("e1", JSONObject(again.body).getJSONArray("entries").getJSONObject(0).getString("externalId"))
        assertEquals(1, bodies.size)
    }

    @Test
    fun anOkThatIsNotJsonStillCounts() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(2), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        val result = Uploader(store) { _, _ -> Response(200, "ok") }.flush(now)

        assertEquals(Uploader.Outcome.EMPTY, result.outcome)
        assertEquals(2, result.uploaded)
    }

    @Test
    fun aFailingSourceIsReportedAndTheQueueStillGoes() = runBlocking {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(2), Trigger.MANUAL), "t0", emptyMap(), emptyMap())
        val status = SyncRunner(store, { _, _ -> ok(2) }, ZoneId.of("UTC")).run({ _, _, _ ->
            throw IllegalStateException("rate limited")
        }, Trigger.MANUAL, now)

        assertEquals("t0", store.cursor())
        assertEquals(0, store.pendingCount())
        assertEquals("Health Connect: IllegalStateException: rate limited", status.lastError)
        assertNull(status.lastSuccess)
    }

    @Test
    fun refusedEntriesAreDroppedTheRestGoesAgain() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(4), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        var calls = 0
        val result = Uploader(store) { body, _ ->
            calls++
            val n = JSONObject(body).getJSONArray("entries").length()
            if (n == 4) {
                Response(422, """{"details":{"issues":[{"path":"entries.1.startDate"},{"path":"entries.3"}]}}""")
            } else {
                ok(n)
            }
        }.flush(now)

        assertEquals(2, calls)
        assertEquals(2, result.uploaded)
        assertEquals(0, store.pendingCount())
    }

    @Test
    fun aRefusalThatNamesNoEntryDropsTheBatchOnce() {
        val store = MemoryStore()
        store.enqueue(Batches.make(entries(2), Trigger.MANUAL), "t1", emptyMap(), emptyMap())
        var calls = 0
        Uploader(store) { _, _ ->
            calls++
            // Index 7 is not in this batch: resending it unchanged would loop.
            Response(422, """{"details":{"issues":[{"path":"entries.7"}]}}""")
        }.flush(now)

        assertEquals(1, calls)
        assertEquals(0, store.pendingCount())
    }

    @Test
    fun invalidEntriesReadsOnlyEntryPaths() {
        val body = """{"details":{"issues":[{"path":"entries.2.value"},{"path":"entries"},{"path":"syncTrigger"},{"path":"entries.10"},{"path":"entriesX.1"}]}}"""
        assertEquals(setOf(2, 10), Uploader.invalidEntries(body))
        assertEquals(emptySet<Int>(), Uploader.invalidEntries("not json"))
    }

    @Test
    fun aRunQueuesWithTheCursorAndRecordsStatus() = runBlocking {
        val store = MemoryStore()
        val zone = ZoneId.of("Europe/Berlin")
        val day = LocalDate.of(2026, 11, 1)
        val steps = Sample(Kind.STEPS, "s1", "p", Instant.parse("2026-11-01T08:00:00Z"), Instant.parse("2026-11-01T08:10:00Z"), 300.0)

        val status = SyncRunner(store, { _, _ -> Response(0, "offline") }, zone).run({ cursor, _, _ ->
            assertNull(cursor)
            HealthConnectSource.Read(listOf(steps), "token-1", mapOf("s1" to setOf(day)))
        }, Trigger.MANUAL, now)

        // Offline: the data stays queued, the cursor moved with it, nothing is lost.
        assertEquals("token-1", store.cursor())
        assertEquals(1, store.pendingCount())
        assertEquals(setOf(day), store.daysOf("s1"))
        assertEquals(now, status.lastRun)
        assertNull(status.lastSuccess)
        assertTrue(status.lastError!!.startsWith("no connection"))
    }

    @Test
    fun aKindThatFellSilentIsNamed() {
        val seen = mapOf(
            Kind.SLEEP_STAGE to Instant.parse("2026-11-01T06:00:00Z"),
            Kind.STEPS to Instant.parse("2026-10-29T20:00:00Z"),
            Kind.HEART_RATE to Instant.parse("2026-10-31T20:00:00Z"),
        )
        assertEquals(setOf(Kind.STEPS), SyncRunner.silentKinds(seen))
        assertEquals(emptySet<Kind>(), SyncRunner.silentKinds(emptyMap()))
    }
}
