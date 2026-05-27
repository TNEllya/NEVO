package com.nevo.voip.core.database.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "server_history")
data class ServerHistoryEntity(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val host: String,
    val port: Int,
    val username: String,
    val lastConnectedAt: Long,
    val serverName: String? = null,
    val useCount: Int = 1
)