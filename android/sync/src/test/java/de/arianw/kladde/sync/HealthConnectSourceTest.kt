package de.arianw.kladde.sync

import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.metadata.Metadata
import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

class HealthConnectSourceTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val start = Instant.parse("2026-10-31T22:30:00Z") // 23:30 on 31 October in Berlin
    private val end = Instant.parse("2026-10-31T23:20:00Z") // 00:20 on 1 November

    @Test
    fun stepsOverMidnightBelongToTheirStartDay() {
        // The Mapper counts them on 31 October; a deletion must re-read that day.
        val steps = StepsRecord(start, null, end, null, 120, Metadata.manualEntry())
        assertEquals(setOf(LocalDate.of(2026, 10, 31)), HealthConnectSource.daysOf(steps, zone))
    }

    @Test
    fun heartRateOverMidnightBelongsToBothDays() {
        val hr = HeartRateRecord(start, null, end, null, listOf(
            HeartRateRecord.Sample(start, 60),
            HeartRateRecord.Sample(end, 70),
        ), Metadata.manualEntry())
        assertEquals(setOf(LocalDate.of(2026, 10, 31), LocalDate.of(2026, 11, 1)),
            HealthConnectSource.daysOf(hr, zone))
    }
}
