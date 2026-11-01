package de.arianw.kladde.sync

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import org.json.JSONObject
import java.time.Instant

/**
 * Server address, the app's device id and the session's token pair (LF-01, LF-02). The pair
 * is encrypted with an AES key that lives in the Android Keystore and never leaves it; the
 * preferences hold only ciphertext. TokenVault is the only one that reads and writes it.
 */
class Credentials(context: Context) : TokenVault.Store {

    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    var server: String?
        get() = prefs.getString(KEY_SERVER, null)
        set(value) {
            prefs.edit().putString(KEY_SERVER, value).apply()
        }

    /** X-Device-Id of the app's sign-in: HealthLog refuses a renewal from another device. */
    var deviceId: String?
        get() = prefs.getString(KEY_DEVICE, null)
        set(value) {
            prefs.edit().putString(KEY_DEVICE, value).apply()
        }

    override fun load(): TokenPair? {
        val json = JSONObject(open(prefs.getString(KEY_PAIR, null) ?: return null))
        return TokenPair(json.getString("access"), Instant.parse(json.getString("expires")), json.getString("refresh"))
    }

    override fun save(pair: TokenPair) {
        val json = JSONObject().put("access", pair.access).put("expires", pair.expires.toString()).put("refresh", pair.refresh)
        prefs.edit().putString(KEY_PAIR, seal(json.toString())).commit()
    }

    override fun clear() {
        prefs.edit().remove(KEY_PAIR).commit()
    }

    fun complete(): Boolean = !server.isNullOrEmpty() && !deviceId.isNullOrEmpty() && prefs.contains(KEY_PAIR)

    private fun seal(text: String): String {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, key())
        return Base64.encodeToString(cipher.iv + cipher.doFinal(text.toByteArray()), Base64.NO_WRAP)
    }

    private fun open(sealed: String): String {
        val bytes = Base64.decode(sealed, Base64.NO_WRAP)
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(TAG_BITS, bytes, 0, IV_BYTES))
        return String(cipher.doFinal(bytes, IV_BYTES, bytes.size - IV_BYTES))
    }

    private fun key(): SecretKey {
        val store = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (store.getEntry(ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }

        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build()
        )
        return generator.generateKey()
    }

    private companion object {
        const val PREFS = "kladde-sync"
        const val KEY_SERVER = "server"
        const val KEY_DEVICE = "deviceId"
        const val KEY_PAIR = "session.sealed"
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "kladde-token"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_BYTES = 12
        const val TAG_BITS = 128
    }
}
