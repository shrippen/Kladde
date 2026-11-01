package de.arianw.kladde.sync

import org.json.JSONObject
import java.time.Duration
import java.time.Instant

/** The session's access token, when it runs out, and the refresh token that renews it. */
class TokenPair(val access: String, val expires: Instant, val refresh: String)

/**
 * The one keeper of the app's HealthLog session on Android: the app (through JNI) and the
 * background job both get their access token here (LF-02, owner 1.11.2026: no token of its
 * own for the sync).
 *
 * HealthLog's refresh tokens are single-use, and a spent one coming back revokes every token
 * of the user. So only one renewal runs at a time; whoever waited gets the pair it brought.
 *
 *   current(stale) ── kept access valid and not `stale` ──▶ Ok (no network)
 *                  └─ else POST /api/auth/refresh ── 200 ──▶ keep the new pair, Ok
 *                                                  ├─ 401/403 ─▶ forget, Refused (sign in)
 *                                                  └─ else ────▶ Unavailable (keep, later)
 */
class TokenVault(private val store: Store, private val refresher: Refresher) {

    /** Counts sign-ins and sign-outs: a run started under an earlier one gets no token. */
    private var session = 0L

    /** Refresh tokens renewals here gave out: one handed back late is no new sign-in. */
    private val issued = mutableSetOf<String>()

    interface Store {
        fun load(): TokenPair?
        fun save(pair: TokenPair)
        fun clear()
    }

    /** POST /api/auth/refresh with the given refresh token. */
    fun interface Refresher {
        fun refresh(refreshToken: String): Response
    }

    sealed class Answer {
        class Ok(val pair: TokenPair) : Answer()
        /** The server's no: the session is over. `response` is its answer, for the app. */
        class Refused(val response: Response) : Answer()
        /** No judgement on the token (no network, 5xx, a proxy's 404): try again later. */
        class Unavailable(val response: Response) : Answer()
    }

    /** Which session the vault holds; a run notes it at its start (see current). */
    @Synchronized
    fun session(): Long = session

    /**
     * A usable access token. `stale`: one the caller saw refused, never handed out again.
     * `session`: the one a run started under; after a sign-out or another sign-in it gets none.
     */
    @Synchronized
    fun current(stale: String?, now: Instant = Instant.now(), session: Long? = null): Answer {
        if (session != null && session != this.session) {
            return Answer.Refused(Response(HTTP_UNAUTHORIZED, ""))
        }
        val kept = store.load() ?: return Answer.Refused(Response(HTTP_UNAUTHORIZED, ""))
        if (kept.access.isNotEmpty() && kept.access != stale && kept.expires.isAfter(now.plus(MARGIN))) {
            return Answer.Ok(kept)
        }

        val response = refresher.refresh(kept.refresh)
        if (response.status == HTTP_OK) {
            val pair = parse(response.body) ?: return Answer.Unavailable(response)
            issued += pair.refresh
            store.save(pair)
            return Answer.Ok(pair)
        }
        if (response.status == HTTP_UNAUTHORIZED || response.status == HTTP_FORBIDDEN) {
            store.clear()
            return Answer.Refused(response)
        }
        return Answer.Unavailable(response)
    }

    /**
     * After a sign-in: the refresh token the app got (its access token stays with the app).
     * The app also hands back the token a renewal here gave it, maybe after the job renewed
     * again: then the kept pair stays (the late one is spent).
     */
    @Synchronized
    fun begin(refreshToken: String) {
        if (store.load()?.refresh == refreshToken || refreshToken in issued) {
            return
        }
        session++
        issued.clear() // the earlier session's tokens: none of them comes back now
        store.save(TokenPair("", Instant.EPOCH, refreshToken))
    }

    @Synchronized
    fun refreshToken(): String? = store.load()?.refresh

    /** Signed out: waits for a renewal under way, so nothing is kept after this. */
    @Synchronized
    fun forget() {
        session++
        issued.clear()
        store.clear()
    }

    private fun parse(body: String): TokenPair? {
        val data = runCatching { JSONObject(body).getJSONObject("data") }.getOrNull() ?: return null
        val access = data.optString("token")
        val refresh = data.optString("refreshToken")
        val expires = runCatching { Instant.parse(data.optString("tokenExpiresAt")) }.getOrNull()
        if (access.isEmpty() || refresh.isEmpty() || expires == null) {
            return null
        }
        return TokenPair(access, expires, refresh)
    }

    private companion object {
        /** Renew this long before the access token runs out (as core/session.cpp). */
        val MARGIN: Duration = Duration.ofSeconds(60)
        const val HTTP_OK = 200
        const val HTTP_UNAUTHORIZED = 401
        const val HTTP_FORBIDDEN = 403
    }
}
