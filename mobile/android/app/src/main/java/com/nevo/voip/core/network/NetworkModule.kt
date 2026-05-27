package com.nevo.voip.core.network

import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object NetworkModule {

    @Provides
    @Singleton
    fun provideTcpConnectionManager(): TcpConnectionManager {
        return TcpConnectionManager()
    }

    @Provides
    @Singleton
    fun provideUdpSocketManager(): UdpSocketManager {
        return UdpSocketManager()
    }
}