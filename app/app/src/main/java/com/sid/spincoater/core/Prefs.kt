package com.sid.spincoater.core

import android.content.Context
import androidx.core.content.edit

class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("spincoater", Context.MODE_PRIVATE)

    var lastAddress: String?
        get() = sp.getString(KEY_LAST_ADDRESS, null)
        set(value) = sp.edit { putString(KEY_LAST_ADDRESS, value) }

    var autoConnect: Boolean
        get() = sp.getBoolean(KEY_AUTO_CONNECT, true)
        set(value) = sp.edit { putBoolean(KEY_AUTO_CONNECT, value) }

    var pollIntervalMs: Long
        get() = sp.getLong(KEY_POLL_MS, 750L)
        set(value) = sp.edit { putLong(KEY_POLL_MS, value.coerceIn(250L, 5000L)) }

    var confirmBeforeStart: Boolean
        get() = sp.getBoolean(KEY_CONFIRM_START, true)
        set(value) = sp.edit { putBoolean(KEY_CONFIRM_START, value) }

    var keepScreenOn: Boolean
        get() = sp.getBoolean(KEY_KEEP_SCREEN_ON, true)
        set(value) = sp.edit { putBoolean(KEY_KEEP_SCREEN_ON, value) }

    private companion object {
        const val KEY_LAST_ADDRESS = "last_address"
        const val KEY_AUTO_CONNECT = "auto_connect"
        const val KEY_POLL_MS = "poll_ms"
        const val KEY_CONFIRM_START = "confirm_start"
        const val KEY_KEEP_SCREEN_ON = "keep_screen_on"
    }
}
