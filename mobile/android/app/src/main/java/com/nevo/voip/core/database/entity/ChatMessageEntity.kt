package com.nevo.voip.core.database.entity

import androidx.room.Entity
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(
    tableName = "chat_messages",
    indices = [
        Index(value = ["channelId", "timestamp"]),
        Index(value = ["channelId"])
    ]
)
data class ChatMessageEntity(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val channelId: Int,
    val senderId: Long,
    val senderName: String,
    val content: String,
    val messageType: String,
    val attachmentUrl: String? = null,
    val attachmentName: String? = null,
    val attachmentSize: Long? = null,
    val timestamp: Long,
    val isRead: Boolean = false,
    val pendingSend: Boolean = false
)