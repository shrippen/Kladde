package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.aggregate.AggregationResult
import androidx.health.connect.client.aggregate.AggregationResultGroupedByDuration
import androidx.health.connect.client.aggregate.AggregationResultGroupedByPeriod
import androidx.health.connect.client.changes.Change
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.response.ReadRecordsResponse
import androidx.health.connect.client.request.AggregateGroupByDurationRequest
import androidx.health.connect.client.request.AggregateGroupByPeriodRequest
import androidx.health.connect.client.request.AggregateRequest
import androidx.health.connect.client.response.ChangesResponse
import java.lang.reflect.Proxy
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneOffset

/**
 * Health Connect for the tests: the calls the sources make, answered from fields; any other
 * call fails. Suspend functions answer at once (the proxy returns, never suspends).
 */
class FakeHealthConnect {

    /** Day totals by metric key (Long and Double metrics), for any aggregate asked. */
    var longs: Map<String, Long> = emptyMap()
    var doubles: Map<String, Double> = emptyMap()

    /** Heart-rate buckets: start -> (avg, min, max). */
    var buckets: Map<Instant, Triple<Long, Long, Long>> = emptyMap()

    var changes: List<Change> = emptyList()

    /** What readRecords finds, any time asked. */
    var records: List<Record> = emptyList()

    /** Pages of this many changes (tokens "page-N"); null: all in one. */
    var changesPerPage: Int? = null

    /** Every changes token expired. */
    var tokenExpired = false

    /** Pages of this many records (tokens are offsets); null: all in one. */
    var recordsPerPage: Int? = null

    val calls = mutableListOf<String>()

    /** The time ranges readRecords was asked for. */
    val ranges = mutableListOf<Pair<Instant, Instant>>()
    val aggregated = mutableListOf<Pair<Instant, Instant>>()

    /** The local ranges asked by day (aggregateGroupByPeriod); each day answers longs/doubles. */
    val periods = mutableListOf<Pair<LocalDateTime, LocalDateTime>>()

    val client: HealthConnectClient = Proxy.newProxyInstance(
        HealthConnectClient::class.java.classLoader, arrayOf(HealthConnectClient::class.java),
    ) { _, method, args ->
        calls += method.name
        when (method.name) {
            "getChangesToken" -> "token-new"
            "getChanges" -> changesPage(args[0] as String)
            "aggregate" -> {
                val filter = (args[0] as AggregateRequest).let { it.javaClass.getDeclaredMethod("getTimeRangeFilter\$connect_client_release").invoke(it) }
                aggregated += timeRange(filter)
                AggregationResult(longs, doubles, emptySet())
            }
            "aggregateGroupByDuration" -> {
                val (from, to) = timeRange((args[0] as AggregateGroupByDurationRequest).getTimeRangeFilter())
                buckets.filterKeys { !it.isBefore(from) && it.isBefore(to) }.map { (start, v) ->
                    val result = AggregationResult(mapOf(
                        HeartRateRecord.BPM_AVG.metricKey to v.first,
                        HeartRateRecord.BPM_MIN.metricKey to v.second,
                        HeartRateRecord.BPM_MAX.metricKey to v.third,
                    ), emptyMap(), emptySet())
                    AggregationResultGroupedByDuration(result, start, start.plusSeconds(600), ZoneOffset.UTC)
                }
            }
            "aggregateGroupByPeriod" -> {
                val filter = (args[0] as AggregateGroupByPeriodRequest).let { it.javaClass.getDeclaredMethod("getTimeRangeFilter\$connect_client_release").invoke(it) }
                val from = filter.javaClass.getDeclaredMethod("getLocalStartTime").invoke(filter) as LocalDateTime
                val to = filter.javaClass.getDeclaredMethod("getLocalEndTime").invoke(filter) as LocalDateTime
                periods += from to to
                generateSequence(from) { it.plusDays(1) }.takeWhile { it.isBefore(to) }.map {
                    AggregationResultGroupedByPeriod(AggregationResult(longs, doubles, emptySet()), it, it.plusDays(1))
                }.toList()
            }
            "readRecords" -> {
                val request = args[0] as ReadRecordsRequest<*>
                ranges += timeRange(request.timeRangeFilter)
                val found = records.filter { request.recordType.isInstance(it) }
                val per = recordsPerPage
                val from = request.pageToken?.toInt() ?: 0
                if (per == null) {
                    ReadRecordsResponse(found, null)
                } else {
                    ReadRecordsResponse(found.drop(from).take(per), if (from + per < found.size) "${from + per}" else null)
                }
            }
            else -> error("not faked: ${method.name}")
        }
    } as HealthConnectClient

    private fun changesPage(token: String): ChangesResponse {
        val per = changesPerPage
        if (tokenExpired) {
            return ChangesResponse(emptyList(), "token-next", false, true)
        }
        if (per == null) {
            return ChangesResponse(changes, "token-next", false, false)
        }
        val page = token.removePrefix("page-").toIntOrNull() ?: 0
        val more = (page + 1) * per < changes.size
        return ChangesResponse(changes.drop(page * per).take(per), if (more) "page-${page + 1}" else "token-next", more, false)
    }

    private fun AggregateGroupByDurationRequest.getTimeRangeFilter(): Any =
        javaClass.getDeclaredMethod("getTimeRangeFilter\$connect_client_release").invoke(this)

    private fun timeRange(filter: Any): Pair<Instant, Instant> {
        val start = filter.javaClass.getDeclaredMethod("getStartTime").invoke(filter) as Instant
        val end = filter.javaClass.getDeclaredMethod("getEndTime").invoke(filter) as Instant
        return start to end
    }
}
