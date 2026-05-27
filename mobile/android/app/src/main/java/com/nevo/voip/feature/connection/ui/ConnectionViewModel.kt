package com.nevo.voip.feature.connection.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nevo.voip.core.database.dao.ServerHistoryDao
import com.nevo.voip.core.database.entity.ServerHistoryEntity
import com.nevo.voip.feature.connection.data.ConnectionRepository
import com.nevo.voip.feature.connection.domain.ConnectUseCase
import com.nevo.voip.feature.connection.domain.ConnectionResult
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

data class ConnectionUiState(
    val host: String = "",
    val port: String = "",
    val username: String = "",
    val password: String = "",
    val isConnecting: Boolean = false,
    val errorMessage: String? = null,
    val connectedHost: String? = null,
    val connectedPort: Int? = null,
    val serverHistory: List<ServerHistoryEntity> = emptyList()
)

@HiltViewModel
class ConnectionViewModel @Inject constructor(
    private val connectUseCase: ConnectUseCase,
    private val connectionRepository: ConnectionRepository,
    private val serverHistoryDao: ServerHistoryDao
) : ViewModel() {

    private val _uiState = MutableStateFlow(ConnectionUiState())
    val uiState: StateFlow<ConnectionUiState> = _uiState.asStateFlow()

    init {
        loadServerHistory()
    }

    private fun loadServerHistory() {
        viewModelScope.launch {
            serverHistoryDao.getAllOrdered().collect { history ->
                _uiState.update { it.copy(serverHistory = history) }
            }
        }
    }

    fun onHostChange(value: String) {
        _uiState.update { it.copy(host = value, errorMessage = null) }
    }

    fun onPortChange(value: String) {
        _uiState.update { it.copy(port = value, errorMessage = null) }
    }

    fun onUsernameChange(value: String) {
        _uiState.update { it.copy(username = value, errorMessage = null) }
    }

    fun onPasswordChange(value: String) {
        _uiState.update { it.copy(password = value, errorMessage = null) }
    }

    fun connect() {
        val state = _uiState.value
        if (state.isConnecting) return

        viewModelScope.launch {
            _uiState.update { it.copy(isConnecting = true, errorMessage = null) }

            connectUseCase(
                host = state.host,
                port = state.port,
                username = state.username,
                password = state.password
            ).collect { result ->
                when (result) {
                    is ConnectionResult.Loading -> Unit
                    is ConnectionResult.Success -> {
                        _uiState.update {
                            it.copy(
                                isConnecting = false,
                                connectedHost = state.host,
                                connectedPort = state.port.toIntOrNull(),
                                errorMessage = null
                            )
                        }
                    }
                    is ConnectionResult.Error -> {
                        _uiState.update {
                            it.copy(
                                isConnecting = false,
                                errorMessage = result.message
                            )
                        }
                    }
                }
            }
        }
    }

    fun disconnect() {
        viewModelScope.launch {
            connectionRepository.disconnect()
        }
    }

    fun onHistoryItemClicked(host: String, port: Int) {
        _uiState.update {
            it.copy(
                host = host,
                port = port.toString(),
                errorMessage = null
            )
        }
    }

    fun deleteHistoryItem(entity: ServerHistoryEntity) {
        viewModelScope.launch {
            serverHistoryDao.delete(entity)
        }
    }

    fun clearError() {
        _uiState.update { it.copy(errorMessage = null) }
    }
}