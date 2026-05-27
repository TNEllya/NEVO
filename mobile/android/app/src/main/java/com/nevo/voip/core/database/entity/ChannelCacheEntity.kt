package com.nevo.voip.core.database.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "channel_cache")
data class ChannelCacheEntity(
    @PrimaryKey
    val channelId: Long,
    val channelName: String,
    val parentId: Long,
    val serverHost: String,
    val serverPort: Int,
    val lastUpdated: Long,
    val rawData: ByteArray
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is ChannelCacheEntity) return false
        return channelId == other.channelId &&
                channelName == other.channelName &&
                parentId == other.parentId &&
                serverHost == other.serverHost &&
                serverPort == other.serverPort &&
                lastUpdated == other.lastUpdated &&
                rawData.contentEquals(other.rawData)
    }

    override fun hashCode(): Int {
        var result = channelId.hashCode()
        result = 31 * result + channelName.hashCode()
        result = 31 * result + parentId.hashCode()
        result = 31 * result + serverHost.hashCode()
        result = 31 * result + serverPort
        result = 31 * result + lastUpdated.hashCode()
        result = 31 * result + rawData.contentHashCode()
        return result
    }
}