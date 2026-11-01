package de.arianw.kladde.sync

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * HealthLog over HTTPS as the app talks to it: a native client (X-Client-Type, not a
 * browser's user agent) with the app's device id, so the session's tokens work here too.
 */
class HealthLogHttp(server: String, private val deviceId: String) : TokenVault.Refresher {

    private val base = server.trimEnd('/')

    /** POST /api/measurements/batch with the session's access token. */
    fun postBatch(access: String, body: String, key: String): Response =
        post(BATCH_PATH, body, mapOf("Authorization" to "Bearer $access", "Idempotency-Key" to key))

    override fun refresh(refreshToken: String): Response =
        try {
            post(REFRESH_PATH, org.json.JSONObject().put("refreshToken", refreshToken).toString(), emptyMap())
        } catch (e: IOException) {
            Response(0, e.message ?: "")
        }

    private fun post(path: String, body: String, headers: Map<String, String>): Response {
        val url = URL(base + path)
        if (url.protocol != "https") {
            throw IOException("not https: $base")
        }
        val c = url.openConnection() as HttpURLConnection
        try {
            c.requestMethod = "POST"
            c.connectTimeout = TIMEOUT_MS
            c.readTimeout = TIMEOUT_MS
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.setRequestProperty("Accept", "application/json")
            c.setRequestProperty("X-Client-Type", "native")
            c.setRequestProperty("User-Agent", USER_AGENT)
            c.setRequestProperty("X-Device-Id", deviceId)
            headers.forEach { (k, v) -> c.setRequestProperty(k, v) }
            c.outputStream.use { it.write(body.toByteArray()) }

            val status = c.responseCode
            val stream = if (status >= 400) c.errorStream else c.inputStream
            val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
            return Response(status, text)
        } finally {
            c.disconnect()
        }
    }

    private companion object {
        const val BATCH_PATH = "/api/measurements/batch"
        const val REFRESH_PATH = "/api/auth/refresh"
        const val TIMEOUT_MS = 30_000
        // Never "Mozilla/…": HealthLog takes that for a browser and refuses its refresh.
        const val USER_AGENT = "Kladde"
    }
}

/**
 * Uploads with the session's access token from the vault. A 401 means the token was
 * renewed elsewhere or ran out: ask the vault once more (never the same token) and repeat.
 */
class SessionTransport(
    private val access: (stale: String?) -> TokenVault.Answer,
    private val send: (access: String, body: String, key: String) -> Response,
) : Transport {

    override fun postBatch(body: String, key: String): Response {
        var stale: String? = null
        repeat(2) {
            val token = when (val answer = access(stale)) {
                is TokenVault.Answer.Ok -> answer.pair.access
                is TokenVault.Answer.Refused -> return Response(HTTP_UNAUTHORIZED, answer.response.body)
                is TokenVault.Answer.Unavailable -> return Response(0, answer.response.body)
            }
            val response = send(token, body, key)
            if (response.status != HTTP_UNAUTHORIZED) {
                return response
            }
            stale = token
        }
        return Response(HTTP_UNAUTHORIZED, "")
    }

    private companion object {
        const val HTTP_UNAUTHORIZED = 401
    }
}
