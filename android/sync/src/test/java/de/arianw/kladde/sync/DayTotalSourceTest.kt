package de.arianw.kladde.sync

import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.metadata.Metadata
import androidx.health.connect.client.changes.DeletionChange
import androidx.health.connect.client.changes.UpsertionChange
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

class DayTotalSourceTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val start = Instant.parse("2026-10-31T22:30:00Z") // 23:30 on 31 October in Berlin
    private val end = Instant.parse("2026-10-31T23:20:00Z") // 00:20 on 1 November

    @Test
    fun stepsOverMidnightBelongToTheirStartDay() {
        // The Mapper counts them on 31 October; a deletion must re-read that day.
        val steps = StepsRecord(start, null, end, null, 120, Metadata.manualEntry())
        assertEquals(setOf(LocalDate.of(2026, 10, 31)), DayTotalSource.daysOf(steps, zone))
    }

    @Test
    fun heartRateOverMidnightBelongsToBothDays() {
        val hr = HeartRateRecord(start, null, end, null, listOf(
            HeartRateRecord.Sample(start, 60),
            HeartRateRecord.Sample(end, 70),
        ), Metadata.manualEntry())
        assertEquals(setOf(LocalDate.of(2026, 10, 31), LocalDate.of(2026, 11, 1)),
            DayTotalSource.daysOf(hr, zone))
    }

    private val now = Instant.parse("2026-11-01T12:00:00Z")

    private fun stepsOn(id: String, at: Instant) =
        StepsRecord(at, null, at.plusSeconds(600), null, 50, Metadata.manualEntryWithId(id))

    @Test
    fun dayTotalsAndBucketsComeFromTheAggregate() = runBlocking {
        val fake = FakeHealthConnect()
        fake.longs = mapOf(StepsRecord.COUNT_TOTAL.metricKey to 5400L)
        val bucket = Instant.parse("2026-11-01T09:00:00Z")
        fake.buckets = mapOf(bucket to Triple(72L, 61L, 98L), Instant.parse("2026-10-31T09:00:00Z") to Triple(60L, 60L, 60L))

        val read = DayTotalSource(fake.client).read(null, zone, 1, { emptySet() }, now)
        val entries = Mapper.map(read.samples, zone).entries

        val steps = entries.single { it.getString("hkIdentifier") == Mapper.STEPS_ID }
        assertEquals("stats:${Mapper.STEPS_ID}:2026-11-01", steps.getString("externalId"))
        assertEquals(5400.0, steps.getDouble("value"), 0.0)
        // Only the bucket of the day read; avg, min, max as Health Connect gave them.
        val hr = entries.single { it.getString("hkIdentifier") == Mapper.HEART_RATE_ID }
        assertEquals(72.0, hr.getDouble("value"), 0.0)
        assertEquals(61.0, hr.getDouble("valueMin"), 0.0)
        assertEquals(98.0, hr.getDouble("valueMax"), 0.0)
        // The local day, not the UTC one: 1 November in Berlin starts 23:00 UTC.
        assertEquals(Instant.parse("2026-10-31T23:00:00Z") to Instant.parse("2026-11-01T23:00:00Z"), fake.aggregated.single())
    }

    @Test
    fun theFirstReadKnowsTheDaysOfEveryRecord() = runBlocking {
        // A record of the window deleted later must re-aggregate its own day, not today.
        val fake = FakeHealthConnect()
        fake.records = listOf(stepsOn("r1", Instant.parse("2026-10-31T10:00:00Z")))
        val read = DayTotalSource(fake.client).read(null, zone, 2, { emptySet() }, now)
        assertEquals(setOf(LocalDate.of(2026, 10, 31)), read.recordDays["r1"])
    }

    @Test
    fun aCursorForOtherTypesReadsTheWindowAgain() = runBlocking {
        // 0.1.0's cursor has no types: the first run of 0.2 reads the window anew (and, as
        // 0.1.0 dated day totals a day late too, the older totals: a020CursorReadsTheWindowAgain).
        val fake = FakeHealthConnect()
        val read = DayTotalSource(fake.client).read("token-0.1", zone, 2, { emptySet() }, now)
        assertTrue("getChangesToken" in fake.calls)
        assertTrue("getChanges" !in fake.calls)
        assertEquals(2, fake.aggregated.size)
        assertTrue(read.cursor!!.endsWith("|token-new"))

        // Its own cursor: only the changes.
        val again = FakeHealthConnect()
        DayTotalSource(again.client).read(read.cursor, zone, 2, { emptySet() }, now)
        assertTrue("getChangesToken" !in again.calls)
        assertEquals(0, again.aggregated.size)
    }

    @Test
    fun a020CursorReadsTheWindowAgain() = runBlocking {
        // 0.2.0 dated day totals a day late: its cursor (same types) reads its window again,
        // and the totals of every day 0.1/0.2 can have sent before it (365 days, the most the
        // setting allows, before 1 November 2026) and one more, so the chain of moved totals
        // ends on a day without an old one and HealthLog retires none.
        val types = (DayTotalSource.TYPES + DayTotalSource.OPTIONAL_TYPES)
            .map { it.simpleName }.sortedBy { it }.joinToString(",")
        val fake = FakeHealthConnect()
        fake.longs = mapOf(StepsRecord.COUNT_TOTAL.metricKey to 100L)
        val later = Instant.parse("2026-11-26T12:00:00Z")
        val read = DayTotalSource(fake.client).read("$types|token-0.2", zone, 2, { emptySet() }, later)
        assertTrue("getChanges" !in fake.calls)
        assertEquals(2, fake.aggregated.size)

        val stepDays = read.samples.filter { it.kind == Kind.STEPS }.map { Mapper.dayOfSample(it, zone) }.toSet()
        assertEquals(DayWindow.range(LocalDate.of(2025, 11, 1), LocalDate.of(2026, 11, 26)), stepDays)
    }

    @Test
    fun aChangeReaggregatesItsDayAndADeletionOfAnUnknownRecordTodayAndYesterday() = runBlocking {
        val fake = FakeHealthConnect()
        val cursor = DayTotalSource(fake.client).read(null, zone, 1, { emptySet() }, now).cursor

        val changed = FakeHealthConnect()
        changed.changes = listOf(UpsertionChange(stepsOn("s9", Instant.parse("2026-10-20T10:00:00Z"))))
        val read = DayTotalSource(changed.client).read(cursor, zone, 1, { emptySet() }, now)
        assertEquals(listOf(Instant.parse("2026-10-19T22:00:00Z")), changed.aggregated.map { it.first })
        assertEquals(setOf(LocalDate.of(2026, 10, 20)), read.recordDays["s9"])

        val deleted = FakeHealthConnect()
        deleted.changes = listOf(DeletionChange("unknown"))
        DayTotalSource(deleted.client).read(cursor, zone, 1, { emptySet() }, now)
        assertEquals(2, deleted.aggregated.size)
    }
}
