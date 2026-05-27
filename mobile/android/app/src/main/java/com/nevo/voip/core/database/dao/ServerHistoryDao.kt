package com.nevo.voip.core.database.dao

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.nevo.voip.core.database.entity.ServerHistoryEntity
import kotlinx.coroutines.flow.Flow

@Dao
interface ServerHistoryDao {

    @Query("SELECT * FROM server_history ORDER BY lastConnectedAt DESC")
    fun getAllOrdered(): Flow<List<ServerHistoryEntity>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertOrUpdate(entity: ServerHistoryEntity)

    @Delete
    suspend fun delete(entity: ServerHistoryEntity)
}