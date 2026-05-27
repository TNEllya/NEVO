package com.nevo.voip.core.database.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "file_cache")
data class FileCacheEntity(
    @PrimaryKey
    val fileId: Long,
    val fileName: String,
    val fileSize: Long,
    val localPath: String,
    val mimeType: String,
    val cachedAt: Long,
    val lastAccessedAt: Long,
    val channelId: Int
)