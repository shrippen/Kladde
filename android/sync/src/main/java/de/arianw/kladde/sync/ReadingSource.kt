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

    private val reader = HealthReader(client)
    private val cursors = Cursor(types)

    suspend fun read(cursor: String?, zone: ZoneId, backfillDays: Long, now: Instant): Read {
        val token = cursors.tokenOf(cursor)
        if (token == null) {
            return backfill(zone, backfillDays, now)
        }

        val changes = reader.changesSince(token) ?: return backfill(zone, backfillDays, now)
        val samples = mutableListOf<Sample>()
        for (change in changes.list) {
            val record = (change as? UpsertionChange)?.record ?: continue
            samples += RecordSamples.samplesOf(record)
        }
        return Read(samples, cursors.of(changes.next))
    }

    private suspend fun backfill(zone: ZoneId, backfillDays: Long, now: Instant): Read {
        // The token first: what changes while we read comes again next time.
        val token = client.getChangesToken(ChangesTokenRequest(types))
        val first = Mapper.dayOf(now, zone).minusDays(backfillDays - 1)
        val (start, _) = DayWindow.bounds(first, zone)

        val samples = mutableListOf<Sample>()
        for (type in types) {
            samples += reader.all(type, start, now).flatMap(RecordSamples::samplesOf)
        }
        return Read(samples, cursors.of(token))
    }

    companion object {
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
