package de.arianw.kladde.sync

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant

class AppOrderTest {

    private val order = WorkoutMapper.DEFAULT_ORDER // Freeletics, Pebble, Fit
    private val other = "org.example.run"

    @Test
    fun onlyTheAppsThatWroteAreShownNewOnesLast() {
        val seen = setOf(WorkoutMapper.FIT_APP, other, HealthConnectSource.PEBBLE_APP)
        assertEquals(listOf(HealthConnectSource.PEBBLE_APP, WorkoutMapper.FIT_APP, other), AppOrder.shown(order, seen))
    }

    @Test
    fun aMoveKeepsTheHiddenAppsAfterTheShown() {
        val shown = listOf(WorkoutMapper.FIT_APP, HealthConnectSource.PEBBLE_APP)
        assertEquals(listOf(WorkoutMapper.FIT_APP, HealthConnectSource.PEBBLE_APP, WorkoutMapper.FREELETICS_APP), AppOrder.stored(order, shown))
    }

    @Test
    fun eachSessionTakesTheOrderInForceWhenItBegan() {
        val a = listOf("a")
        val b = listOf("b")
        val c = listOf("c")
        val first = Instant.parse("2026-11-01T10:00:00Z")
        val second = Instant.parse("2026-11-03T10:00:00Z")
        val ranking = Ranking(a).changed(b, first).changed(c, second).changed(c, second.plusSeconds(60))

        assertEquals(a, ranking.at(first.minusSeconds(1)))
        assertEquals(b, ranking.at(first))
        assertEquals(c, ranking.at(second))
        assertEquals(2, ranking.past.size) // the same order again is no change
    }
}
