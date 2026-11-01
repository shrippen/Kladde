package de.arianw.kladde.sync

import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

/** One session (training, sleep) as Health Connect holds it, before any HealthLog mapping. */
data class Session(
    val id: String,
    val app: String,
    val type: Int, // ExerciseSessionRecord.EXERCISE_TYPE_*, 0 for sleep
    val title: String?,
    val start: Instant,
    val end: Instant,
)

/** One session, or the sections of one (Freeletics). */
class SessionGroup(val sessions: List<Session>) {
    val first: Session get() = sessions.first()
    val start: Instant get() = first.start
    val end: Instant get() = sessions.maxOf { it.end }

    /** The gaps between the sections. */
    val pause: Duration get() = Duration.between(start, end) - sessions.fold(Duration.ZERO) { d, s -> d + Duration.between(s.start, s.end) }
}

/**
 * Which sessions of several apps go to HealthLog.
 *
 *   sessions ──merge──▶ groups ──overlap: app order wins──▶ settled, on the days asked
 *
 * Sessions of one app and type at most [MAX_GAP] apart are one group. Where groups of two
 * apps overlap, only the one whose app comes first in the order stays; apps not in the order
 * come last. HealthLog keeps the first write, so a group goes only once it ended `settle`
 * ago: by then its sections and the other apps' sessions are in.
 */
object Sessions {

    val MAX_GAP: Duration = Duration.ofMinutes(5)

    /**
     * The groups to send: merged, overlaps resolved, settled at [now], and touching [days]
     * (changed) or settled since the last read at [since] (held then, unchanged since).
     */
    fun select(
        sessions: List<Session>,
        order: List<String>,
        days: Set<LocalDate>,
        zone: ZoneId,
        now: Instant,
        settle: Duration,
        since: Instant? = null,
    ): List<SessionGroup> {
        val rank = { app: String -> order.indexOf(app).takeIf { it >= 0 } ?: order.size }
        val kept = mutableListOf<SessionGroup>()

        for (g in merge(sessions).sortedWith(compareBy({ rank(it.first.app) }, { it.start }))) {
            if (kept.none { it.start < g.end && g.start < it.end }) {
                kept += g
            }
        }

        return kept.filter { g ->
            val settledAt = g.end.plus(settle)
            val changed = Mapper.dayOf(g.start, zone) in days || Mapper.dayOf(g.end, zone) in days
            val newlySettled = since != null && settledAt.isAfter(since)
            !settledAt.isAfter(now) && (changed || newlySettled)
        }.sortedBy { it.start }
    }

    /** Sessions of one app and type, at most [MAX_GAP] apart, as one group. */
    private fun merge(sessions: List<Session>): List<SessionGroup> {
        val out = mutableListOf<SessionGroup>()
        val valid = sessions.filter { it.end.isAfter(it.start) }

        for ((_, group) in valid.groupBy { it.app to it.type }) {
            var current = mutableListOf<Session>()
            for (s in group.sortedBy { it.start }) {
                if (current.isNotEmpty() && Duration.between(current.maxOf { it.end }, s.start) > MAX_GAP) {
                    out += SessionGroup(current)
                    current = mutableListOf()
                }
                current += s
            }
            if (current.isNotEmpty()) {
                out += SessionGroup(current)
            }
        }
        return out
    }
}
