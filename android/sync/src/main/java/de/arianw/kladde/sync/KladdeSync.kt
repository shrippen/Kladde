package de.arianw.kladde.sync

import android.app.Activity
import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.permission.HealthPermission
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.workDataOf
import kotlinx.coroutines.flow.first
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
        (HealthConnectSource.TYPES + WorkoutSource.TYPES).map { HealthPermission.getReadPermission(it) }.toSet() +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_HISTORY +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_IN_BACKGROUND

    @JvmStatic
    fun available(context: Context): Boolean =
        HealthConnectClient.getSdkStatus(context) == HealthConnectClient.SDK_AVAILABLE

    /**
     * Where the session lives and the app's device id; the tokens come through begin().
     * `fresh`, a new sign-in: drop what an earlier session left (queue, cursor, status), it
     * may belong to another account or server. The vault already holds the new session.
     */
    @JvmStatic
    fun configure(context: Context, server: String, deviceId: String, fresh: Boolean) {
        if (fresh) {
            val work = WorkManager.getInstance(context)
            work.cancelUniqueWork(PERIODIC)
            work.cancelUniqueWork(ONCE)
            context.deleteDatabase(SqliteQueueStore.NAME)
        }
        val credentials = Credentials(context)
        credentials.server = server.trim().trimEnd('/')
        credentials.deviceId = deviceId
    }

    @Volatile
    private var vault: TokenVault? = null

    /** The one vault of the process: the app and the background job share its lock. */
    @JvmStatic
    fun vault(context: Context): TokenVault =
        vault ?: synchronized(this) {
            vault ?: Credentials(context.applicationContext).let { credentials ->
                TokenVault(credentials) { refreshToken ->
                    val server = credentials.server.orEmpty()
                    val device = credentials.deviceId.orEmpty()
                    if (server.isEmpty() || device.isEmpty()) {
                        Response(0, "")
                    } else {
                        HealthLogHttp(server, device).refresh(refreshToken)
                    }
                }
            }.also { vault = it }
        }

    // The app's keychain on Android (app/sessionvault_android.cpp): the refresh token.

    @JvmStatic
    fun refreshToken(context: Context): String = vault(context).refreshToken().orEmpty()

    /** After a sign-in, or the app keeping the token a renewal gave it (then nothing changes). */
    @JvmStatic
    fun begin(context: Context, refreshToken: String) {
        vault(context).begin(refreshToken)
    }

    @JvmStatic
    fun endSession(context: Context) {
        vault(context).forget()
    }

    /**
     * The app's renewal (blocks; call off the UI thread). Answers like POST /api/auth/refresh:
     * {"status": 200, "body": {"data": {token, tokenExpiresAt, refreshToken}}}, or the
     * server's refusal, or status 0 for no answer.
     */
    @JvmStatic
    fun renewJson(context: Context, stale: String): String {
        val answer = vault(context).current(stale.ifEmpty { null })
        val (status, body) = when (answer) {
            is TokenVault.Answer.Ok -> 200 to JSONObject().put("data", JSONObject()
                .put("token", answer.pair.access)
                .put("tokenExpiresAt", answer.pair.expires.toString())
                .put("refreshToken", answer.pair.refresh)).toString()
            is TokenVault.Answer.Refused -> answer.response.status to answer.response.body
            is TokenVault.Answer.Unavailable -> answer.response.status to answer.response.body
        }
        return JSONObject().put("status", status).put("body", body).toString()
    }

    @JvmStatic
    fun server(context: Context): String = Credentials(context).server ?: ""

    @JvmStatic
    fun configured(context: Context): Boolean = Credentials(context).complete()

    /**
     * Opens Health Connect's permission screen; the result comes to the activity. From
     * Android 14 the health permissions are runtime permissions. Not Health Connect's result
     * contract: its intent needs a ComponentActivity, and Qt's activity is a plain one.
     */
    @JvmStatic
    fun requestPermissions(activity: Activity) {
        activity.requestPermissions(READ_PERMISSIONS.toTypedArray(), PERMISSION_REQUEST)
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
    @JvmStatic
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
            .put("running", running(context))
            .put("kinds", JSONArray(Kind.entries.map { kind ->
                JSONObject()
                    .put("kind", kind.name)
                    .put("lastSeen", seen[kind]?.toString() ?: JSONObject.NULL)
                    .put("silent", kind in silent)
            }))
            .put("now", Instant.now().toString())
            .toString()
    }

    /** A run under way, or one asked for with "sync now" that has not started yet. */
    private fun running(context: Context): Boolean {
        val work = WorkManager.getInstance(context)
        val (now, periodic) = runBlocking {
            work.getWorkInfosForUniqueWorkFlow(ONCE).first() to work.getWorkInfosForUniqueWorkFlow(PERIODIC).first()
        }
        return now.any { it.state == WorkInfo.State.RUNNING || it.state == WorkInfo.State.ENQUEUED }
            || periodic.any { it.state == WorkInfo.State.RUNNING }
    }

    /**
     * Sign-out: stop the background work and drop everything queued or
     * remembered for this account. Signing in again reads the backfill window anew.
     */
    @JvmStatic
    fun forget(context: Context) {
        val work = WorkManager.getInstance(context)
        work.cancelUniqueWork(PERIODIC)
        work.cancelUniqueWork(ONCE)
        // Not the vault: the session ends it (endSession) after reading the token to revoke;
        // the app calls this on its UI thread, where the vault's lock may wait a minute.
        context.deleteDatabase(SqliteQueueStore.NAME)
    }

    /** Forget the cursor: the next sync reads the backfill window again (the server dedups). */
    @JvmStatic
    fun resync(context: Context) {
        SqliteQueueStore(context).use { it.resetCursor() }
        syncNow(context)
    }
}
