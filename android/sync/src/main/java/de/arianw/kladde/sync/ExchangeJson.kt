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
                "Steps", "Distance" -> {
                    val steps = type == "Steps"
                    val value = r.optDouble(if (steps) "count" else "meters", -1.0)
                    val start = time(r, "start")
                    val end = time(r, "end")
                    if (start == null || end == null || value < 0) {
                        warnings += "record $i ($type): bad interval or value"
                        continue
                    }
                    samples += Sample(if (steps) Kind.STEPS else Kind.DISTANCE, id, app, start, end, value)
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
                        samples += Sample(Kind.HEART_RATE, id, app, t, t, bpm)
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
