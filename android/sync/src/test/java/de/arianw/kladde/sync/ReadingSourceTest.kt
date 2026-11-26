package de.arianw.kladde.sync

import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.BodyFatRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.WeightRecord
import androidx.health.connect.client.records.metadata.Metadata
import androidx.health.connect.client.units.Mass
import androidx.health.connect.client.units.Percentage
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.ZoneId
import kotlin.reflect.KClass

class ReadingSourceTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val now = Instant.parse("2026-11-01T12:00:00Z")
    private val types: Set<KClass<out Record>> = setOf(WeightRecord::class, BodyFatRecord::class)
    private val signature = "BodyFatRecord,WeightRecord"

    private fun weight(id: String, kg: Double) =
        WeightRecord(now.minusSeconds(3600), null, Mass.kilograms(kg), Metadata.manualEntryWithId(id))

    @Test
    fun theFirstReadTakesTheWindowAndSetsTheCursor() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(weight("w1", 80.0))

        val read = ReadingSource(fake.client, types).read(null, zone, 2, now)

        assertTrue("getChanges" !in fake.calls)
        assertEquals(listOf("w1"), read.samples.map { it.id })
        assertEquals("$signature|token-new", read.cursor)
        // Window: the start of the day before today in Berlin, up to now; once per type.
        assertEquals(List(2) { Instant.parse("2026-10-30T23:00:00Z") to now }, fake.ranges)
    }

    @Test
    fun aLaterReadTakesOnlyTheChanges() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(weight("old", 70.0))
        fake.changes = listOf(UpsertionChange(weight("new", 81.0)))

        val read = ReadingSource(fake.client, types).read("$signature|token-new", zone, 2, now)

        assertTrue("getChangesToken" !in fake.calls)
        assertTrue("readRecords" !in fake.calls)
        assertEquals(listOf("new"), read.samples.map { it.id })
        assertEquals("$signature|token-next", read.cursor)
    }

    @Test
    fun aCursorForOtherTypesReadsTheWindowAgain() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(weight("w1", 80.0))

        val read = ReadingSource(fake.client, types).read("WeightRecord|token-old", zone, 2, now)

        assertTrue("getChanges" !in fake.calls)
        assertEquals(listOf("w1"), read.samples.map { it.id })
        assertEquals("$signature|token-new", read.cursor)
    }

    @Test
    fun anExpiredTokenReadsTheWindowAgain() = runBlocking {
        val fake = FakeHealthConnect()
        fake.tokenExpired = true
        fake.records = listOf(weight("w1", 80.0))

        val read = ReadingSource(fake.client, types).read("$signature|token-old", zone, 2, now)

        assertTrue("getChangesToken" in fake.calls)
        assertEquals(listOf("w1"), read.samples.map { it.id })
        assertEquals("$signature|token-new", read.cursor)
    }

    @Test
    fun recordsComeOverSeveralPages() = runBlocking {
        val fake = FakeHealthConnect()
        fake.recordsPerPage = 2
        fake.records = (1..5).map { weight("w$it", 70.0 + it) }

        val read = ReadingSource(fake.client, types).read(null, zone, 2, now)

        assertEquals((1..5).map { "w$it" }, read.samples.map { it.id })
        // Weight: three pages; body fat: one empty page.
        assertEquals(4, fake.calls.count { it == "readRecords" })
    }

    @Test
    fun changesComeOverSeveralPages() = runBlocking {
        val fake = FakeHealthConnect()
        fake.changesPerPage = 2
        fake.changes = (1..5).map { UpsertionChange(weight("w$it", 70.0 + it)) }

        val read = ReadingSource(fake.client, types).read("$signature|token-new", zone, 2, now)

        assertEquals((1..5).map { "w$it" }, read.samples.map { it.id })
        assertEquals(3, fake.calls.count { it == "getChanges" })
        assertEquals("$signature|token-next", read.cursor)
    }
}
