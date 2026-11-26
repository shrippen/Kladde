package de.arianw.kladde.sync

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** The same behaviour against the real SQLite queue (Robolectric's SQLite on the JVM). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class UploaderSqliteTest : UploaderTest() {
    // Empty each time: a test may ask for several stores, MemoryStore starts empty too.
    override fun newStore(): QueueStore {
        val context = ApplicationProvider.getApplicationContext<Context>()
        context.deleteDatabase(SqliteQueueStore.NAME)
        return SqliteQueueStore(context)
    }
}
