package de.arianw.kladde.sync

import android.app.Activity
import android.content.Context
import android.content.Intent
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.PermissionController
import androidx.health.connect.client.permission.HealthPermission
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.workDataOf
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.concurrent.TimeUnit

/**
 * What the Qt app calls through JNI (static methods, strings in and out). Everything else
 * in this module is internal to the sync.
 */
object KladdeSync {

    private const val PERIODIC = "kladde-sync"
    private const val ONCE = "kladde-sync-now"
    private const val PREFS = "kladde-sync"
    private const val KEY_INTERVAL = "intervalMinutes"
    private const val KEY_BACKFILL = "backfillDays"
    private const val DEFAULT_INTERVAL = 60L // LF-13
    private const val MIN_INTERVAL = 15L     // WorkManager's floor
    private const val DEFAULT_BACKFILL = 30L
    const val PERMISSION_REQUEST = 4711

    val READ_PERMISSIONS: Set<String> =
        HealthConnectSource.TYPES.map { HealthPermission.getReadPermission(it) }.toSet() +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_HISTORY +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_IN_BACKGROUND

    @JvmStatic
    fun available(context: Context): Boolean =
        HealthConnectClient.getSdkStatus(context) == HealthConnectClient.SDK_AVAILABLE

    @JvmStatic
    fun configure(context: Context, server: String, token: String) {
        val credentials = Credentials(context)
        credentials.server = server.trim().trimEnd('/')
        if (token.isNotEmpty()) {
            credentials.token = token.trim()
        }
    }

    @JvmStatic
    fun server(context: Context): String = Credentials(context).server ?: ""

    @JvmStatic
    fun hasToken(context: Context): Boolean = Credentials(context).complete()

    /** Opens Health Connect's permission screen; the result comes to the activity. */
    @JvmStatic
    fun requestPermissions(activity: Activity) {
        val intent: Intent = PermissionController.createRequestPermissionResultContract()
            .createIntent(activity, READ_PERMISSIONS)
        activity.startActivityForResult(intent, PERMISSION_REQUEST)
    }

    /** JSON: {"granted": [...], "missing": [...]}; blocks briefly, call off the UI thread. */
    @JvmStatic
    fun permissionsJson(context: Context): String {
        if (!available(context)) {
            return JSONObject().put("available", false).toString()
        }
        val granted = runBlocking {
            HealthConnectClient.getOrCreate(context).permissionController.getGrantedPermissions()
        }
        return JSONObject()
            .put("available", true)
            .put("granted", JSONArray(granted.intersect(READ_PERMISSIONS).sorted()))
            .put("missing", JSONArray((READ_PERMISSIONS - granted).sorted()))
            .toString()
    }

    /** Starts or updates the periodic background sync (LF-13). */
    @JvmStatic
    fun schedule(context: Context, intervalMinutes: Long) {
        val minutes = intervalMinutes.coerceAtLeast(MIN_INTERVAL)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putLong(KEY_INTERVAL, minutes).apply()
        val request = PeriodicWorkRequestBuilder<SyncWorker>(minutes, TimeUnit.MINUTES)
            .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
            .setInputData(workDataOf(SyncWorker.KEY_TRIGGER to Trigger.BACKGROUND.wire))
            .build()
        WorkManager.getInstance(context).enqueueUniquePeriodicWork(PERIODIC, ExistingPeriodicWorkPolicy.UPDATE, request)
    }

    @JvmStatic
    fun intervalMinutes(context: Context): Long =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getLong(KEY_INTERVAL, DEFAULT_INTERVAL)

    @JvmStatic
    fun syncNow(context: Context) {
        val request = OneTimeWorkRequestBuilder<SyncWorker>()
            .setInputData(workDataOf(SyncWorker.KEY_TRIGGER to Trigger.MANUAL.wire))
            .build()
        WorkManager.getInstance(context).enqueueUniqueWork(ONCE, ExistingWorkPolicy.KEEP, request)
    }

    /** How far the first sync reaches back (LF-11). */
    fun backfillDays(context: Context): Long =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getLong(KEY_BACKFILL, DEFAULT_BACKFILL)

    @JvmStatic
    fun setBackfillDays(context: Context, days: Long) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putLong(KEY_BACKFILL, days.coerceIn(1, 3650)).apply()
    }

    /** The status page's data (LF-24) as JSON. */
    @JvmStatic
    fun statusJson(context: Context): String = SqliteQueueStore(context).use { store ->
        val status = store.status()
        val seen = store.lastSeen()
        val silent = SyncRunner.silentKinds(seen)
        JSONObject()
            .put("lastRun", status.lastRun?.toString() ?: JSONObject.NULL)
            .put("lastSuccess", status.lastSuccess?.toString() ?: JSONObject.NULL)
            .put("lastError", status.lastError ?: JSONObject.NULL)
            .put("uploaded", status.uploaded)
            .put("pending", store.pendingCount())
            .put("kinds", JSONArray(Kind.entries.map { kind ->
                JSONObject()
                    .put("kind", kind.name)
                    .put("lastSeen", seen[kind]?.toString() ?: JSONObject.NULL)
                    .put("silent", kind in silent)
            }))
            .put("now", Instant.now().toString())
            .toString()
    }

    /** Forget the cursor: the next sync reads the backfill window again (the server dedups). */
    @JvmStatic
    fun resync(context: Context) {
        SqliteQueueStore(context).use { it.resetCursor() }
        syncNow(context)
    }
}
