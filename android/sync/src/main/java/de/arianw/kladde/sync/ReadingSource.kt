package de.arianw.kladde.sync

import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.changes.UpsertionChange
import androidx.health.connect.client.records.BloodGlucoseRecord
import androidx.health.connect.client.records.BloodPressureRecord
import androidx.health.connect.client.records.BodyFatRecord
import androidx.health.connect.client.records.BodyTemperatureRecord
import androidx.health.connect.client.records.HeartRateVariabilityRmssdRecord
import androidx.health.connect.client.records.LeanBodyMassRecord
import androidx.health.connect.client.records.OxygenSaturationRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.RespiratoryRateRecord
import androidx.health.connect.client.records.RestingHeartRateRecord
import androidx.health.connect.client.records.Vo2MaxRecord
import androidx.health.connect.client.records.WeightRecord
import androidx.health.connect.client.request.ChangesTokenRequest
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import java.time.Instant
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Reads single readings (weight, blood pressure, SpO₂ …) of every app, each one entry, with
 * a changes token of its own; only the [types] the user allowed.
 *
 * The cursor holds the types it was made for: when the allowed types change, a new token
 * and the backfill window again (HealthLog keeps each reading's first write, a resend is a
 * duplicate). Known gaps: HealthLog keeps a reading's first write, so a reading corrected
 * or deleted in Health Connect stays as it was; the same weighing written by two apps comes
 * twice (no app order for readings yet).
 */
class ReadingSource(
    private val client: HealthConnectClient,
    private val types: Set<KClass<out Record>>,
) {

    class Read(val samples: List<Sample>, val cursor: String)

    private val signature = types.map { it.simpleName }.sortedBy { it }.joinToString(",")

    suspend fun read(cursor: String?, zone: ZoneId, backfillDays: Long, now: Instant): Read {
        val token = cursor?.removePrefix("$signature$SEPARATOR")?.takeIf { it != cursor }
        if (token == null) {
            return backfill(zone, backfillDays, now)
        }

        var next: String = token
        val samples = mutableListOf<Sample>()
        while (true) {
            val response = client.getChanges(next)
            if (response.changesTokenExpired) {
                return backfill(zone, backfillDays, now)
            }
            for (change in response.changes) {
                val record = (change as? UpsertionChange)?.record ?: continue
                samples += HealthConnectSource.samplesOf(record)
            }
            next = response.nextChangesToken
            if (!response.hasMore) {
                break
            }
        }
        return Read(samples, cursorOf(next))
    }

    private suspend fun backfill(zone: ZoneId, backfillDays: Long, now: Instant): Read {
        // The token first: what changes while we read comes again next time.
        val token = client.getChangesToken(ChangesTokenRequest(types))
        val first = Mapper.dayOf(now, zone).minusDays(backfillDays - 1)
        val (start, _) = DayWindow.bounds(first, zone)

        val samples = mutableListOf<Sample>()
        for (type in types) {
            var page: String? = null
            do {
                val response = client.readRecords(
                    ReadRecordsRequest(type, TimeRangeFilter.between(start, now), pageSize = PAGE_SIZE, pageToken = page)
                )
                samples += response.records.flatMap(HealthConnectSource::samplesOf)
                page = response.pageToken
            } while (page != null)
        }
        return Read(samples, cursorOf(token))
    }

    private fun cursorOf(token: String) = "$signature$SEPARATOR$token"

    companion object {
        private const val PAGE_SIZE = 5000
        private const val SEPARATOR = "|"

        val TYPES: Set<KClass<out Record>> = setOf(
            RestingHeartRateRecord::class,
            WeightRecord::class,
            BodyFatRecord::class,
            LeanBodyMassRecord::class,
            BloodPressureRecord::class,
            BloodGlucoseRecord::class,
            OxygenSaturationRecord::class,
            HeartRateVariabilityRmssdRecord::class,
            RespiratoryRateRecord::class,
            Vo2MaxRecord::class,
            BodyTemperatureRecord::class,
        )
    }
}
