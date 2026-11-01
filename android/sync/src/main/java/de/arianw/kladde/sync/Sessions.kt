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
 * The app order over time: a session takes the order in force when it began, so a reorder
 * never changes the winner of one already sent (HealthLog would keep both). The beginning,
 * not the end: a rival of a sent session began before that one ended, so before any later
 * change, even if it ends after it.
 *
 *   past [Pebble, Sleep] until 1.11. 10:00, current [Sleep, Pebble]
 *   night began 31.10. 22:00 ─▶ Pebble's       night began 1.11. 22:00 ─▶ Sleep's
 */
class Ranking(val current: List<String>, val past: List<Era> = emptyList()) {

    /** [order] was in force for sessions that began before [until]. */
    class Era(val until: Instant, val order: List<String>)

    fun at(start: Instant): List<String> =
        past.filter { start.isBefore(it.until) }.minByOrNull { it.until }?.order ?: current

    /** After the user set [order] at [now]; the order before stays for what began until then. */
    fun changed(order: List<String>, now: Instant): Ranking =
        if (order == current) this else Ranking(order, past + Era(now, current))
}

/**
 * Which sessions of several apps go to HealthLog.
 *
 *   sessions ──merge──▶ groups ──overlap: app order wins──▶ settled, on the days asked
 *
 * Sessions of one app and type at most [MAX_GAP] apart are one group. Where groups of two
 * apps overlap, only the one whose app comes first in the order stays; apps not in the order
 * come last, by name (as the app lists them, AppOrder.shown). HealthLog keeps the first write, so a group goes only once it ended `settle`
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
        ranking: Ranking,
        days: Set<LocalDate>,
        zone: ZoneId,
        now: Instant,
        settle: Duration,
        since: Instant? = null,
    ): List<SessionGroup> {
        val rank = { g: SessionGroup -> ranking.at(g.start).let { o -> o.indexOf(g.first.app).takeIf { it >= 0 } ?: o.size } }
        val unknown = { g: SessionGroup -> if (g.first.app in ranking.at(g.start)) "" else g.first.app }
        val kept = mutableListOf<SessionGroup>()

        for (g in merge(sessions).sortedWith(compareBy({ rank(it) }, { unknown(it) }, { it.start }))) {
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
