package com.nevo.voip.core.database

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.TypeConverters
import com.nevo.voip.core.database.dao.ChatMessageDao
import com.nevo.voip.core.database.dao.FileCacheDao
import com.nevo.voip.core.database.dao.ServerHistoryDao
import com.nevo.voip.core.database.entity.ChannelCacheEntity
import com.nevo.voip.core.database.entity.ChatMessageEntity
import com.nevo.voip.core.database.entity.FileCacheEntity
import com.nevo.voip.core.database.entity.ServerHistoryEntity

@Database(
    entities = [
        ChatMessageEntity::class,
        ServerHistoryEntity::class,
        FileCacheEntity::class,
        ChannelCacheEntity::class
    ],
    version = 1,
    exportSchema = false
)
@TypeConverters(Converters::class)
abstract class NevoDatabase : RoomDatabase() {

    abstract fun chatMessageDao(): ChatMessageDao
    abstract fun serverHistoryDao(): ServerHistoryDao
    abstract fun fileCacheDao(): FileCacheDao

    companion object {
        @Volatile
        private var INSTANCE: NevoDatabase? = null

        fun getInstance(context: Context): NevoDatabase {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: Room.databaseBuilder(
                    context.applicationContext,
                    NevoDatabase::class.java,
                    "nevo_voip.db"
                )
                    .fallbackToDestructiveMigration()
                    .build()
                    .also { INSTANCE = it }
            }
        }
    }
}