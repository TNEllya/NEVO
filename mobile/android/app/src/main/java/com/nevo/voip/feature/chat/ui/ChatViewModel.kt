package com.nevo.voip.feature.chat.ui

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nevo.voip.core.database.entity.ChatMessageEntity
import com.nevo.voip.feature.chat.data.ChatRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

data class ChatUiState(
    val messages: List<ChatMessageEntity> = emptyList(),
    val inputText: String = "",
    val isSending: Boolean = false,
    val channelId: Long = 0
)

@HiltViewModel
class ChatViewModel @Inject constructor(
    private val chatRepository: ChatRepository,
    savedStateHandle: SavedStateHandle
) : ViewModel() {

    private val channelId: Long = savedStateHandle.get<String>("channelId")?.toLongOrNull() ?: 0L

    private val _uiState = MutableStateFlow(ChatUiState(channelId = channelId))
    val uiState: StateFlow<ChatUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            chatRepository.getMessages(channelId).collect { messages ->
                _uiState.update { it.copy(messages = messages) }
            }
        }
        viewModelScope.launch {
            chatRepository.chatBroadcasts.collect { broadcast ->
                if (broadcast.channelId == channelId) {
                    val entity = ChatMessageEntity(
                        channelId = broadcast.channelId.toInt(),
                        senderId = broadcast.senderId,
                        senderName = broadcast.senderName,
                        content = broadcast.text,
                        messageType = "chat",
                        timestamp = broadcast.timestamp
                    )
                    chatRepository.insertMessage(entity)
                }
            }
        }
    }

    fun onInputChanged(text: String) {
        _uiState.update { it.copy(inputText = text) }
    }

    fun updateInputText(text: String) {
        _uiState.update { it.copy(inputText = text) }
    }

    fun sendMessage() {
        val text = _uiState.value.inputText.trim()
        if (text.isEmpty() || _uiState.value.isSending) return

        val entity = ChatMessageEntity(
            channelId = channelId.toInt(),
            senderId = 0,
            senderName = "",
            content = text,
            messageType = "chat",
            timestamp = System.currentTimeMillis(),
            pendingSend = true
        )

        viewModelScope.launch {
            chatRepository.insertMessage(entity)
            _uiState.update { it.copy(inputText = "", isSending = true) }
            chatRepository.sendMessage(channelId, text).onSuccess {
                chatRepository.markMessageSent(entity.id)
            }
            _uiState.update { it.copy(isSending = false) }
        }
    }
}