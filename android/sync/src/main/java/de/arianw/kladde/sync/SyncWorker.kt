package de.arianw.kladde.sync

import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters

/** One background or manual sync (LF-13). Retries come from the queue's own backoff. */
class SyncWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {

    override suspend fun doWork(): Result {
        val credentials = Credentials(applicationContext)
        val server = credentials.server
        val token = credentials.token
        if (server.isNullOrEmpty() || token.isNullOrEmpty()) {
            return Result.success()
        }
        if (HealthConnectClient.getSdkStatus(applicationContext) != HealthConnectClient.SDK_AVAILABLE) {
            return Result.success()
        }

        val trigger = Trigger.entries.firstOrNull { it.wire == inputData.getString(KEY_TRIGGER) } ?: Trigger.BACKGROUND
        val store = SqliteQueueStore(applicationContext)
        val source = HealthConnectSource(HealthConnectClient.getOrCreate(applicationContext))
        val backfill = KladdeSync.backfillDays(applicationContext)

        store.use {
            SyncRunner(it, HttpTransport(server, token)).run({ cursor, zone, now ->
                source.read(cursor, zone, backfill, it::dayOf, now)
            }, trigger)
        }
        return Result.success()
    }

    companion object {
        const val KEY_TRIGGER = "trigger"
    }
}
