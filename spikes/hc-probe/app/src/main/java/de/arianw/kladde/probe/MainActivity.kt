package de.arianw.kladde.probe

import android.os.Bundle
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.PermissionController
import androidx.health.connect.client.permission.HealthPermission
import androidx.health.connect.client.records.*
import androidx.health.connect.client.records.ExerciseSessionRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.Record
import androidx.health.connect.client.records.RestingHeartRateRecord
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import kotlin.reflect.KClass

/**
 * Phase 0 probe: what does the Pebble app (and every other app) really write to Health
 * Connect? Shows a count per type and app, and exports the records as JSON in Kladde's
 * exchange format (core/hcjson.h), the input of kladde-push and of the test fixtures.
 *
 * Read only. Throwaway code: the app's real reader replaces it in phase 1.
 */
class MainActivity : ComponentActivity() {

    private val types: List<KClass<out Record>> = listOf(
        ActiveCaloriesBurnedRecord::class,
        BasalBodyTemperatureRecord::class,
        BasalMetabolicRateRecord::class,
        BloodGlucoseRecord::class,
        BloodPressureRecord::class,
        BodyFatRecord::class,
        BodyTemperatureRecord::class,
        BodyWaterMassRecord::class,
        BoneMassRecord::class,
        CyclingPedalingCadenceRecord::class,
        DistanceRecord::class,
        ElevationGainedRecord::class,
        ExerciseSessionRecord::class,
        FloorsClimbedRecord::class,
        HeartRateRecord::class,
        HeartRateVariabilityRmssdRecord::class,
        HeightRecord::class,
        HydrationRecord::class,
        LeanBodyMassRecord::class,
        NutritionRecord::class,
        OxygenSaturationRecord::class,
        PowerRecord::class,
        RespiratoryRateRecord::class,
        RestingHeartRateRecord::class,
        SleepSessionRecord::class,
        SpeedRecord::class,
        StepsCadenceRecord::class,
        StepsRecord::class,
        TotalCaloriesBurnedRecord::class,
        Vo2MaxRecord::class,
        WeightRecord::class,
        WheelchairPushesRecord::class,
    )
    private val permissions: Set<String> =
        types.map { HealthPermission.getReadPermission(it) }.toSet() +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_HISTORY

    private lateinit var output: TextView
    private lateinit var daysField: EditText
    private var lastExport: String? = null

    private val requestPermissions =
        registerForActivityResult(PermissionController.createRequestPermissionResultContract()) {
            granted -> show("Granted ${granted.size} of ${permissions.size} permissions")
        }

