package de.arianw.kladde.sync

import android.content.Context
import androidx.health.connect.client.records.Record
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import kotlin.reflect.KClass

/** The kinds whose sessions overlap between apps: there the app first in the order stays. */
enum class Ranked(val key: String, val defaults: List<String>, val types: Set<KClass<out Record>>) {
    SLEEP("sleep", SleepSource.DEFAULT_ORDER, SleepSource.TYPES),
    WORKOUTS("workouts", WorkoutMapper.DEFAULT_ORDER, WorkoutSource.TYPES),
}

/**
 * The app order per [Ranked] kind, as the user set it in the app (default: [Ranked.defaults]),
 * with the orders before it for what began until each change (Ranking).
 *
 * The app shows only the apps that wrote that kind, so a move keeps the others where they
 * were relative to each other, after the shown ones:
 *
 *   stored  [Freeletics, Pebble, Fit]   seen {Fit, Pebble}
 *   shown   [Pebble, Fit]               Fit moved up ─▶ stored [Fit, Pebble, Freeletics]
 */
class AppOrder(context: Context) {

    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun of(kind: Ranked): Ranking {
        val current = prefs.getString(keyOf(kind), null)?.split(SEPARATOR)?.filter { it.isNotEmpty() } ?: kind.defaults
        val past = JSONArray(prefs.getString(pastKeyOf(kind), null) ?: "[]")
        return Ranking(current, (0 until past.length()).map { i ->
            val era = past.getJSONObject(i)
            val order = era.getJSONArray(ORDER)
            Ranking.Era(Instant.parse(era.getString(UNTIL)), (0 until order.length()).map { order.getString(it) })
        })
    }

    /** The user set [apps] at [now]. */
    fun set(kind: Ranked, apps: List<String>, now: Instant) {
        val ranking = of(kind).changed(apps, now)
        val past = JSONArray(ranking.past.map { JSONObject().put(UNTIL, it.until.toString()).put(ORDER, JSONArray(it.order)) })
        prefs.edit()
            .putString(keyOf(kind), ranking.current.joinToString(SEPARATOR))
            .putString(pastKeyOf(kind), past.toString())
            .apply()
    }

    private fun keyOf(kind: Ranked) = "order.${kind.key}"
    private fun pastKeyOf(kind: Ranked) = "order.${kind.key}.past"

    companion object {
        private const val PREFS = "kladde-order"
        private const val SEPARATOR = ","
        private const val UNTIL = "until"
        private const val ORDER = "order"

        /** The apps to show: the [seen] ones in the order, new ones last. */
        fun shown(order: List<String>, seen: Set<String>): List<String> =
            order.filter { it in seen } + (seen - order.toSet()).sorted()

        /** The order with the [shown] apps as the user put them; the hidden ones follow. */
        fun stored(order: List<String>, shown: List<String>): List<String> =
            shown + order.filter { it !in shown }
    }
}
