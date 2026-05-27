package com.nevo.voip.core.datastore

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "nevo_preferences")

class NevoPreferences(private val context: Context) {

    companion object {
        val KEY_THEME_MODE = stringPreferencesKey("theme_mode")
        val KEY_LANGUAGE = stringPreferencesKey("language")
        val KEY_PTT_ENABLED = booleanPreferencesKey("ptt_enabled")
        val KEY_VAD_SENSITIVITY = intPreferencesKey("vad_sensitivity")
        val KEY_NOISE_SUPPRESSION_LEVEL = intPreferencesKey("noise_suppression_level")
        val KEY_LAST_CONNECTED_HOST = stringPreferencesKey("last_connected_host")
        val KEY_LAST_CONNECTED_PORT = intPreferencesKey("last_connected_port")
        val KEY_LAST_USERNAME = stringPreferencesKey("last_username")

        const val THEME_LIGHT = "LIGHT"
        const val THEME_DARK = "DARK"
        const val THEME_SYSTEM = "SYSTEM"

        const val LANGUAGE_EN = "en"
        const val LANGUAGE_ZH_CN = "zh_CN"
        const val LANGUAGE_ZH_TW = "zh_TW"
    }

    val themeMode: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[KEY_THEME_MODE] ?: THEME_SYSTEM
    }

    val language: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[KEY_LANGUAGE] ?: LANGUAGE_EN
    }

    val languageSnapshot: String
        get() = runBlocking { context.dataStore.data.first()[KEY_LANGUAGE] ?: LANGUAGE_EN }

    val pttEnabled: Flow<Boolean> = context.dataStore.data.map { preferences ->
        preferences[KEY_PTT_ENABLED] ?: false
    }

    val vadSensitivity: Flow<Int> = context.dataStore.data.map { preferences ->
        preferences[KEY_VAD_SENSITIVITY] ?: 1
    }

    val noiseSuppressionLevel: Flow<Int> = context.dataStore.data.map { preferences ->
        preferences[KEY_NOISE_SUPPRESSION_LEVEL] ?: 1
    }

    val lastConnectedHost: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[KEY_LAST_CONNECTED_HOST] ?: ""
    }

    val lastConnectedPort: Flow<Int> = context.dataStore.data.map { preferences ->
        preferences[KEY_LAST_CONNECTED_PORT] ?: 0
    }

    val lastUsername: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[KEY_LAST_USERNAME] ?: ""
    }

    suspend fun setThemeMode(mode: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_THEME_MODE] = mode
        }
    }

    suspend fun setLanguage(lang: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_LANGUAGE] = lang
        }
    }

    suspend fun setPttEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[KEY_PTT_ENABLED] = enabled
        }
    }

    suspend fun setVadSensitivity(level: Int) {
        context.dataStore.edit { preferences ->
            preferences[KEY_VAD_SENSITIVITY] = level.coerceIn(0, 3)
        }
    }

    suspend fun setNoiseSuppressionLevel(level: Int) {
        context.dataStore.edit { preferences ->
            preferences[KEY_NOISE_SUPPRESSION_LEVEL] = level.coerceIn(0, 3)
        }
    }

    suspend fun setLastConnectedHost(host: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_LAST_CONNECTED_HOST] = host
        }
    }

    suspend fun setLastConnectedPort(port: Int) {
        context.dataStore.edit { preferences ->
            preferences[KEY_LAST_CONNECTED_PORT] = port
        }
    }

    suspend fun setLastUsername(username: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_LAST_USERNAME] = username
        }
    }
}