package de.arianw.kladde.sync

import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.records.metadata.DataOrigin
import androidx.health.connect.client.records.metadata.Metadata
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant
import java.time.ZoneId

class SleepSourceTest {

    private val zone = ZoneId.of("Europe/Berlin")
    private val morning = Instant.parse("2026-11-01T12:00:00Z")

    private fun night(id: String, app: String, from: String, to: String) = SleepSessionRecord(
        Instant.parse(from), null, Instant.parse(to), null,
        metadata = Metadata.autoRecordedWithId(id, androidx.health.connect.client.records.metadata.Device(type = 0)).let {
            withOrigin(it, app)
        },
    )

    // Metadata's origin is set by Health Connect; the tests need one per app.
    private fun withOrigin(m: Metadata, app: String): Metadata {
        val field = Metadata::class.java.getDeclaredField("dataOrigin")
        field.isAccessible = true
        field.set(m, DataOrigin(app))
        return m
    }

    private val pebble = night("p", HealthConnectSource.PEBBLE_APP, "2026-10-31T22:00:00Z", "2026-11-01T05:00:00Z")
    private val phone = night("s", SleepSource.SLEEP_AS_ANDROID_APP, "2026-10-31T21:50:00Z", "2026-11-01T05:10:00Z")

    private suspend fun ids(records: List<SleepSessionRecord>, now: Instant): Set<String> {
        val fake = FakeHealthConnect()
        fake.records = records
        return SleepSource(fake.client).read(null, null, zone, 2, now).samples.map { it.id }.toSet()
    }

    @Test
    fun theWatchsNightWinsOverThePhones() = runBlocking {
        assertEquals(setOf("p"), ids(listOf(pebble, phone), morning))
    }

    @Test
    fun aNightOfOneAppAloneGoes() = runBlocking {
        assertEquals(setOf("s"), ids(listOf(phone), morning))
    }

    @Test
    fun aNightWaitsUntilTheWatchHadTimeToSync() = runBlocking {
        // 07:30 in Berlin, the phone's night ended an hour ago: the watch's may still come.
        assertEquals(emptySet<String>(), ids(listOf(phone), Instant.parse("2026-11-01T06:30:00Z")))
    }
}
