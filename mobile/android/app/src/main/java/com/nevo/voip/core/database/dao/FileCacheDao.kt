package com.nevo.voip.core.database.dao

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import androidx.room.Update
import com.nevo.voip.core.database.entity.FileCacheEntity
import kotlinx.coroutines.flow.Flow

@Dao
interface FileCacheDao {

    @Query("SELECT * FROM file_cache ORDER BY lastAccessedAt DESC")
    fun getAll(): Flow<List<FileCacheEntity>>

    @Query("SELECT * FROM file_cache WHERE fileId = :fileId")
    suspend fun getByFileId(fileId: Long): FileCacheEntity?

    @Query("SELECT * FROM file_cache WHERE channelId = :channelId ORDER BY cachedAt DESC")
    fun getByChannelId(channelId: Int): Flow<List<FileCacheEntity>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(entity: FileCacheEntity)

    @Update
    suspend fun update(entity: FileCacheEntity)

    @Delete
    suspend fun delete(entity: FileCacheEntity)

    @Query("DELETE FROM file_cache WHERE fileId = :fileId")
    suspend fun deleteByFileId(fileId: Long)

    @Query("UPDATE file_cache SET lastAccessedAt = :timestamp WHERE fileId = :fileId")
    suspend fun updateLastAccessed(fileId: Long, timestamp: Long)

    @Query("DELETE FROM file_cache WHERE lastAccessedAt < :beforeTimestamp")
    suspend fun deleteExpired(beforeTimestamp: Long)
}