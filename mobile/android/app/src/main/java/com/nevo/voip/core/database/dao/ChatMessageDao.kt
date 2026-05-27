package com.nevo.voip.core.database.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.nevo.voip.core.database.entity.ChatMessageEntity
import kotlinx.coroutines.flow.Flow

@Dao
interface ChatMessageDao {

    @Query("SELECT * FROM chat_messages WHERE channelId = :channelId ORDER BY timestamp ASC LIMIT :limit")
    fun getMessagesByChannel(channelId: Int, limit: Int = 500): Flow<List<ChatMessageEntity>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertMessage(msg: ChatMessageEntity)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertMessages(msgs: List<ChatMessageEntity>)

    @Query("SELECT * FROM chat_messages WHERE pendingSend = 1")
    suspend fun getPendingMessages(): List<ChatMessageEntity>

    @Query("UPDATE chat_messages SET pendingSend = 0 WHERE id = :messageId")
    suspend fun markSent(messageId: Long)

    @Query("DELETE FROM chat_messages WHERE timestamp < :beforeTimestamp")
    suspend fun deleteOldMessages(beforeTimestamp: Long)
}