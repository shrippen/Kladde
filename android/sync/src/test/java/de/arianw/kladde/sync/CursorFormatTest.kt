package de.arianw.kladde.sync

import androidx.health.connect.client.records.BodyFatRecord
import androidx.health.connect.client.records.WeightRecord
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant
import java.time.ZoneId

/** Stored cursors: a changed string makes every user read the whole window again. */
class CursorFormatTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val now = Instant.parse("2026-11-01T12:00:00Z")

    @Test
    fun dayTotalCursor() = runBlocking {
        val expected = "ActiveCaloriesBurnedRecord,DistanceRecord,FloorsClimbedRecord,HeartRateRecord,StepsRecord;day-at-start|"
        val first = DayTotalSource(FakeHealthConnect().client).read(null, zone, 1, { emptySet() }, now)
        assertEquals(expected + "token-new", first.cursor)

        val next = DayTotalSource(FakeHealthConnect().client).read(first.cursor, zone, 1, { emptySet() }, now)
        assertEquals(expected + "token-next", next.cursor)
    }

    @Test
    fun readingCursor() = runBlocking {
        val source = ReadingSource(FakeHealthConnect().client, setOf(WeightRecord::class, BodyFatRecord::class))
        val first = source.read(null, zone, 1, now)
        assertEquals("BodyFatRecord,WeightRecord|token-new", first.cursor)
        assertEquals("BodyFatRecord,WeightRecord|token-next", source.read(first.cursor, zone, 1, now).cursor)
    }

    @Test
    fun sleepCursor() = runBlocking {
        val source = SleepSource(FakeHealthConnect().client)
        val first = source.read(null, null, zone, 1, now)
        assertEquals("token-new", first.cursor)
        assertEquals("token-next", source.read(first.cursor, now, zone, 1, now).cursor)
    }

    @Test
    fun workoutCursor() = runBlocking {
        val source = WorkoutSource(FakeHealthConnect().client)
        val first = source.read(null, null, zone, 1, now)
        assertEquals("token-new", first.cursor)
        assertEquals("token-next", source.read(first.cursor, now, zone, 1, now).cursor)
    }
}
