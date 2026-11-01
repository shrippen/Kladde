package de.arianw.kladde.sync

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

/**
 * Day totals and heart-rate buckets overwrite on the server, so they must be built from a
 * complete day. When anything changes on a day, the source re-reads that whole day; this
 * keeps only the samples that belong to the days being sent.
 *
 *   day D re-read:  [23:30 D-1 ──── 00:20 D]  [08:00 D]  [23:55 D ── 00:05 D+1]
 *   kept for D:                                [08:00 D]  [23:55 D ...]
 *
 * A record that starts on D-1 would otherwise yield a partial total for D-1 that overwrites
 * the full one.
 */
object DayWindow {

    /** Kinds sent as day totals or buckets: re-read and send whole days. */
    val DAY_KINDS = setOf(Kind.STEPS, Kind.DISTANCE, Kind.HEART_RATE)

    fun keep(samples: List<Sample>, days: Set<LocalDate>, zone: ZoneId): List<Sample> =
        samples.filter { it.kind !in DAY_KINDS || Mapper.dayOf(it.start, zone) in days }

    /** [start, end) of a local day. */
    fun bounds(day: LocalDate, zone: ZoneId): Pair<Instant, Instant> =
        day.atStartOfDay(zone).toInstant() to day.plusDays(1).atStartOfDay(zone).toInstant()

    /** The local days from [first] to [last], both included. */
    fun range(first: LocalDate, last: LocalDate): Set<LocalDate> {
        val out = linkedSetOf<LocalDate>()
        var d = first
        while (!d.isAfter(last)) {
            out += d
            d = d.plusDays(1)
        }
        return out
    }
}
