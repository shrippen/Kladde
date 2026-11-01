package de.arianw.kladde.sync

import androidx.health.connect.client.records.ExerciseSessionRecord as Ex
import org.json.JSONObject
import java.time.Duration
import kotlin.math.roundToInt

/**
 * Exercise sessions to entries for POST /api/workouts/batch; which ones, Sessions.select.
 *
 * Freeletics writes one session per section (Warmup 09:02–09:08, Intervall 09:09–09:18,
 * Cooldown 09:19–09:25): one training, the gaps its pause. Where the Pebble's "Workout"
 * overlaps a Freeletics one, the app first in [DEFAULT_ORDER] stays.
 */
object WorkoutMapper {

    /** HealthLog keeps a training's first write: by then the sections and the other apps are in. */
    val SETTLE: Duration = Duration.ofHours(2)

    const val FREELETICS_APP = "com.freeletics.lite"
    const val FIT_APP = "com.google.android.apps.fitness"

    /** Owner, 1.11.2026: Freeletics, then the Pebble app, then Google Fit. */
    val DEFAULT_ORDER = listOf(FREELETICS_APP, HealthConnectSource.PEBBLE_APP, FIT_APP)

    private const val SOURCE = "APPLE_HEALTH" // like the measurements: the session's client
    private const val ID_PREFIX = "hc:"

    // HealthLog's limits (CreateBatchWorkoutRequest): a value outside is left out, never
    // sent, as one refused entry fails the whole batch (400) and stops the queue.
    private val HEART_RATE = 20..300
    private const val MAX_STEPS = 200_000
    private const val MAX_DISTANCE_M = 1_000_000.0
    private const val MAX_PAUSE_S = 86_400L

    /**
     * One entry. Heart rate from every app (watch and phone agree, no sum); steps and
     * distance only from the training's own app, as the phone and the watch both count them.
     */
    fun entry(w: SessionGroup, samples: List<Sample>): JSONObject {
        val within = samples.filter { !it.start.isBefore(w.start) && it.start.isBefore(w.end) }
        val mine = within.filter { it.app == w.first.app }

        val e = JSONObject()
            .put("sportType", sportType(w.first.type))
            .put("startedAt", w.start.toString())
            .put("endedAt", w.end.toString())
            .put("source", SOURCE)
            .put("externalId", ID_PREFIX + w.first.id)
            .put("metadata", metadata(w))

        val pause = w.pause.seconds
        if (pause in 1..MAX_PAUSE_S) {
            e.put("pauseDurationSec", pause)
        }

        val bpm = within.filter { it.kind == Kind.HEART_RATE }.map { it.value.roundToInt() }.filter { it in HEART_RATE }
        if (bpm.isNotEmpty()) {
            e.put("avgHeartRate", bpm.average().roundToInt())
            e.put("minHeartRate", bpm.min())
            e.put("maxHeartRate", bpm.max())
        }

        val steps = mine.filter { it.kind == Kind.STEPS }.sumOf { it.value }.roundToInt()
        if (steps in 1..MAX_STEPS) {
            e.put("stepCount", steps)
        }

        val metres = mine.filter { it.kind == Kind.DISTANCE }.sumOf { it.value }
        if (metres > 0 && metres <= MAX_DISTANCE_M) {
            e.put("totalDistanceM", metres)
        }
        return e
    }

    private fun metadata(w: SessionGroup): JSONObject {
        val m = JSONObject()
            .put("app", w.first.app)
            .put("healthConnectType", Ex.EXERCISE_TYPE_INT_TO_STRING_MAP[w.first.type] ?: w.first.type.toString())
            .put("sections", w.sessions.size)
        val titles = w.sessions.mapNotNull { it.title?.takeIf(String::isNotBlank) }.distinct()
        if (titles.isNotEmpty()) {
            m.put("title", titles.joinToString(" · "))
        }
        return m
    }

    /** Health Connect's exercise type as HealthLog's sport type; what has none is "other". */
    fun sportType(type: Int): String = when (type) {
        Ex.EXERCISE_TYPE_WALKING -> "walking"
        Ex.EXERCISE_TYPE_RUNNING, Ex.EXERCISE_TYPE_RUNNING_TREADMILL -> "running"
        Ex.EXERCISE_TYPE_BIKING, Ex.EXERCISE_TYPE_BIKING_STATIONARY -> "cycling"
        Ex.EXERCISE_TYPE_HIKING -> "hiking"
        Ex.EXERCISE_TYPE_SWIMMING_OPEN_WATER, Ex.EXERCISE_TYPE_SWIMMING_POOL -> "swimming"
        Ex.EXERCISE_TYPE_ROWING, Ex.EXERCISE_TYPE_ROWING_MACHINE -> "rowing"
        Ex.EXERCISE_TYPE_ELLIPTICAL -> "elliptical"
        Ex.EXERCISE_TYPE_STAIR_CLIMBING, Ex.EXERCISE_TYPE_STAIR_CLIMBING_MACHINE -> "stairClimber"
        Ex.EXERCISE_TYPE_YOGA -> "yoga"
        Ex.EXERCISE_TYPE_PILATES, Ex.EXERCISE_TYPE_GUIDED_BREATHING, Ex.EXERCISE_TYPE_STRETCHING -> "mindAndBody"
        Ex.EXERCISE_TYPE_STRENGTH_TRAINING, Ex.EXERCISE_TYPE_WEIGHTLIFTING, Ex.EXERCISE_TYPE_CALISTHENICS -> "strength"
        Ex.EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING -> "hiit"
        Ex.EXERCISE_TYPE_DANCING -> "dance"
        Ex.EXERCISE_TYPE_GOLF -> "golf"
        Ex.EXERCISE_TYPE_BADMINTON -> "badminton"
        Ex.EXERCISE_TYPE_TENNIS -> "tennis"
        Ex.EXERCISE_TYPE_BASKETBALL -> "basketball"
        Ex.EXERCISE_TYPE_SOCCER -> "soccer"
        Ex.EXERCISE_TYPE_BOOT_CAMP -> "crossTraining"
        Ex.EXERCISE_TYPE_EXERCISE_CLASS -> "mixedCardio"
        else -> "other"
    }
}