    private val saveExport =
        registerForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
            val json = lastExport ?: return@registerForActivityResult
            if (uri == null) return@registerForActivityResult
            contentResolver.openOutputStream(uri)?.use { it.write(json.toByteArray()) }
            show("Saved ${json.length / 1024} KiB")
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        daysField = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_NUMBER
            setText("7")
            hint = "days"
        }
        output = TextView(this).apply { setTextIsSelectable(true) }
        column.addView(TextView(this).apply { text = "Days back (local midnight)" })
        column.addView(daysField)
        column.addView(Button(this).apply {
            text = "Grant read access"
            setOnClickListener { requestPermissions.launch(permissions) }
        })
        column.addView(Button(this).apply {
            text = "Count per type and app"
            setOnClickListener { run(export = false) }
        })
        column.addView(Button(this).apply {
            text = "Export JSON"
            setOnClickListener { run(export = true) }
        })
        column.addView(output)
        setContentView(ScrollView(this).apply { addView(column) })

        if (HealthConnectClient.getSdkStatus(this) != HealthConnectClient.SDK_AVAILABLE) {
            show("Health Connect is not available on this device")
        }
    }

    private fun show(text: String) {
        output.text = text
    }

    private fun run(export: Boolean) {
        val days = daysField.text.toString().toLongOrNull()?.coerceIn(1, 3650) ?: 7
        val zone = ZoneId.systemDefault()
        val from = LocalDate.now(zone).minusDays(days - 1).atStartOfDay(zone).toInstant()
        val range = TimeRangeFilter.between(from, Instant.now())
        val client = HealthConnectClient.getOrCreate(this)
        show("Reading …")
        lifecycleScope.launch {
            try {
                val granted = client.permissionController.getGrantedPermissions()
                val all = mutableListOf<Record>()
                val counts = sortedMapOf<String, MutableMap<String, Int>>()
                for (type in types) {
                    if (HealthPermission.getReadPermission(type) !in granted) {
                        counts[type.simpleName ?: "?"] = mutableMapOf("(no permission)" to 0)
                        continue
                    }
                    val records = readAll(client, type, range)
                    all += records
                    val perApp = counts.getOrPut(type.simpleName ?: "?") { sortedMapOf() }
                    for (r in records) {
                        val app = r.metadata.dataOrigin.packageName
                        perApp[app] = (perApp[app] ?: 0) + 1
                    }
                }
                val summary = buildString {
                    append("Since $from ($days days, zone $zone)\n")
                    append("History permission: ")
                    append(HealthPermission.PERMISSION_READ_HEALTH_DATA_HISTORY in granted)
                    append("\n\n")
                    for ((type, perApp) in counts) {
                        append(type).append('\n')
                        if (perApp.isEmpty()) append("  (none)\n")
                        for ((app, n) in perApp) append("  ").append(app).append(": ").append(n).append('\n')
                    }
                }
                val sessions = all.filterIsInstance<ExerciseSessionRecord>().sortedBy { it.startTime }
                val detail = buildString {
                    append("\nEXERCISE\n")
                    for (s in sessions) {
                        append(s.metadata.dataOrigin.packageName).append(' ')
                        append(s.exerciseType).append(' ')
                        append(s.startTime).append(' ').append(s.endTime).append(' ')
                        append(s.title ?: "").append('\n')
                    }
                }
                show(summary + detail)
                if (export) {
                    lastExport = toJson(all, from, zone).toString(1)
                    saveExport.launch("health-connect-$days-days.json")
                }
            } catch (e: Exception) {
                show("Failed: $e")
            }
        }
    }

    private suspend fun <T : Record> readAll(
        client: HealthConnectClient,
        type: KClass<T>,
        range: TimeRangeFilter,
    ): List<T> {
        val out = mutableListOf<T>()
        var token: String? = null
        do {
            val response = client.readRecords(
                ReadRecordsRequest(type, range, pageSize = 5000, pageToken = token)
            )
            out += response.records
            token = response.pageToken
        } while (token != null)
        return out
    }

    private fun toJson(records: List<Record>, from: Instant, zone: ZoneId): JSONObject {
        val array = JSONArray()
        for (r in records) {
            val o = JSONObject()
                .put("id", r.metadata.id)
                .put("app", r.metadata.dataOrigin.packageName)
            when (r) {
                is StepsRecord -> o.put("type", "Steps")
                    .put("start", r.startTime.toString()).put("end", r.endTime.toString())
                    .put("count", r.count)
                is DistanceRecord -> o.put("type", "Distance")
                    .put("start", r.startTime.toString()).put("end", r.endTime.toString())
                    .put("meters", r.distance.inMeters)
                is HeartRateRecord -> o.put("type", "HeartRate")
                    .put("samples", JSONArray(r.samples.map {
                        JSONObject().put("time", it.time.toString()).put("bpm", it.beatsPerMinute)
                    }))
                is RestingHeartRateRecord -> o.put("type", "RestingHeartRate")
                    .put("time", r.time.toString()).put("bpm", r.beatsPerMinute)
                is SleepSessionRecord -> o.put("type", "SleepSession")
                    .put("start", r.startTime.toString()).put("end", r.endTime.toString())
                    .put("stages", JSONArray(r.stages.map {
                        JSONObject().put("start", it.startTime.toString())
                            .put("end", it.endTime.toString()).put("stage", it.stage)
                    }))
                is WeightRecord -> o.put("type", "Weight").put("time", r.time.toString()).put("value", r.weight.inKilograms)
                is BodyFatRecord -> o.put("type", "BodyFat").put("time", r.time.toString()).put("value", r.percentage.value)
                is LeanBodyMassRecord -> o.put("type", "LeanBodyMass").put("time", r.time.toString()).put("value", r.mass.inKilograms)
                is BloodPressureRecord -> o.put("type", "BloodPressure").put("time", r.time.toString())
                    .put("systolic", r.systolic.inMillimetersOfMercury).put("diastolic", r.diastolic.inMillimetersOfMercury)
                is BloodGlucoseRecord -> o.put("type", "BloodGlucose").put("time", r.time.toString()).put("value", r.level.inMilligramsPerDeciliter)
                is OxygenSaturationRecord -> o.put("type", "OxygenSaturation").put("time", r.time.toString()).put("value", r.percentage.value)
                is HeartRateVariabilityRmssdRecord -> o.put("type", "HeartRateVariabilityRmssd").put("time", r.time.toString())
                    .put("value", r.heartRateVariabilityMillis)
                is RespiratoryRateRecord -> o.put("type", "RespiratoryRate").put("time", r.time.toString()).put("value", r.rate)
                is Vo2MaxRecord -> o.put("type", "Vo2Max").put("time", r.time.toString()).put("value", r.vo2MillilitersPerMinuteKilogram)
                is BodyTemperatureRecord -> o.put("type", "BodyTemperature").put("time", r.time.toString()).put("value", r.temperature.inCelsius)
                is ExerciseSessionRecord -> o.put("type", "Exercise")
                    .put("start", r.startTime.toString()).put("end", r.endTime.toString())
                    .put("exerciseType", r.exerciseType).put("title", r.title ?: "")
                else -> continue
            }
            array.put(o)
        }
        return JSONObject()
            .put("exportedAt", Instant.now().toString())
            .put("from", from.toString())
            .put("zone", zone.id)
            .put("records", array)
    }
}
