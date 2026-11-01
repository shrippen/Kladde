package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.aggregate.AggregationResult
import androidx.health.connect.client.aggregate.AggregationResultGroupedByDuration
import androidx.health.connect.client.changes.Change
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.response.ReadRecordsResponse
import androidx.health.connect.client.request.AggregateGroupByDurationRequest
import androidx.health.connect.client.request.AggregateRequest
import androidx.health.connect.client.response.ChangesResponse
import java.lang.reflect.Proxy
import java.time.Instant
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

    val calls = mutableListOf<String>()
    val aggregated = mutableListOf<Pair<Instant, Instant>>()

    val client: HealthConnectClient = Proxy.newProxyInstance(
        HealthConnectClient::class.java.classLoader, arrayOf(HealthConnectClient::class.java),
    ) { _, method, args ->
        calls += method.name
        when (method.name) {
            "getChangesToken" -> "token-new"
            "getChanges" -> ChangesResponse(changes, "token-next", false, false)
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
            "readRecords" -> {
                val type = (args[0] as ReadRecordsRequest<*>).recordType
                ReadRecordsResponse(records.filter { type.isInstance(it) }, null)
            }
            else -> error("not faked: ${method.name}")
        }
    } as HealthConnectClient

    private fun AggregateGroupByDurationRequest.getTimeRangeFilter(): Any =
        javaClass.getDeclaredMethod("getTimeRangeFilter\$connect_client_release").invoke(this)

    private fun timeRange(filter: Any): Pair<Instant, Instant> {
        val start = filter.javaClass.getDeclaredMethod("getStartTime").invoke(filter) as Instant
        val end = filter.javaClass.getDeclaredMethod("getEndTime").invoke(filter) as Instant
        return start to end
    }
}
