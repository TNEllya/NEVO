package com.nevo.voip.core.datastore

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import org.json.JSONObject

private val Context.volumeDataStore: DataStore<Preferences> by preferencesDataStore(name = "user_volume_preferences")

class UserVolumePreferences(private val context: Context) {

    companion object {
        val KEY_PER_USER_VOLUME = stringPreferencesKey("per_user_volume")

        const val DEFAULT_VOLUME = 1.0
        const val MIN_VOLUME = 0.0
        const val MAX_VOLUME = 2.0
    }

    val allVolumes: Flow<Map<String, Double>> = context.volumeDataStore.data.map { preferences ->
        loadVolumeMap(preferences[KEY_PER_USER_VOLUME] ?: "{}")
    }

    suspend fun setUserVolume(userId: String, volume: Double) {
        val clampedVolume = volume.coerceIn(MIN_VOLUME, MAX_VOLUME)
        context.volumeDataStore.edit { preferences ->
            val currentJson = preferences[KEY_PER_USER_VOLUME] ?: "{}"
            val map = loadVolumeMap(currentJson).toMutableMap()
            map[userId] = clampedVolume
            preferences[KEY_PER_USER_VOLUME] = JSONObject(map).toString()
        }
    }

    suspend fun getUserVolume(userId: String): Double {
        val preferences = context.volumeDataStore.data
        var result = DEFAULT_VOLUME
        preferences.collect { prefs ->
            val map = loadVolumeMap(prefs[KEY_PER_USER_VOLUME] ?: "{}")
            result = map[userId] ?: DEFAULT_VOLUME
            return@collect
        }
        return result
    }

    suspend fun getAllVolumes(): Map<String, Double> {
        val preferences = context.volumeDataStore.data
        var result = emptyMap<String, Double>()
        preferences.collect { prefs ->
            result = loadVolumeMap(prefs[KEY_PER_USER_VOLUME] ?: "{}")
            return@collect
        }
        return result
    }

    suspend fun removeUserVolume(userId: String) {
        context.volumeDataStore.edit { preferences ->
            val currentJson = preferences[KEY_PER_USER_VOLUME] ?: "{}"
            val map = loadVolumeMap(currentJson).toMutableMap()
            map.remove(userId)
            preferences[KEY_PER_USER_VOLUME] = JSONObject(map).toString()
        }
    }

    private fun loadVolumeMap(json: String): Map<String, Double> {
        return try {
            val jsonObject = JSONObject(json)
            val map = mutableMapOf<String, Double>()
            jsonObject.keys().forEach { key ->
                map[key] = jsonObject.getDouble(key)
            }
            map
        } catch (e: Exception) {
            emptyMap()
        }
    }
}