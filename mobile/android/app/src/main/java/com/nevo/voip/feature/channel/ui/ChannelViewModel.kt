package com.nevo.voip.feature.channel.ui

import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nevo.voip.core.datastore.NevoPreferences
import com.nevo.voip.core.model.ChannelInfo
import com.nevo.voip.core.model.UserInfo
import com.nevo.voip.feature.channel.data.ChannelRepository
import com.nevo.voip.feature.connection.data.ConnectionRepository
import com.nevo.voip.feature.voice.data.VoiceEngine
import com.nevo.voip.service.NevoAudioService
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

data class FlatChannelNode(
    val channel: ChannelInfo,
    val depth: Int,
    val hasChildren: Boolean,
    val isExpanded: Boolean
)

data class ChannelUiState(
    val channels: List<FlatChannelNode> = emptyList(),
    val currentChannelId: Long = 0,
    val currentChannelName: String = "",
    val isInChannel: Boolean = false,
    val usersInChannel: List<UserInfo> = emptyList(),
    val expandedChannelIds: Set<Long> = emptySet(),
    val isMuted: Boolean = false,
    val isDeafened: Boolean = false
)

@HiltViewModel
class ChannelViewModel @Inject constructor(
    private val connectionRepository: ConnectionRepository,
    private val channelRepository: ChannelRepository,
    private val voiceEngine: VoiceEngine,
    @ApplicationContext private val context: Context
) : ViewModel() {

    private val _uiState = MutableStateFlow(ChannelUiState())
    val uiState: StateFlow<ChannelUiState> = _uiState.asStateFlow()

    private val _isMuted = MutableStateFlow(false)
    val isMuted: StateFlow<Boolean> = _isMuted.asStateFlow()

    private val _isDeafened = MutableStateFlow(false)
    val isDeafened: StateFlow<Boolean> = _isDeafened.asStateFlow()

    private val preferences = NevoPreferences(context)

    init {
        viewModelScope.launch {
            connectionRepository.channelListUpdates.collect { update ->
                val expanded = _uiState.value.expandedChannelIds
                val flatList = flattenChannels(update.channels, expanded)
                _uiState.update { state ->
                    state.copy(
                        channels = flatList,
                        isInChannel = state.currentChannelId != 0L
                    )
                }
                if (update.channels.isNotEmpty() && _uiState.value.currentChannelId == 0L) {
                    val firstChannel = update.channels.first()
                    _uiState.update { it.copy(
                        currentChannelId = firstChannel.id,
                        currentChannelName = firstChannel.name,
                        usersInChannel = firstChannel.users
                    )}
                }
            }
        }
        viewModelScope.launch {
            connectionRepository.userJoinedChannel.collect { event ->
                if (event.channelId == _uiState.value.currentChannelId && event.user != null) {
                    _uiState.update { state ->
                        val updated = state.usersInChannel.toMutableList()
                        if (updated.none { it.id == event.user!!.id }) {
                            updated.add(event.user!!)
                        }
                        state.copy(usersInChannel = updated)
                    }
                }
            }
        }
        viewModelScope.launch {
            connectionRepository.userLeftChannel.collect { event ->
                if (event.channelId == _uiState.value.currentChannelId) {
                    _uiState.update { state ->
                        state.copy(
                            usersInChannel = state.usersInChannel.filter { it.id != event.userId }
                        )
                    }
                }
            }
        }
        viewModelScope.launch {
            connectionRepository.userSpeaking.collect { _ -> }
        }
    }

    fun joinChannel(channelId: Long) {
        viewModelScope.launch {
            channelRepository.joinChannel(channelId).onSuccess {
                val channel = findChannelById(
                    _uiState.value.channels.map { it.channel },
                    channelId
                )
                _uiState.update { state ->
                    state.copy(
                        currentChannelId = channelId,
                        currentChannelName = channel?.name ?: state.currentChannelName,
                        isInChannel = true,
                        usersInChannel = channel?.users ?: state.usersInChannel
                    )
                }
                startVoiceEngine()
            }
        }
    }

    fun leaveChannel() {
        viewModelScope.launch {
            stopVoiceEngine()
            channelRepository.leaveChannel().onSuccess {
                _uiState.update { state ->
                    state.copy(
                        currentChannelId = 0,
                        currentChannelName = "",
                        isInChannel = false,
                        usersInChannel = emptyList()
                    )
                }
            }
        }
    }

    fun createChannel(name: String, parentId: Long) {
        viewModelScope.launch {
            channelRepository.createChannel(name, parentId)
        }
    }

    fun deleteChannel(channelId: Long) {
        viewModelScope.launch {
            channelRepository.deleteChannel(channelId)
        }
    }

    fun renameChannel(channelId: Long, newName: String) {
        viewModelScope.launch {
            channelRepository.renameChannel(channelId, newName)
        }
    }

    fun toggleExpand(channelId: Long) {
        _uiState.update { state ->
            val updated = state.expandedChannelIds.toMutableSet()
            if (updated.contains(channelId)) {
                updated.remove(channelId)
            } else {
                updated.add(channelId)
            }
            val rootChannels = state.channels.map { it.channel }
            val allChannels = findAllChannels(rootChannels)
            val newFlatList = flattenChannels(allChannels, updated)
            state.copy(
                expandedChannelIds = updated,
                channels = newFlatList
            )
        }
    }

    fun toggleMute() {
        val newMuted = !_isMuted.value
        _isMuted.value = newMuted
        _uiState.update { it.copy(isMuted = newMuted) }
        voiceEngine.setMuted(newMuted)
    }

    fun toggleDeafen() {
        val newDeafened = !_isDeafened.value
        _isDeafened.value = newDeafened
        _uiState.update { it.copy(isDeafened = newDeafened) }
        voiceEngine.setDeafened(newDeafened)
    }

    private fun startVoiceEngine() {
        viewModelScope.launch {
            val host = preferences.lastConnectedHost.first()
            val port = preferences.lastConnectedPort.first()
            if (host.isNotBlank() && port > 0) {
                voiceEngine.setServerInfo(host, port)
            }
            startAudioService()
            voiceEngine.startMicrophone()
            voiceEngine.startSpeaker(port)
        }
    }

    private fun stopVoiceEngine() {
        voiceEngine.stopAll()
        stopAudioService()
    }

    private fun startAudioService() {
        val intent = Intent(context, NevoAudioService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }
    }

    private fun stopAudioService() {
        context.stopService(Intent(context, NevoAudioService::class.java))
    }

    private fun flattenChannels(
        channels: List<ChannelInfo>,
        expandedIds: Set<Long>,
        depth: Int = 0
    ): List<FlatChannelNode> {
        val result = mutableListOf<FlatChannelNode>()
        for (channel in channels) {
            val hasChildren = channel.children.isNotEmpty()
            val isExpanded = expandedIds.contains(channel.id)
            result.add(
                FlatChannelNode(
                    channel = channel,
                    depth = depth,
                    hasChildren = hasChildren,
                    isExpanded = isExpanded
                )
            )
            if (hasChildren && isExpanded) {
                result.addAll(flattenChannels(channel.children, expandedIds, depth + 1))
            }
        }
        return result
    }

    private fun findAllChannels(channels: List<ChannelInfo>): List<ChannelInfo> {
        val result = mutableListOf<ChannelInfo>()
        for (channel in channels) {
            result.add(channel)
            result.addAll(findAllChannels(channel.children))
        }
        return result
    }

    private fun findChannelById(
        channels: List<ChannelInfo>,
        channelId: Long
    ): ChannelInfo? {
        for (channel in channels) {
            if (channel.id == channelId) return channel
            val found = findChannelById(channel.children, channelId)
            if (found != null) return found
        }
        return null
    }
}