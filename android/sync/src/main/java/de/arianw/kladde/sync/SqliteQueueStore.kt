package de.arianw.kladde.sync

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.time.Instant
import java.time.LocalDate

/** QueueStore in the app's private SQLite database (kladde-sync.db). */
class SqliteQueueStore(context: Context) :
    SQLiteOpenHelper(context, "kladde-sync.db", null, VERSION), QueueStore {

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE pending (key TEXT PRIMARY KEY, body TEXT NOT NULL, size INTEGER NOT NULL, created INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, due INTEGER NOT NULL)")
        db.execSQL("CREATE TABLE kv (name TEXT PRIMARY KEY, value TEXT)")
        db.execSQL("CREATE TABLE record_day (id TEXT NOT NULL, day TEXT NOT NULL, PRIMARY KEY (id, day))")
        db.execSQL("CREATE TABLE seen (kind TEXT PRIMARY KEY, at INTEGER NOT NULL)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        // Before the first release: no migrations, start over (agent.md, breaking changes).
        db.execSQL("DROP TABLE IF EXISTS pending")
        db.execSQL("DROP TABLE IF EXISTS kv")
        db.execSQL("DROP TABLE IF EXISTS record_day")
        db.execSQL("DROP TABLE IF EXISTS seen")
        onCreate(db)
    }

    override fun enqueue(batches: List<Batch>, cursor: String?, recordDays: Map<String, Set<LocalDate>>, seen: Map<Kind, Instant>) {
        val db = writableDatabase
        val now = System.currentTimeMillis()
        db.beginTransaction()
        try {
            for (b in batches) {
                db.insertWithOnConflict("pending", null, ContentValues().apply {
                    put("key", b.key)
                    put("body", b.body)
                    put("size", b.size)
                    put("created", now)
                    put("due", 0L)
                }, SQLiteDatabase.CONFLICT_IGNORE)
            }
            for ((id, days) in recordDays) {
                db.delete("record_day", "id = ?", arrayOf(id))
                for (day in days) {
                    db.insertWithOnConflict("record_day", null, ContentValues().apply {
                        put("id", id)
                        put("day", day.toString())
                    }, SQLiteDatabase.CONFLICT_IGNORE)
                }
            }
            for ((kind, at) in seen) {
                db.execSQL("INSERT INTO seen (kind, at) VALUES (?, ?) ON CONFLICT(kind) DO UPDATE SET at = MAX(at, excluded.at)",
                    arrayOf(kind.name, at.toEpochMilli()))
            }
            if (cursor != null) {
                putValue(db, KEY_CURSOR, cursor)
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
        }
    }

    override fun next(now: Instant): Pending? =
        readableDatabase.rawQuery(
            // rowid, not created: the wall clock can go back, a new row's rowid cannot.
            "SELECT body, key, size, attempts, due FROM pending ORDER BY rowid LIMIT 1",
            null,
        ).use { c ->
            if (!c.moveToFirst() || !QueueStore.isDue(Instant.ofEpochMilli(c.getLong(4)), now)) {
                return null
            }
            Pending(Batch(c.getString(0), c.getString(1), c.getInt(2)), c.getInt(3))
        }

    override fun done(key: String) {
        writableDatabase.delete("pending", "key = ?", arrayOf(key))
    }

    override fun retryAt(key: String, at: Instant) {
        writableDatabase.execSQL("UPDATE pending SET attempts = attempts + 1, due = ? WHERE key = ?",
            arrayOf(at.toEpochMilli(), key))
    }

    override fun replace(key: String, with: Batch?, keepAttempts: Boolean) {
        if (with == null) {
            done(key)
            return
        }
        // In place: same rowid, so the batch keeps its turn in the queue.
        val attempts = if (keepAttempts) "attempts" else "0"
        writableDatabase.execSQL("UPDATE pending SET key = ?, body = ?, size = ?, attempts = $attempts, due = 0 WHERE key = ?",
            arrayOf(with.key, with.body, with.size, key))
    }

    override fun cursor(): String? = value(KEY_CURSOR)

    override fun resetCursor() {
        writableDatabase.delete("kv", "name = ?", arrayOf(KEY_CURSOR))
    }

    override fun daysOf(recordId: String): Set<LocalDate> =
        readableDatabase.rawQuery("SELECT day FROM record_day WHERE id = ?", arrayOf(recordId)).use { c ->
            val out = mutableSetOf<LocalDate>()
            while (c.moveToNext()) {
                out += LocalDate.parse(c.getString(0))
            }
            out
        }

    override fun pendingCount(): Int =
        readableDatabase.rawQuery("SELECT COALESCE(SUM(size), 0) FROM pending", null).use {
            if (it.moveToFirst()) it.getInt(0) else 0
        }

    override fun lastSeen(): Map<Kind, Instant> =
        readableDatabase.rawQuery("SELECT kind, at FROM seen", null).use { c ->
            val out = mutableMapOf<Kind, Instant>()
            while (c.moveToNext()) {
                val kind = Kind.entries.firstOrNull { it.name == c.getString(0) } ?: continue
                out[kind] = Instant.ofEpochMilli(c.getLong(1))
            }
            out
        }

    override fun setStatus(status: SyncStatus) {
        val db = writableDatabase
        putValue(db, "status.lastRun", status.lastRun?.toString())
        putValue(db, "status.lastSuccess", status.lastSuccess?.toString())
        putValue(db, "status.lastError", status.lastError)
        putValue(db, "status.uploaded", status.uploaded.toString())
    }

    override fun status(): SyncStatus = SyncStatus(
        lastRun = value("status.lastRun")?.let(Instant::parse),
        lastSuccess = value("status.lastSuccess")?.let(Instant::parse),
        lastError = value("status.lastError"),
        uploaded = value("status.uploaded")?.toIntOrNull() ?: 0,
    )

    private fun value(name: String): String? =
        readableDatabase.rawQuery("SELECT value FROM kv WHERE name = ?", arrayOf(name)).use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) else null
        }

    private fun putValue(db: SQLiteDatabase, name: String, value: String?) {
        db.insertWithOnConflict("kv", null, ContentValues().apply {
            put("name", name)
            put("value", value)
        }, SQLiteDatabase.CONFLICT_REPLACE)
    }

    private companion object {
        const val VERSION = 2
        const val KEY_CURSOR = "hc.changesToken"
    }
}
