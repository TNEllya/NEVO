package com.nevo.voip.feature.channel.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.ExitToApp
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material.icons.filled.ScreenShare
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.TextSnippet
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.nevo.voip.core.model.UserInfo
import com.nevo.voip.ui.theme.OnlineGreen
import com.nevo.voip.ui.theme.AwayAmber
import com.nevo.voip.ui.theme.ErrorRed
import com.nevo.voip.feature.voice.ui.QuickActionsBar

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChannelContent(
    onChannelClick: (Long) -> Unit,
    onScreenShareClick: (Long) -> Unit,
    onBack: () -> Unit
) {
    val viewModel: ChannelViewModel = hiltViewModel()
    val currentState by viewModel.uiState.collectAsState()

    var showCreateDialog by remember { mutableStateOf(false) }
    var createChannelName by remember { mutableStateOf("") }
    var createParentId by remember { mutableStateOf(0L) }
    var showRenameDialog by remember { mutableStateOf(false) }
    var renameChannelId by remember { mutableStateOf(0L) }
    var renameChannelName by remember { mutableStateOf("") }
    var showDeleteConfirm by remember { mutableStateOf(false) }
    var deleteChannelId by remember { mutableStateOf(0L) }

    if (showCreateDialog) {
        AlertDialog(
            onDismissRequest = { showCreateDialog = false },
            title = { Text("Create Channel") },
            text = {
                OutlinedTextField(
                    value = createChannelName,
                    onValueChange = { createChannelName = it },
                    label = { Text("Channel Name") },
                    singleLine = true
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    if (createChannelName.isNotBlank()) {
                        viewModel.createChannel(createChannelName, createParentId)
                        createChannelName = ""
                        showCreateDialog = false
                    }
                }) { Text("Create") }
            },
            dismissButton = {
                TextButton(onClick = { showCreateDialog = false }) { Text("Cancel") }
            }
        )
    }

    if (showRenameDialog) {
        AlertDialog(
            onDismissRequest = { showRenameDialog = false },
            title = { Text("Rename Channel") },
            text = {
                OutlinedTextField(
                    value = renameChannelName,
                    onValueChange = { renameChannelName = it },
                    label = { Text("New Name") },
                    singleLine = true
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    if (renameChannelName.isNotBlank()) {
                        viewModel.renameChannel(renameChannelId, renameChannelName)
                        showRenameDialog = false
                    }
                }) { Text("Rename") }
            },
            dismissButton = {
                TextButton(onClick = { showRenameDialog = false }) { Text("Cancel") }
            }
        )
    }

    if (showDeleteConfirm) {
        AlertDialog(
            onDismissRequest = { showDeleteConfirm = false },
            title = { Text("Delete Channel") },
            text = { Text("Are you sure you want to delete this channel? This action cannot be undone.") },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.deleteChannel(deleteChannelId)
                    showDeleteConfirm = false
                }) { Text("Delete", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteConfirm = false }) { Text("Cancel") }
            }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        currentState.currentChannelName.ifEmpty { "NEVO" },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                }
            )
        },
        bottomBar = {
            QuickActionsBar(
                isMuted = currentState.isMuted,
                isDeafened = currentState.isDeafened,
                onToggleMute = { viewModel.toggleMute() },
                onToggleDeafen = { viewModel.toggleDeafen() },
                onScreenShare = { onScreenShareClick(0L) },
                onChat = {
                    if (currentState.currentChannelId != 0L) {
                        onChannelClick(currentState.currentChannelId)
                    }
                }
            )
        }
    ) { padding ->
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            Column(modifier = Modifier.weight(0.4f)) {
                LazyColumn(
                    modifier = Modifier
                        .fillMaxHeight()
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f))
                ) {
                    items(currentState.channels, key = { it.channel.id }) { node ->
                        ChannelTreeItem(
                            node = node,
                            isSelected = node.channel.id == currentState.currentChannelId,
                            onSelect = { viewModel.joinChannel(node.channel.id) },
                            onExpandToggle = { viewModel.toggleExpand(node.channel.id) },
                            onLongPress = {
                                createParentId = node.channel.id
                                showCreateDialog = true
                            },
                            onRename = {
                                renameChannelId = node.channel.id
                                renameChannelName = node.channel.name
                                showRenameDialog = true
                            },
                            onDelete = {
                                deleteChannelId = node.channel.id
                                showDeleteConfirm = true
                            },
                            onLeave = {
                                if (currentState.currentChannelId == node.channel.id) {
                                    viewModel.leaveChannel()
                                }
                            }
                        )
                    }
                }
            }

            VerticalDivider()

            Column(
                modifier = Modifier
                    .weight(0.6f)
                    .fillMaxHeight()
            ) {
                if (currentState.currentChannelId != 0L && currentState.isInChannel) {
                    Text(
                        text = "Users in ${currentState.currentChannelName}",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(12.dp)
                    )
                    LazyColumn(
                        modifier = Modifier.fillMaxSize()
                    ) {
                        items(currentState.usersInChannel, key = { it.id }) { user ->
                            UserListItem(user = user)
                        }
                        if (currentState.usersInChannel.isEmpty()) {
                            item {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(32.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text(
                                        "No users in this channel",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontStyle = FontStyle.Italic,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                } else {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(32.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(
                                "Select a channel",
                                style = MaterialTheme.typography.headlineSmall
                            )
                            Spacer(Modifier.height(8.dp))
                            Text(
                                "Choose a channel from the list to join the conversation",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ChannelTreeItem(
    node: FlatChannelNode,
    isSelected: Boolean,
    onSelect: () -> Unit,
    onExpandToggle: () -> Unit,
    onLongPress: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
    onLeave: () -> Unit
) {
    var showContextMenu by remember { mutableStateOf(false) }
    val channel = node.channel
    val isVoiceChannel = channel.name.contains("voice", ignoreCase = true) ||
            channel.name.contains("🔊")

    val rotationAngle by animateFloatAsState(
        targetValue = if (node.isExpanded) -90f else 0f,
        animationSpec = tween(200)
    )

    val bgColor = if (isSelected) {
        MaterialTheme.colorScheme.primaryContainer
    } else {
        Color.Transparent
    }

    Box {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(bgColor)
                .padding(start = (node.depth * 24 + 8).dp, end = 8.dp, top = 10.dp, bottom = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (node.hasChildren) {
                IconButton(
                    onClick = onExpandToggle,
                    modifier = Modifier.size(24.dp)
                ) {
                    Icon(
                        Icons.Filled.KeyboardArrowRight,
                        contentDescription = if (node.isExpanded) "Collapse" else "Expand",
                        modifier = Modifier.rotate(rotationAngle),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            } else {
                Spacer(Modifier.width(24.dp))
            }

            Row(
                modifier = Modifier
                    .weight(1f)
                    .combinedClickable(
                        onClick = onSelect,
                        onLongClick = { showContextMenu = true }
                    ),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = if (isVoiceChannel) "🔊" else "🔤",
                    style = MaterialTheme.typography.bodyLarge
                )

                Spacer(Modifier.width(8.dp))

                Text(
                    text = channel.name,
                    style = MaterialTheme.typography.bodyMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f)
                )

                if (channel.users.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .size(22.dp)
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.secondaryContainer),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = channel.users.size.toString(),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    }
                }
            }
        }

        DropdownMenu(
            expanded = showContextMenu,
            onDismissRequest = { showContextMenu = false },
            offset = DpOffset(16.dp, 0.dp)
        ) {
            DropdownMenuItem(
                text = { Text("Create Sub-channel") },
                onClick = {
                    showContextMenu = false
                    onLongPress()
                },
                leadingIcon = { Icon(Icons.Filled.TextSnippet, null) }
            )
            DropdownMenuItem(
                text = { Text("Rename") },
                onClick = {
                    showContextMenu = false
                    onRename()
                },
                leadingIcon = { Icon(Icons.Filled.Edit, null) }
            )
            DropdownMenuItem(
                text = { Text("Delete", color = MaterialTheme.colorScheme.error) },
                onClick = {
                    showContextMenu = false
                    onDelete()
                },
                leadingIcon = {
                    Icon(Icons.Filled.Delete, null, tint = MaterialTheme.colorScheme.error)
                }
            )
            DropdownMenuItem(
                text = { Text("Leave") },
                onClick = {
                    showContextMenu = false
                    onLeave()
                },
                leadingIcon = { Icon(Icons.Filled.ExitToApp, null) }
            )
        }
    }
}

@Composable
private fun UserListItem(user: UserInfo) {
    val statusColor = when (user.status) {
        1 -> OnlineGreen
        2 -> AwayAmber
        3 -> ErrorRed
        else -> OnlineGreen
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(modifier = Modifier.size(40.dp)) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.primaryContainer)
                    .align(Alignment.Center),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = user.username.firstOrNull()?.uppercase() ?: "?",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
            Box(
                modifier = Modifier
                    .size(12.dp)
                    .clip(CircleShape)
                    .background(statusColor)
                    .align(Alignment.BottomEnd)
            )
        }
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = user.username,
                style = MaterialTheme.typography.bodyMedium
            )
            if (user.muted) {
                Text(
                    text = "Muted",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }
        if (user.muted) {
            Icon(
                Icons.Filled.MicOff,
                contentDescription = "Muted",
                tint = MaterialTheme.colorScheme.error,
                modifier = Modifier.size(18.dp)
            )
        }
    }
}