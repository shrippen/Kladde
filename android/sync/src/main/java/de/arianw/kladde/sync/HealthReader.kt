package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.changes.Change
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import java.time.Instant
import kotlin.reflect.KClass

/** What every source asks of Health Connect: the changes since a token, and a range of records. */
internal class HealthReader(private val client: HealthConnectClient) {

    /** [next]: the token to read from the next time. */
    class Changes(val list: List<Change>, val next: String)

    /** Every page of changes since [token]; null when it expired (older than Health Connect keeps them, 30 days). */
    suspend fun changesSince(token: String): Changes? {
        var next = token
        val list = mutableListOf<Change>()
        while (true) {
            val response = client.getChanges(next)
            if (response.changesTokenExpired) {
                return null
            }
            list += response.changes
            next = response.nextChangesToken
            if (!response.hasMore) {
                return Changes(list, next)
            }
        }
    }

    /** Every app's records of [type] in the range, page by page. */
    suspend fun <T : Record> all(type: KClass<T>, start: Instant, end: Instant): List<T> {
        val out = mutableListOf<T>()
        var page: String? = null
        do {
            val response = client.readRecords(
                ReadRecordsRequest(type, TimeRangeFilter.between(start, end), pageSize = PAGE_SIZE, pageToken = page)
            )
            out += response.records
            page = response.pageToken
        } while (page != null)
        return out
    }

    private companion object {
        const val PAGE_SIZE = 5000
    }
}

/**
 * A stored cursor: the types it was made for, [suffix], and the changes token. Another set of
 * types gives no token, so the source reads its window again. Stored on phones: keep the format.
 *
 *   BodyFatRecord,WeightRecord<suffix>|<token>
 */
internal class Cursor(types: Set<KClass<out Record>>, private val suffix: String = "") {

    private val signature = types.map { it.simpleName }.sortedBy { it }.joinToString(",") + suffix

    fun of(token: String) = "$signature$SEPARATOR$token"

    /** The token of a cursor made for these types; null for none or another one. */
    fun tokenOf(cursor: String?): String? = cursor?.removePrefix("$signature$SEPARATOR")?.takeIf { it != cursor }

    private companion object {
        const val SEPARATOR = "|"
    }
}
