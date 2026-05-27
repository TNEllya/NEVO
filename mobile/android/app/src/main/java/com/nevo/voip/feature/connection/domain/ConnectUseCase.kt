package com.nevo.voip.feature.connection.domain

import android.util.Log
import com.nevo.voip.core.model.LoginResponse
import com.nevo.voip.feature.connection.data.ConnectionRepository
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import javax.inject.Inject

sealed class ConnectionResult {
    data object Loading : ConnectionResult()
    data class Success(val response: LoginResponse) : ConnectionResult()
    data class Error(val message: String) : ConnectionResult()
}

class ConnectUseCase @Inject constructor(
    private val connectionRepository: ConnectionRepository
) {
    operator fun invoke(
        host: String,
        port: String,
        username: String,
        password: String
    ): Flow<ConnectionResult> = flow {
        emit(ConnectionResult.Loading)

        if (host.isBlank()) {
            emit(ConnectionResult.Error("Server address cannot be empty"))
            return@flow
        }

        val portInt = port.toIntOrNull()
        if (portInt == null || portInt < 1 || portInt > 65535) {
            emit(ConnectionResult.Error("Port must be between 1 and 65535"))
            return@flow
        }

        if (username.isBlank()) {
            emit(ConnectionResult.Error("Username cannot be empty"))
            return@flow
        }

        Log.d(TAG, "Connecting to $host:$portInt as $username")
        val result = connectionRepository.connect(host, portInt, username, password)

        result.fold(
            onSuccess = { response ->
                Log.d(TAG, "Connection successful: userId=${response.userInfo?.id}")
                emit(ConnectionResult.Success(response))
            },
            onFailure = { error ->
                val msg = error.message ?: error.javaClass.simpleName
                Log.e(TAG, "Connection failed: $msg", error)
                emit(ConnectionResult.Error("Connection failed: $msg"))
            }
        )
    }

    companion object {
        private const val TAG = "ConnectUseCase"
    }
}