package de.arianw.kladde.sync

import androidx.health.connect.client.records.ExerciseSessionRecord as Ex
import org.json.JSONObject
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import kotlin.math.roundToInt

/** One exercise session as Health Connect holds it, before any HealthLog mapping. */
data class Session(
    val id: String,
    val app: String,
    val type: Int, // ExerciseSessionRecord.EXERCISE_TYPE_*
    val title: String?,
    val start: Instant,
    val end: Instant,
)

/** One training for HealthLog: one session, or the sections of one (Freeletics). */
class Workout(val sessions: List<Session>) {
    val first: Session get() = sessions.first()
    val start: Instant get() = first.start
    val end: Instant get() = sessions.maxOf { it.end }

    /** The gaps between the sections. */
    val pause: Duration get() = Duration.between(start, end) - sessions.fold(Duration.ZERO) { d, s -> d + Duration.between(s.start, s.end) }
}

/**
 * Exercise sessions to entries for POST /api/workouts/batch.
 *
 *   sessions ──merge──▶ trainings ──overlap: app order wins──▶ settled, on the days asked ──▶ JSON
 *
 * Freeletics writes one session per section (Warmup 09:02–09:08, Intervall 09:09–09:18,
 * Cooldown 09:19–09:25): sessions of one app and sport with at most [MAX_GAP] between them
 * are one training, the gaps its pause. Where trainings of two apps overlap (the Pebble's
 * "Workout" during a Freeletics one), only the one whose app comes first in the order stays;
 * apps not in the order come last. HealthLog keeps the first write of a training, so it is
 * sent only once it ended [SETTLE] ago: by then the sections and the other apps are in.
 */
object WorkoutMapper {

    val MAX_GAP: Duration = Duration.ofMinutes(5)
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
     * The trainings to send: merged, overlaps resolved, settled at [now], and touching [days]
     * (changed) or settled since the last read at [since] (held then, unchanged since).
     */
    fun select(sessions: List<Session>, order: List<String>, days: Set<LocalDate>, zone: ZoneId, now: Instant, since: Instant? = null): List<Workout> {
        val rank = { app: String -> order.indexOf(app).takeIf { it >= 0 } ?: order.size }
        val kept = mutableListOf<Workout>()

        for (w in merge(sessions).sortedWith(compareBy({ rank(it.first.app) }, { it.start }))) {
            if (kept.none { it.start < w.end && w.start < it.end }) {
                kept += w
            }
        }

        return kept.filter { w ->
            val settledAt = w.end.plus(SETTLE)
            val changed = Mapper.dayOf(w.start, zone) in days || Mapper.dayOf(w.end, zone) in days
            val newlySettled = since != null && settledAt.isAfter(since)
            !settledAt.isAfter(now) && (changed || newlySettled)
        }.sortedBy { it.start }
    }

    /** Sections of one app and sport, at most [MAX_GAP] apart, as one training. */
    private fun merge(sessions: List<Session>): List<Workout> {
        val out = mutableListOf<Workout>()
        val valid = sessions.filter { it.end.isAfter(it.start) }

        for ((_, group) in valid.groupBy { it.app to it.type }) {
            var current = mutableListOf<Session>()
            for (s in group.sortedBy { it.start }) {
                if (current.isNotEmpty() && Duration.between(current.maxOf { it.end }, s.start) > MAX_GAP) {
                    out += Workout(current)
                    current = mutableListOf()
                }
                current += s
            }
            if (current.isNotEmpty()) {
                out += Workout(current)
            }
        }
        return out
    }

    /**
     * One entry. Heart rate from every app (watch and phone agree, no sum); steps and
     * distance only from the training's own app, as the phone and the watch both count them.
     */
    fun entry(w: Workout, samples: List<Sample>): JSONObject {
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

    private fun metadata(w: Workout): JSONObject {
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
