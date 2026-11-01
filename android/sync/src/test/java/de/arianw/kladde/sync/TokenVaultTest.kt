package de.arianw.kladde.sync

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Duration
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class TokenVaultTest {

    private val now = Instant.parse("2026-11-01T12:00:00Z")

    private class MemoryPairs(var pair: TokenPair? = null) : TokenVault.Store {
        override fun load() = pair
        override fun save(pair: TokenPair) { this.pair = pair }
        override fun clear() { pair = null }
    }

    /** HealthLog's /api/auth/refresh: r1 → r2 → r3 …, a spent token is refused. */
    private class Server : TokenVault.Refresher {
        val calls = AtomicInteger()
        var live = "r1"
        var status = 200

        override fun refresh(refreshToken: String): Response {
            val n = calls.incrementAndGet()
            if (status != 200) {
                return Response(status, "")
            }
            if (refreshToken != live) {
                return Response(401, """{"meta":{"errorCode":"auth.refresh.already_used"}}""")
            }
            live = "r${n + 1}"
            return Response(200, """{"data":{"token":"a${n + 1}","tokenExpiresAt":"2026-11-02T12:00:00.000Z","refreshToken":"$live"}}""")
        }
    }

    private fun pair(access: String, refresh: String, expires: Instant = now.plus(Duration.ofHours(12))) =
        TokenPair(access, expires, refresh)

    @Test
    fun aRunOfAnEarlierSessionGetsNoTokenOfTheNext() {
        // A sync still running while the user signs out and in with another account must not
        // upload the earlier account's queue with the new session (review of #6).
        val server = Server()
        val vault = TokenVault(MemoryPairs(), server)
        vault.begin("r1")
        val run = vault.session()

        vault.forget()
        vault.begin("rB")
        val answer = vault.current(stale = null, now = now, session = run)
        assertTrue(answer is TokenVault.Answer.Refused)
        assertEquals(0, server.calls.get())
    }

    @Test
    fun aLateHandBackOfAnOlderTokenKeepsTheLiveOne() {
        // The app's renewal brings r2; before the app hands it back, the job renews to r3.
        // r2 is spent: kept, it would come back to the server and revoke every token.
        val server = Server()
        val store = MemoryPairs()
        val vault = TokenVault(store, server)
        vault.begin("r1")
        val app = vault.current(stale = null, now = now) as TokenVault.Answer.Ok
        vault.current(stale = app.pair.access, now = now)
        val run = vault.session()

        vault.begin(app.pair.refresh)
        assertEquals("r3", store.pair?.refresh)
        assertEquals(run, vault.session())
    }

    @Test
    fun theAppsRenewalKeepsARunsSession() {
        val server = Server()
        val vault = TokenVault(MemoryPairs(), server)
        vault.begin("r1")
        val run = vault.session()

        val app = vault.current(stale = null, now = now) as TokenVault.Answer.Ok
        vault.begin(app.pair.refresh)
        assertTrue(vault.current(stale = null, now = now, session = run) is TokenVault.Answer.Ok)
    }

    @Test
    fun aValidKeptTokenIsHandedOutWithoutTheNetwork() {
        val server = Server()
        val vault = TokenVault(MemoryPairs(pair("a1", "r1")), server)
        val answer = vault.current(stale = null, now = now)
        assertEquals("a1", (answer as TokenVault.Answer.Ok).pair.access)
        assertEquals(0, server.calls.get())
    }

    @Test
    fun aStaleOrExpiringTokenIsRenewedAndTheRotatedPairKept() {
        val server = Server()
        val store = MemoryPairs(pair("a1", "r1"))
        val vault = TokenVault(store, server)

        // The caller's a1 was refused: renew, keep a2/r2.
        val answer = vault.current(stale = "a1", now = now) as TokenVault.Answer.Ok
        assertEquals("a2", answer.pair.access)
        assertEquals("r2", store.pair?.refresh)

        // Close to expiry: renew too.
        val soon = now.plus(Duration.ofHours(23).plusMinutes(59).plusSeconds(30))
        assertEquals("a3", (vault.current(stale = null, now = soon) as TokenVault.Answer.Ok).pair.access)
        assertEquals(2, server.calls.get())
    }

    @Test
    fun afterSignInOnlyTheRefreshTokenIsKnown() {
        val server = Server()
        val vault = TokenVault(MemoryPairs(pair("", "r1", Instant.EPOCH)), server)
        assertEquals("a2", (vault.current(stale = null, now = now) as TokenVault.Answer.Ok).pair.access)
    }

    @Test
    fun theAppAndTheJobNeverSpendTheSameRefreshToken() {
        // Both find a1 refused at once. HealthLog revokes every token of the user when a spent
        // refresh token comes back: exactly one refresh, the other gets the new pair.
        val server = Server()
        val vault = TokenVault(MemoryPairs(pair("a1", "r1")), server)
        val pool = Executors.newFixedThreadPool(2)
        val start = CountDownLatch(1)
        val answers = (1..2).map {
            pool.submit<TokenVault.Answer> {
                start.await()
                vault.current(stale = "a1", now = now)
            }
        }
        start.countDown()
        val got = answers.map { it.get(5, TimeUnit.SECONDS) }
        pool.shutdown()

        assertEquals(1, server.calls.get())
        assertTrue(got.all { it is TokenVault.Answer.Ok && it.pair.access == "a2" })
    }

    @Test
    fun aRefusalSignsOutAndForgetsThePair() {
        val server = Server().apply { status = 401 }
        val store = MemoryPairs(pair("a1", "r1"))
        val vault = TokenVault(store, server)
        val answer = vault.current(stale = "a1", now = now)
        assertTrue(answer is TokenVault.Answer.Refused)
        assertNull(store.pair)
    }

    @Test
    fun noAnswerKeepsThePairForLater() {
        for (status in listOf(0, 404, 502)) {
            val server = Server().apply { this.status = status }
            val store = MemoryPairs(pair("a1", "r1"))
            val answer = TokenVault(store, server).current(stale = "a1", now = now)
            assertTrue(answer is TokenVault.Answer.Unavailable)
            assertEquals("r1", store.pair?.refresh)
        }
    }

    @Test
    fun theAppKeepingTheRenewedTokenChangesNothing() {
        val store = MemoryPairs(pair("a2", "r2"))
        val vault = TokenVault(store, Server())
        vault.begin("r2")
        assertEquals("a2", store.pair?.access)
        vault.begin("r9") // a new sign-in
        assertEquals("", store.pair?.access)
        assertEquals("r9", store.pair?.refresh)
    }

    @Test
    fun withoutAPairTheAnswerIsRefused() {
        val vault = TokenVault(MemoryPairs(), Server())
        assertTrue(vault.current(stale = null, now = now) is TokenVault.Answer.Refused)
    }
}
