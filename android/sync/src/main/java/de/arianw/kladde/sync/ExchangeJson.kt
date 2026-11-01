package de.arianw.kladde.sync

import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.format.DateTimeParseException

/**
 * Kladde's exchange format (core/hcjson.h): what the probe exports and kladde-push reads.
 * On Android the sync reads Health Connect directly; this parser serves the shared test
 * fixtures, so both implementations are checked against the same input.
 */
object ExchangeJson {

    private const val STAGE_UNKNOWN = 0
    private const val STAGE_OUT_OF_BED = 3

    class Parsed(val samples: List<Sample>, val warnings: List<String>)

    /** Day kinds over an interval: exchange type to kind and the value's key. */
    private val DAY_TYPES = mapOf(
        "Steps" to (Kind.STEPS to "count"),
        "Distance" to (Kind.DISTANCE to "meters"),
        "ActiveCaloriesBurned" to (Kind.ACTIVE_ENERGY to "kcal"),
        "FloorsClimbed" to (Kind.FLIGHTS_CLIMBED to "floors"),
    )

    /** Readings with one "value": exchange type to kind. */
    private val READINGS = mapOf(
        "Weight" to Kind.WEIGHT,
        "BodyFat" to Kind.BODY_FAT,
        "LeanBodyMass" to Kind.LEAN_BODY_MASS,
        "BloodGlucose" to Kind.BLOOD_GLUCOSE,
        "OxygenSaturation" to Kind.OXYGEN_SATURATION,
        "HeartRateVariabilityRmssd" to Kind.HRV_RMSSD,
        "RespiratoryRate" to Kind.RESPIRATORY_RATE,
        "Vo2Max" to Kind.VO2_MAX,
        "BodyTemperature" to Kind.BODY_TEMPERATURE,
    )

    fun parse(json: String): Parsed {
        val records = JSONObject(json).getJSONArray("records")
        val samples = mutableListOf<Sample>()
        val warnings = mutableListOf<String>()

        for (i in 0 until records.length()) {
            val r = records.getJSONObject(i)
            val type = r.optString("type")
            val id = r.optString("id")
            val app = r.optString("app")
            if (id.isEmpty()) {
                warnings += "record $i ($type): no id"
                continue
            }

            when (type) {
                in DAY_TYPES.keys -> {
                    val (kind, key) = DAY_TYPES.getValue(type)
                    val value = r.optDouble(key, -1.0)
                    val start = time(r, "start")
                    val end = time(r, "end")
                    if (start == null || end == null || value < 0) {
                        warnings += "record $i ($type): bad interval or value"
                        continue
                    }
                    samples += Sample(kind, id, app, start, end, value)
                }

                "HeartRate" -> {
                    val points = r.optJSONArray("samples") ?: JSONArray()
                    for (p in 0 until points.length()) {
                        val point = points.getJSONObject(p)
                        val t = time(point, "time")
                        val bpm = point.optDouble("bpm", -1.0)
                        if (t == null || bpm <= 0) {
                            warnings += "record $i ($type): bad heart rate sample"
                            continue
                        }
                        // A bucket (min ≤ bpm ≤ max) or a single reading.
                        val bucket = point.has("min") || point.has("max")
                        val min = point.optDouble("min", -1.0)
                        val max = point.optDouble("max", -1.0)
                        if (bucket && !(min > 0 && min <= bpm && bpm <= max)) {
                            warnings += "record $i ($type): bad heart rate sample"
                            continue
                        }
                        samples += Sample(Kind.HEART_RATE, id, app, t, t, bpm, min = min.takeIf { bucket }, max = max.takeIf { bucket })
                    }
                }

                "RestingHeartRate" -> {
                    val t = time(r, "time")
                    val bpm = r.optDouble("bpm", -1.0)
                    if (t == null || bpm <= 0) {
                        warnings += "record $i ($type): bad resting heart rate"
                        continue
                    }
                    samples += Sample(Kind.RESTING_HEART_RATE, id, app, t, t, bpm)
                }

                in READINGS.keys, "BloodPressure" -> {
                    val t = time(r, "time")
                    val readings = if (type == "BloodPressure") {
                        listOf(Kind.BLOOD_PRESSURE_SYSTOLIC to "systolic", Kind.BLOOD_PRESSURE_DIASTOLIC to "diastolic")
                    } else {
                        listOf(READINGS.getValue(type) to "value")
                    }.map { (kind, key) -> Sample(kind, id, app, t ?: Instant.EPOCH, t ?: Instant.EPOCH, r.optDouble(key, -1.0)) }
                    // A blood pressure is the pair; half of one is no reading.
                    if (t == null || readings.any { it.value <= 0 }) {
                        warnings += "record $i ($type): bad reading"
                        continue
                    }
                    samples += readings
                }

                "SleepSession" -> {
                    val stages = r.optJSONArray("stages") ?: JSONArray()
                    val list = (0 until stages.length()).map { stages.getJSONObject(it) }
                    val known = list.any {
                        val s = it.optInt("stage")
                        s != STAGE_UNKNOWN && s != STAGE_OUT_OF_BED
                    }

                    // A session without stages is one block of plain sleep.
                    val blocks = list.ifEmpty { listOf(JSONObject().put("start", r.opt("start")).put("end", r.opt("end")).put("stage", STAGE_UNKNOWN)) }
                    for (b in blocks) {
                        val start = time(b, "start")
                        val end = time(b, "end")
                        if (start == null || end == null || !end.isAfter(start)) {
                            warnings += "record $i ($type): bad sleep stage interval"
                            continue
                        }
                        samples += Sample(Kind.SLEEP_STAGE, id, app, start, end, 0.0, b.optInt("stage"), known)
                    }
                }

                else -> warnings += "record $i ($type): type not mapped"
            }
        }
        return Parsed(samples, warnings)
    }

    private fun time(o: JSONObject, key: String): Instant? {
        val text = o.optString(key)
        if (text.isEmpty()) {
            return null
        }
        return try {
            Instant.parse(text)
        } catch (e: DateTimeParseException) {
            null
        }
    }
}
