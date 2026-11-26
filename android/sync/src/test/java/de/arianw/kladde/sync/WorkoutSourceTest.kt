package de.arianw.kladde.sync

import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.ExerciseSessionRecord
import androidx.health.connect.client.records.ExerciseSessionRecord.Companion.EXERCISE_TYPE_RUNNING
import androidx.health.connect.client.records.metadata.Metadata
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.ZoneId

class WorkoutSourceTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val now = Instant.parse("2026-11-01T12:00:00Z")

    // Sessions a day apart, so each is its own training.
    private fun run(id: String, daysAgo: Long) = Instant.parse("2026-10-31T10:00:00Z").minusSeconds(daysAgo * 86400).let {
        ExerciseSessionRecord(it, null, it.plusSeconds(1800), null, Metadata.manualEntryWithId(id), EXERCISE_TYPE_RUNNING)
    }

    private fun ids(read: WorkoutSource.Read) = read.entries.map { it.getString("externalId") }

    @Test
    fun theFirstReadTakesTheWindowAndSetsTheCursor() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(run("r1", 0))

        val read = WorkoutSource(fake.client).read(null, null, zone, 3, now)

        assertTrue("getChanges" !in fake.calls)
        assertEquals(listOf("hc:r1"), ids(read))
        assertEquals("token-new", read.cursor)
        // The day before the window's first day, in Berlin, up to now.
        assertEquals(Instant.parse("2026-10-28T23:00:00Z") to now, fake.ranges.first())
    }

    @Test
    fun aLaterReadFollowsTheChanges() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(run("r1", 0))
        fake.changes = listOf(UpsertionChange(run("r1", 0)))

        val read = WorkoutSource(fake.client).read("token-new", now.minusSeconds(86400), zone, 3, now)

        assertTrue("getChangesToken" !in fake.calls)
        assertEquals(listOf("hc:r1"), ids(read))
        assertEquals("token-next", read.cursor)
    }

    @Test
    fun withoutChangesNothingNewComes() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(run("r1", 0))

        val read = WorkoutSource(fake.client).read("token-new", now.minusSeconds(60), zone, 3, now)

        assertEquals(emptyList<String>(), ids(read))
        assertEquals("token-next", read.cursor)
    }

    @Test
    fun anExpiredTokenReadsTheWindowAgain() = runBlocking {
        val fake = FakeHealthConnect()
        fake.tokenExpired = true
        fake.records = listOf(run("r1", 0))

        val read = WorkoutSource(fake.client).read("token-old", now.minusSeconds(60), zone, 3, now)

        assertTrue("getChangesToken" in fake.calls)
        assertEquals(listOf("hc:r1"), ids(read))
        assertEquals("token-new", read.cursor)
    }

    @Test
    fun recordsComeOverSeveralPages() = runBlocking {
        val fake = FakeHealthConnect()
        fake.recordsPerPage = 2
        fake.records = (0L..4L).map { run("r$it", it) }

        val read = WorkoutSource(fake.client).read(null, null, zone, 7, now)

        assertEquals(setOf("hc:r0", "hc:r1", "hc:r2", "hc:r3", "hc:r4"), ids(read).toSet())
    }

    @Test
    fun changesComeOverSeveralPages() = runBlocking {
        val fake = FakeHealthConnect()
        fake.changesPerPage = 2
        fake.records = (0L..4L).map { run("r$it", it) }
        fake.changes = fake.records.map { UpsertionChange(it) }

        val read = WorkoutSource(fake.client).read("token-new", now.minusSeconds(60), zone, 7, now)

        assertEquals(3, fake.calls.count { it == "getChanges" })
        assertEquals(setOf("hc:r0", "hc:r1", "hc:r2", "hc:r3", "hc:r4"), ids(read).toSet())
        assertEquals("token-next", read.cursor)
    }
}
