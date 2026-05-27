package com.nevo.voip.core.database

import android.util.Base64
import androidx.room.TypeConverter
import org.json.JSONArray

class Converters {

    @TypeConverter
    fun fromByteArray(value: ByteArray?): String? {
        return value?.let { Base64.encodeToString(it, Base64.NO_WRAP) }
    }

    @TypeConverter
    fun toByteArray(value: String?): ByteArray? {
        return value?.let { Base64.decode(it, Base64.NO_WRAP) }
    }

    @TypeConverter
    fun fromStringList(value: List<String>?): String? {
        return value?.let {
            val jsonArray = JSONArray()
            it.forEach { item -> jsonArray.put(item) }
            jsonArray.toString()
        }
    }

    @TypeConverter
    fun toStringList(value: String?): List<String>? {
        return value?.let {
            val jsonArray = JSONArray(it)
            (0 until jsonArray.length()).map { i -> jsonArray.getString(i) }
        }
    }
}