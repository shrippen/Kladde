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

/**
 * Server address and token (LF-01, LF-02). The token is encrypted with an AES key that
 * lives in the Android Keystore and never leaves it; the preferences hold only ciphertext.
 */
class Credentials(context: Context) {

    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    var server: String?
        get() = prefs.getString(KEY_SERVER, null)
        set(value) {
            prefs.edit().putString(KEY_SERVER, value).apply()
        }

    var token: String?
        get() {
            val stored = prefs.getString(KEY_TOKEN, null) ?: return null
            val bytes = Base64.decode(stored, Base64.NO_WRAP)
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(TAG_BITS, bytes, 0, IV_BYTES))
            return String(cipher.doFinal(bytes, IV_BYTES, bytes.size - IV_BYTES))
        }
        set(value) {
            if (value == null) {
                prefs.edit().remove(KEY_TOKEN).apply()
                return
            }
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, key())
            val sealed = cipher.iv + cipher.doFinal(value.toByteArray())
            prefs.edit().putString(KEY_TOKEN, Base64.encodeToString(sealed, Base64.NO_WRAP)).apply()
        }

    fun complete(): Boolean = !server.isNullOrEmpty() && prefs.contains(KEY_TOKEN)

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
        const val KEY_TOKEN = "token.sealed"
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "kladde-token"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_BYTES = 12
        const val TAG_BITS = 128
    }
}
