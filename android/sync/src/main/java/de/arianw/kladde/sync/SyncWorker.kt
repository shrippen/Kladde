package de.arianw.kladde.sync

import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters

/** One background or manual sync (LF-13). Retries come from the queue's own backoff. */
class SyncWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {

    override suspend fun doWork(): Result {
        val credentials = Credentials(applicationContext)
        if (!credentials.complete()) {
            return Result.success()
        }
        // The app's session, renewed in the vault when it runs out (owner: no token of its own).
        val http = HealthLogHttp(credentials.server!!, credentials.deviceId!!)
        val vault = KladdeSync.vault(applicationContext)
        // Only this session's token: after a sign-out and in, the rest of this run is refused.
        val session = vault.session()
        val transport = SessionTransport({ stale -> vault.current(stale, session = session) }, http::postBatch)
        if (HealthConnectClient.getSdkStatus(applicationContext) != HealthConnectClient.SDK_AVAILABLE) {
            return Result.success()
        }

        val trigger = Trigger.entries.firstOrNull { it.wire == inputData.getString(KEY_TRIGGER) } ?: Trigger.BACKGROUND
        val store = SqliteQueueStore(applicationContext)
        val source = HealthConnectSource(HealthConnectClient.getOrCreate(applicationContext))
        val backfill = KladdeSync.backfillDays(applicationContext)

        store.use {
            SyncRunner(it, transport).run({ cursor, zone, now ->
                source.read(cursor, zone, backfill, it::daysOf, now)
            }, trigger)
        }
        return Result.success()
    }

    companion object {
        const val KEY_TRIGGER = "trigger"
    }
}
