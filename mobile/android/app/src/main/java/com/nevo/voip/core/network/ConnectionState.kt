package com.nevo.voip.core.network

sealed class ConnectionState {
    data object Disconnected : ConnectionState()
    data object Connecting : ConnectionState()
    data class Connected(
        val serverName: String,
        val userId: Long,
        val sessionId: Long
    ) : ConnectionState()
    data class InChannel(
        val channelId: Long,
        val channelName: String
    ) : ConnectionState()
    data class Error(
        val message: String
    ) : ConnectionState()
}