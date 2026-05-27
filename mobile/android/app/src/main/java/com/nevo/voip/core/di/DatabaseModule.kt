package com.nevo.voip.core.di

import android.content.Context
import androidx.room.Room
import com.nevo.voip.core.database.NevoDatabase
import com.nevo.voip.core.database.dao.ChatMessageDao
import com.nevo.voip.core.database.dao.FileCacheDao
import com.nevo.voip.core.database.dao.ServerHistoryDao
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object DatabaseModule {

    @Provides
    @Singleton
    fun provideNevoDatabase(@ApplicationContext context: Context): NevoDatabase {
        return Room.databaseBuilder(
            context,
            NevoDatabase::class.java,
            "nevo_voip.db"
        )
            .fallbackToDestructiveMigration()
            .build()
    }

    @Provides
    fun provideChatMessageDao(db: NevoDatabase): ChatMessageDao = db.chatMessageDao()

    @Provides
    fun provideServerHistoryDao(db: NevoDatabase): ServerHistoryDao = db.serverHistoryDao()

    @Provides
    fun provideFileCacheDao(db: NevoDatabase): FileCacheDao = db.fileCacheDao()
}