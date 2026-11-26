package de.arianw.kladde.sync

import androidx.health.connect.client.records.ActiveCaloriesBurnedRecord
import androidx.health.connect.client.records.BloodGlucoseRecord
import androidx.health.connect.client.records.BloodPressureRecord
import androidx.health.connect.client.records.BodyFatRecord
import androidx.health.connect.client.records.BodyTemperatureRecord
import androidx.health.connect.client.records.DistanceRecord
import androidx.health.connect.client.records.FloorsClimbedRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.HeartRateVariabilityRmssdRecord
import androidx.health.connect.client.records.LeanBodyMassRecord
import androidx.health.connect.client.records.OxygenSaturationRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.RespiratoryRateRecord
import androidx.health.connect.client.records.RestingHeartRateRecord
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.Vo2MaxRecord
import androidx.health.connect.client.records.WeightRecord
import java.time.Instant

/** Translates a Health Connect record of any kind into [Sample]s; the apps' package names stay as they are. */
object RecordSamples {
    const val PEBBLE_APP = "coredevices.coreapp"
    private const val STAGE_UNKNOWN = 0
    private const val STAGE_OUT_OF_BED = 3

    fun samplesOf(r: Record): List<Sample> {
        val id = r.metadata.id
        val app = r.metadata.dataOrigin.packageName
        val at = { kind: Kind, t: Instant, v: Double -> Sample(kind, id, app, t, t, v) }
        return when (r) {
            is StepsRecord -> listOf(Sample(Kind.STEPS, id, app, r.startTime, r.endTime, r.count.toDouble()))
            is DistanceRecord -> listOf(Sample(Kind.DISTANCE, id, app, r.startTime, r.endTime, r.distance.inMeters))
            is ActiveCaloriesBurnedRecord -> listOf(Sample(Kind.ACTIVE_ENERGY, id, app, r.startTime, r.endTime, r.energy.inKilocalories))
            is FloorsClimbedRecord -> listOf(Sample(Kind.FLIGHTS_CLIMBED, id, app, r.startTime, r.endTime, r.floors))
            is HeartRateRecord -> r.samples.map {
                Sample(Kind.HEART_RATE, id, app, it.time, it.time, it.beatsPerMinute.toDouble())
            }
            is RestingHeartRateRecord -> listOf(at(Kind.RESTING_HEART_RATE, r.time, r.beatsPerMinute.toDouble()))
            is WeightRecord -> listOf(at(Kind.WEIGHT, r.time, r.weight.inKilograms))
            is BodyFatRecord -> listOf(at(Kind.BODY_FAT, r.time, r.percentage.value))
            is LeanBodyMassRecord -> listOf(at(Kind.LEAN_BODY_MASS, r.time, r.mass.inKilograms))
            is BloodPressureRecord -> listOf(
                at(Kind.BLOOD_PRESSURE_SYSTOLIC, r.time, r.systolic.inMillimetersOfMercury),
                at(Kind.BLOOD_PRESSURE_DIASTOLIC, r.time, r.diastolic.inMillimetersOfMercury),
            )
            is BloodGlucoseRecord -> listOf(at(Kind.BLOOD_GLUCOSE, r.time, r.level.inMilligramsPerDeciliter))
            is OxygenSaturationRecord -> listOf(at(Kind.OXYGEN_SATURATION, r.time, r.percentage.value))
            is HeartRateVariabilityRmssdRecord -> listOf(at(Kind.HRV_RMSSD, r.time, r.heartRateVariabilityMillis))
            is RespiratoryRateRecord -> listOf(at(Kind.RESPIRATORY_RATE, r.time, r.rate))
            is Vo2MaxRecord -> listOf(at(Kind.VO2_MAX, r.time, r.vo2MillilitersPerMinuteKilogram))
            is BodyTemperatureRecord -> listOf(at(Kind.BODY_TEMPERATURE, r.time, r.temperature.inCelsius))
            is SleepSessionRecord -> sleepSamples(r, id, app)
            else -> emptyList()
        }
    }

    private fun sleepSamples(r: SleepSessionRecord, id: String, app: String): List<Sample> {
        val known = r.stages.any { it.stage != STAGE_UNKNOWN && it.stage != STAGE_OUT_OF_BED }
        if (r.stages.isEmpty()) {
            // A session without stages is one block of plain sleep.
            return listOf(Sample(Kind.SLEEP_STAGE, id, app, r.startTime, r.endTime, 0.0, STAGE_UNKNOWN, false))
        }
        return r.stages.filter { it.endTime.isAfter(it.startTime) }.map {
            Sample(Kind.SLEEP_STAGE, id, app, it.startTime, it.endTime, 0.0, it.stage, known)
        }
    }
}
