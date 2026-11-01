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

    private suspend fun ids(records: List<SleepSessionRecord>, now: Instant, ranking: Ranking = Ranking(SleepSource.DEFAULT_ORDER)): Set<String> {
        val fake = FakeHealthConnect()
        fake.records = records
        return SleepSource(fake.client, ranking).read(null, null, zone, 2, now).samples.map { it.id }.toSet()
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

    @Test
    fun aNightHeldBackGoesOnceSettledWithoutAChange() = runBlocking {
        val fake = FakeHealthConnect()
        fake.records = listOf(phone)
        val source = SleepSource(fake.client)
        val early = Instant.parse("2026-11-01T06:30:00Z")
        val first = source.read(null, null, zone, 2, early)
        assertEquals(emptyList<Sample>(), first.samples)

        // No change since: the night still goes, now that it settled.
        val next = source.read(first.cursor, early, zone, 2, morning)
        assertEquals(setOf("s"), next.samples.map { it.id }.toSet())
    }

    @Test
    fun aReorderKeepsTheWinnerOfANightAlreadySent() = runBlocking {
        // Pebble's night went; at 10:00 the user put Sleep as Android first, then read again.
        val ranking = Ranking(SleepSource.DEFAULT_ORDER).changed(listOf(SleepSource.SLEEP_AS_ANDROID_APP, HealthConnectSource.PEBBLE_APP), Instant.parse("2026-11-01T10:00:00Z"))
        assertEquals(setOf("p"), ids(listOf(pebble, phone), morning, ranking))
    }

    @Test
    fun appsNotInTheOrderRankAsTheListShowsThem() = runBlocking {
        // Shown alphabetically after the order (AppOrder.shown): Samsung above Withings,
        // though Withings' night began earlier.
        val withings = night("w", "com.withings.wiscale2", "2026-10-31T21:50:00Z", "2026-11-01T05:00:00Z")
        val samsung = night("x", "com.sec.android.app.shealth", "2026-10-31T22:10:00Z", "2026-11-01T05:00:00Z")
        assertEquals(setOf("x"), ids(listOf(withings, samsung), morning))
    }

    @Test
    fun aNightPastTheReorderKeepsTheSentOneOut() = runBlocking {
        // The watch stopped at 03:00, its night went at 07:00; the phone's ran to 07:30.
        // At 07:15 the user put Sleep as Android first, then read again.
        val watch = night("p", HealthConnectSource.PEBBLE_APP, "2026-10-31T23:00:00Z", "2026-11-01T03:00:00Z")
        val longer = night("s", SleepSource.SLEEP_AS_ANDROID_APP, "2026-10-31T22:50:00Z", "2026-11-01T07:30:00Z")
        val ranking = Ranking(SleepSource.DEFAULT_ORDER).changed(listOf(SleepSource.SLEEP_AS_ANDROID_APP, HealthConnectSource.PEBBLE_APP), Instant.parse("2026-11-01T07:15:00Z"))
        assertEquals(setOf("p"), ids(listOf(watch, longer), Instant.parse("2026-11-01T11:30:00Z"), ranking))
    }
}
