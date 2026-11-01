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
 * the full one. A heart-rate bucket belongs to the day its start falls on; where local
 * midnight is not on a 10-minute mark (UTC+5:45) the last bucket reaches into the next day,
 * so heart rate is read up to the end of that bucket ([readEnd]).
 */
object DayWindow {

    /** Kinds sent as day totals or buckets: re-read and send whole days. */
    val DAY_KINDS = setOf(Kind.STEPS, Kind.DISTANCE, Kind.ACTIVE_ENERGY, Kind.FLIGHTS_CLIMBED, Kind.HEART_RATE)

    fun keep(samples: List<Sample>, days: Set<LocalDate>, zone: ZoneId): List<Sample> =
        samples.filter { it.kind !in DAY_KINDS || Mapper.dayOfSample(it, zone) in days }

    /** Where reading [kind] for a day ending at [end] has to stop: the last bucket complete. */
    fun readEnd(kind: Kind, end: Instant): Instant {
        if (kind != Kind.HEART_RATE) {
            return end
        }
        val start = Mapper.bucketStart(end)
        return if (start == end) end else start.plusSeconds(600)
    }

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
