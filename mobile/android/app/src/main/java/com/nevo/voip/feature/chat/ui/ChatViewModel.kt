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

    private val channelId: Long = when (val value = savedStateHandle.get<Any>("channelId")) {
        is Long -> value
        is Int -> value.toLong()
        is String -> value.toLongOrNull() ?: 0L
        else -> 0L
    }

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
                        channelId = broadcast.channelId,
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

    fun appendEmoji(emoji: String) {
        _uiState.update { it.copy(inputText = it.inputText + emoji) }
    }

    fun sendMessage() {
        val text = _uiState.value.inputText.trim()
        if (text.isEmpty() || _uiState.value.isSending) return

        val entity = ChatMessageEntity(
            channelId = channelId,
            senderId = 0,
            senderName = "",
            content = text,
            messageType = "chat",
            timestamp = System.currentTimeMillis(),
            pendingSend = true
        )

        viewModelScope.launch {
            val insertedId = chatRepository.insertMessage(entity)
            _uiState.update { it.copy(inputText = "", isSending = true) }
            chatRepository.sendMessage(channelId, text)
                .onSuccess { chatRepository.markMessageSent(insertedId) }
                .onFailure {
                    // Message sending failed — keep pendingSend = true for UI display
                }
            _uiState.update { it.copy(isSending = false) }
        }
    }
}