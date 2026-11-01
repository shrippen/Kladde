package de.arianw.kladde.sync

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** POST /api/measurements/batch with a Bearer token, over HTTPS only. */
class HttpTransport(private val server: String, private val token: String) : Transport {

    override fun postBatch(body: String, key: String): Response {
        val url = URL(server.trimEnd('/') + BATCH_PATH)
        if (url.protocol != "https") {
            throw IOException("not https: $server")
        }
        val c = url.openConnection() as HttpURLConnection
        try {
            c.requestMethod = "POST"
            c.connectTimeout = TIMEOUT_MS
            c.readTimeout = TIMEOUT_MS
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.setRequestProperty("Authorization", "Bearer $token")
            c.setRequestProperty("Idempotency-Key", key)
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
        const val TIMEOUT_MS = 30_000
    }
}
