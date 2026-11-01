package de.arianw.kladde.sync

import androidx.health.connect.client.permission.HealthPermission
import org.junit.Assert.assertEquals
import org.junit.Test

class PermissionsTest {

    private fun names(array: org.json.JSONArray) = (0 until array.length()).map { array.getString(it) }.toSet()

    @Test
    fun optionalKindsLeftOutStillMeanAccess() {
        // Granted before the update: the watch's kinds, history, background. Weight and the like not.
        val before = ((HealthConnectSource.TYPES + SleepSource.TYPES).map { HealthPermission.getReadPermission(it) } +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_HISTORY +
            HealthPermission.PERMISSION_READ_HEALTH_DATA_IN_BACKGROUND).toSet()
        val state = KladdeSync.permissionState(before)

        assertEquals(emptySet<String>(), names(state.getJSONArray("missing")))
        assertEquals(KladdeSync.READ_PERMISSIONS - before, names(state.getJSONArray("optional")))
    }
}
