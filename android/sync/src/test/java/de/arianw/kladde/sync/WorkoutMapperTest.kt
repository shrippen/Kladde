package de.arianw.kladde.sync

import androidx.health.connect.client.records.ExerciseSessionRecord.Companion.EXERCISE_TYPE_BIKING_STATIONARY
import androidx.health.connect.client.records.ExerciseSessionRecord.Companion.EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING
import androidx.health.connect.client.records.ExerciseSessionRecord.Companion.EXERCISE_TYPE_OTHER_WORKOUT
import androidx.health.connect.client.records.ExerciseSessionRecord.Companion.EXERCISE_TYPE_WALKING
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

class WorkoutMapperTest {

    private val zone = ZoneId.of("UTC")
    private val day = LocalDate.of(2026, 10, 10)
    private val now = Instant.parse("2026-10-11T12:00:00Z")
    private val order = WorkoutMapper.DEFAULT_ORDER
    private val freeletics = WorkoutMapper.FREELETICS_APP
    private val pebble = HealthConnectSource.PEBBLE_APP

    private fun pick(sessions: List<Session>, order: List<String>, days: Set<LocalDate>, zone: ZoneId, now: Instant, since: Instant? = null) =
        Sessions.select(sessions, order, days, zone, now, WorkoutMapper.SETTLE, since)

    private fun t(hhmm: String) = Instant.parse("2026-10-10T$hhmm:00Z")

    private fun session(id: String, app: String, type: Int, from: String, to: String, title: String? = null) =
        Session(id, app, type, title, t(from), t(to))

    // A Freeletics training in three sections, the Pebble's own "Workout" over it.
    private val sections = listOf(
        session("w", freeletics, EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING, "08:10", "08:20", "Warmup"),
        session("i", freeletics, EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING, "08:21", "08:40", "Intervall"),
        session("c", freeletics, EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING, "08:42", "08:50", "Cooldown"),
    )
    private val pebbleWorkout = session("p", pebble, EXERCISE_TYPE_OTHER_WORKOUT, "08:08", "09:00")

    @Test
    fun sectionsAreOneTrainingWithTheGapsAsPause() {
        val w = pick(sections, order, setOf(day), zone, now).single()
        val e = WorkoutMapper.entry(w, emptyList())

        assertEquals("hiit", e.getString("sportType"))
        assertEquals("2026-10-10T08:10:00Z", e.getString("startedAt"))
        assertEquals("2026-10-10T08:50:00Z", e.getString("endedAt"))
        assertEquals(180, e.getInt("pauseDurationSec"))
        assertEquals("hc:w", e.getString("externalId"))
        assertEquals("Warmup · Intervall · Cooldown", e.getJSONObject("metadata").getString("title"))
        assertEquals(3, e.getJSONObject("metadata").getInt("sections"))
    }

    @Test
    fun aGapOverFiveMinutesStartsAnotherTraining() {
        val two = listOf(
            session("a", freeletics, EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING, "08:00", "08:10"),
            session("b", freeletics, EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING, "08:16", "08:30"),
        )
        assertEquals(2, pick(two, order, setOf(day), zone, now).size)
    }

    @Test
    fun onOverlapTheAppFirstInTheOrderWins() {
        val first = pick(sections + pebbleWorkout, order, setOf(day), zone, now)
        assertEquals(listOf("w"), first.map { it.first.id })

        val pebbleFirst = pick(sections + pebbleWorkout, listOf(pebble, freeletics), setOf(day), zone, now)
        assertEquals(listOf("p"), pebbleFirst.map { it.first.id })
    }

    @Test
    fun anAppNotInTheOrderComesLast() {
        val other = session("x", "org.example.gym", EXERCISE_TYPE_BIKING_STATIONARY, "08:00", "08:30")
        val walk = session("f", WorkoutMapper.FIT_APP, EXERCISE_TYPE_WALKING, "08:20", "08:40")
        val kept = pick(listOf(other, walk), order, setOf(day), zone, now)
        assertEquals(listOf("f"), kept.map { it.first.id })

        // Alone it goes.
        assertEquals("cycling", WorkoutMapper.entry(pick(listOf(other), order, setOf(day), zone, now).single(), emptyList()).getString("sportType"))
    }

    @Test
    fun aTrainingIsHeldUntilItSettled() {
        val soon = t("10:00")
        assertEquals(0, pick(sections, order, setOf(day), zone, soon).size)
        assertEquals(1, pick(sections, order, setOf(day), zone, t("10:50")).size)
    }

    @Test
    fun aTrainingHeldAtTheLastReadGoesLaterWithoutAChange() {
        // Read at 09:30, held (not settled); the next read two days on brings no change for it.
        val lastRead = t("09:30")
        val later = Instant.parse("2026-10-12T00:30:00Z")
        val sent = pick(sections, order, emptySet(), zone, later, lastRead)
        assertEquals(listOf("w"), sent.map { it.first.id })

        // Settled before that read: it went then, not again.
        assertEquals(0, pick(sections, order, emptySet(), zone, later, t("11:00")).size)
    }

    @Test
    fun onlyTheDaysAskedFor() {
        assertEquals(0, pick(sections, order, setOf(day.plusDays(1)), zone, now).size)
    }

    @Test
    fun heartRateFromEveryAppStepsAndDistanceFromItsOwn() {
        val walk = session("f", pebble, EXERCISE_TYPE_WALKING, "18:00", "18:30")
        val samples = listOf(
            Sample(Kind.HEART_RATE, "h1", pebble, t("18:05"), t("18:05"), 90.0),
            Sample(Kind.HEART_RATE, "h2", WorkoutMapper.FIT_APP, t("18:10"), t("18:10"), 110.0),
            Sample(Kind.HEART_RATE, "h3", pebble, t("18:20"), t("18:20"), 400.0), // a sensor glitch
            Sample(Kind.HEART_RATE, "h4", pebble, t("18:40"), t("18:40"), 70.0), // after it
            Sample(Kind.STEPS, "s1", pebble, t("18:00"), t("18:15"), 1500.0),
            Sample(Kind.STEPS, "s2", WorkoutMapper.FIT_APP, t("18:00"), t("18:15"), 1400.0),
            Sample(Kind.DISTANCE, "d1", WorkoutMapper.FIT_APP, t("18:00"), t("18:15"), 1100.0),
        )
        val e = WorkoutMapper.entry(pick(listOf(walk), order, setOf(day), zone, now).single(), samples)

        assertEquals("walking", e.getString("sportType"))
        assertEquals(100, e.getInt("avgHeartRate"))
        assertEquals(90, e.getInt("minHeartRate"))
        assertEquals(110, e.getInt("maxHeartRate"))
        assertEquals(1500, e.getInt("stepCount"))
        assertFalse(e.has("totalDistanceM"))
        assertFalse(e.has("pauseDurationSec"))
    }
}
