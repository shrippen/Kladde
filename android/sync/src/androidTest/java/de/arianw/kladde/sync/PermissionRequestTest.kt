package de.arianw.kladde.sync

import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Test
import org.junit.runner.RunWith

/**
 * On the phone (./gradlew :sync:connectedDebugAndroidTest): the permission request from a
 * plain Activity. Health Connect's result contract builds an intent only a ComponentActivity
 * understands; from Qt's activity it threw ActivityNotFoundException and nothing opened.
 */
@RunWith(AndroidJUnit4::class)
class PermissionRequestTest {

    @Test
    fun asksFromAPlainActivity() {
        ActivityScenario.launch(PlainActivity::class.java).use { scenario ->
            scenario.onActivity { KladdeSync.requestPermissions(it) }
        }
    }
}
