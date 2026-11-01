package de.arianw.kladde.sync

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant

class SessionTransportTest {

    private fun ok(access: String): TokenVault.Answer =
        TokenVault.Answer.Ok(TokenPair(access, Instant.parse("2026-11-02T12:00:00Z"), "r"))

    @Test
    fun aRefusedAccessTokenIsRenewedOnceAndTheBatchRepeated() {
        // The app renewed the session meanwhile: the job's a1 is gone, the vault has a2.
        val asked = mutableListOf<String?>()
        val sent = mutableListOf<String>()
        val transport = SessionTransport(
            { stale -> asked += stale; ok(if (stale == null) "a1" else "a2") },
            { access, _, _ -> sent += access; Response(if (access == "a1") 401 else 200, "") },
        )
        assertEquals(200, transport.postBatch("{}", "k").status)
        assertEquals(listOf(null, "a1"), asked)
        assertEquals(listOf("a1", "a2"), sent)
    }

    @Test
    fun aSecond401StopsTheUpload() {
        val transport = SessionTransport({ stale -> ok(if (stale == null) "a1" else "a2") }, { _, _, _ -> Response(401, "") })
        assertEquals(401, transport.postBatch("{}", "k").status)
    }

    @Test
    fun anEndedSessionIsAnAuthFailureAndNoAnswerIsRetried() {
        val refused = SessionTransport({ TokenVault.Answer.Refused(Response(401, "")) }, { _, _, _ -> error("not sent") })
        assertEquals(401, refused.postBatch("{}", "k").status)
        val offline = SessionTransport({ TokenVault.Answer.Unavailable(Response(503, "")) }, { _, _, _ -> error("not sent") })
        assertEquals(0, offline.postBatch("{}", "k").status)
    }
}
