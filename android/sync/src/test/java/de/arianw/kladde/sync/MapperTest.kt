package de.arianw.kladde.sync

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

class MapperTest {

    private fun fixture(name: String): String =
        requireNotNull(javaClass.classLoader?.getResource(name)) { "fixture $name" }.readText()

    /** Entry by identifier and externalId, values as numbers, so key order and 52 vs 52.0 do not matter. */
    private fun normalised(entries: List<JSONObject>): Map<String, Map<String, Any>> =
        entries.associate { e ->
            e.getString("hkIdentifier") + " " + e.getString("externalId") to e.keys().asSequence().toList().associateWith { k ->
                val v = e.get(k)
                if (v is Number) v.toDouble() else v
            }
        }

    @Test
    fun matchesTheSharedFixture() {
        // The same input and expectation as core (tests/test_mapper.cpp): both sides map alike.
        val parsed = ExchangeJson.parse(fixture("pebble-day.json"))
        assertEquals(2, parsed.warnings.size)

        val expected = JSONObject(fixture("pebble-day.expected.json"))
        val zone = ZoneId.of(expected.getString("zone"))
        val want = expected.getJSONArray("entries").let { a -> (0 until a.length()).map { a.getJSONObject(it) } }

        val got = Mapper.map(parsed.samples, zone)
        assertEquals(normalised(want), normalised(got.entries))
        assertEquals(2, got.skipped.size)
    }

    @Test
    fun readingsMatchTheSharedFixture() {
        // Weight, blood pressure, SpO₂ … of any app, in HealthKit's units; half a blood pressure is none.
        val parsed = ExchangeJson.parse(fixture("readings.json"))
        assertEquals(1, parsed.warnings.size)
        assertTrue(parsed.warnings[0].contains("BloodPressure"))

        val expected = JSONObject(fixture("readings.expected.json"))
        val want = expected.getJSONArray("entries").let { a -> (0 until a.length()).map { a.getJSONObject(it) } }
        val got = Mapper.map(parsed.samples, ZoneId.of(expected.getString("zone")))
        assertEquals(normalised(want), normalised(got.entries))
    }

    @Test
    fun dayTotalsAndBucketsMatchTheSharedFixture() {
        // Active energy and flights as day totals; heart-rate buckets from the aggregate keep their extremes.
        val parsed = ExchangeJson.parse(fixture("day-totals.json"))
        assertEquals(1, parsed.warnings.size) // a bucket whose min is no number

        val expected = JSONObject(fixture("day-totals.expected.json"))
        val want = expected.getJSONArray("entries").let { a -> (0 until a.length()).map { a.getJSONObject(it) } }
        val got = Mapper.map(parsed.samples, ZoneId.of(expected.getString("zone")))
        assertEquals(normalised(want), normalised(got.entries))
    }

    @Test
    fun sleepStageTable() {
        val table = mapOf(
            Pair(0, false) to 1, Pair(0, true) to -1, Pair(1, true) to 2, Pair(2, true) to 1,
            Pair(3, true) to -1, Pair(4, true) to 3, Pair(5, true) to 4, Pair(6, true) to 5,
            Pair(7, true) to 2, Pair(8, true) to -1,
        )
        for ((input, hk) in table) {
            assertEquals("stage $input", hk, Mapper.hkSleepStage(input.first, input.second))
        }
    }

    @Test
    fun dayOfTheClockChangeHas25Hours() {
        val s = Sample(Kind.STEPS, "x", "a", Instant.parse("2026-10-25T12:00:00Z"), Instant.parse("2026-10-25T12:01:00Z"), 1.0)
        val e = Mapper.map(listOf(s), ZoneId.of("Europe/Berlin")).entries.single()
        assertEquals("2026-10-24T22:00:00.000Z", e.getString("startDate"))
        assertEquals("2026-10-25T23:00:00.000Z", e.getString("endDate"))
    }

    @Test
    fun dayWindowDropsWhatStartedOnAnotherDay() {
        val zone = ZoneId.of("Europe/Berlin")
        val d = LocalDate.of(2026, 11, 1)
        val before = Sample(Kind.STEPS, "a", "p", Instant.parse("2026-10-31T22:30:00Z"), Instant.parse("2026-10-31T23:20:00Z"), 50.0)
        val inside = Sample(Kind.STEPS, "b", "p", Instant.parse("2026-11-01T07:00:00Z"), Instant.parse("2026-11-01T07:10:00Z"), 70.0)
        val sleep = Sample(Kind.SLEEP_STAGE, "c", "p", Instant.parse("2026-10-31T21:00:00Z"), Instant.parse("2026-10-31T22:00:00Z"), 0.0, 4, true)

        // 22:30 UTC is 23:30 on 31 October in Berlin: it would make a partial total for that day.
        val kept = DayWindow.keep(listOf(before, inside, sleep), setOf(d), zone)
        assertEquals(listOf(inside, sleep), kept)
        assertEquals(3, DayWindow.range(d, d.plusDays(2)).size)
    }

    @Test
    fun parserSkipsWhatItCannotMap() {
        val parsed = ExchangeJson.parse(fixture("pebble-day.json"))
        assertTrue(parsed.warnings[0].contains("Exercise"))
        assertTrue(parsed.warnings[1].contains("no id"))
    }

    @Test
    fun aHeartRateBucketOverMidnightBelongsToItsStartDay() {
        // Kathmandu (UTC+5:45): local midnight of 1 November is 18:15 UTC, inside the
        // bucket 18:10-18:20 that starts on 31 October.
        val zone = ZoneId.of("Asia/Kathmandu")
        val at = Instant.parse("2026-10-31T18:17:00Z")
        val hr = Sample(Kind.HEART_RATE, "h", "p", at, at, 70.0)
        assertEquals(listOf(hr), DayWindow.keep(listOf(hr), setOf(LocalDate.of(2026, 10, 31)), zone))
        assertEquals(emptyList<Sample>(), DayWindow.keep(listOf(hr), setOf(LocalDate.of(2026, 11, 1)), zone))
        val (_, end) = DayWindow.bounds(LocalDate.of(2026, 10, 31), zone)
        assertEquals(Instant.parse("2026-10-31T18:20:00Z"), DayWindow.readEnd(Kind.HEART_RATE, end))
        assertEquals(end, DayWindow.readEnd(Kind.STEPS, end))
        val berlin = DayWindow.bounds(LocalDate.of(2026, 10, 31), ZoneId.of("Europe/Berlin")).second
        assertEquals(berlin, DayWindow.readEnd(Kind.HEART_RATE, berlin))
    }

}
