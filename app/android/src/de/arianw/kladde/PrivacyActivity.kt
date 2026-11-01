package de.arianw.kladde

import android.app.Activity
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView

/** What Kladde does with the Health Connect data; Health Connect links here. */
class PrivacyActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (24 * resources.displayMetrics.density).toInt()
        val text = TextView(this).apply {
            setText(R.string.privacy_text)
            setPadding(pad, pad, pad, pad)
            textSize = 16f
        }
        setContentView(ScrollView(this).apply { addView(text) })
    }
}
