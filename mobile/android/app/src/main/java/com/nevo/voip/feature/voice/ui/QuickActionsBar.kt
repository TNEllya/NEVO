package com.nevo.voip.feature.voice.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.HeadsetOff
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material.icons.filled.ScreenShare
import androidx.compose.material3.FilledIconToggleButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedIconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.nevo.voip.ui.theme.ErrorRed

@Composable
fun QuickActionsBar(
    isMuted: Boolean,
    isDeafened: Boolean,
    onToggleMute: () -> Unit,
    onToggleDeafen: () -> Unit,
    onScreenShare: () -> Unit,
    onChat: () -> Unit
) {
    Surface(
        tonalElevation = 3.dp,
        shadowElevation = 8.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.SpaceEvenly,
            verticalAlignment = Alignment.CenterVertically
        ) {
            ActionButton(
                icon = if (isMuted) Icons.Filled.MicOff else Icons.Filled.Mic,
                label = if (isMuted) "Unmute" else "Mute",
                isActive = isMuted,
                activeTint = ErrorRed,
                onClick = onToggleMute
            )

            ActionButton(
                icon = if (isDeafened) Icons.Filled.HeadsetOff else Icons.Filled.Headphones,
                label = if (isDeafened) "Undeafen" else "Deafen",
                isActive = isDeafened,
                activeTint = ErrorRed,
                onClick = onToggleDeafen
            )

            ActionButton(
                icon = Icons.Filled.ScreenShare,
                label = "Share",
                isActive = false,
                activeTint = Color.Unspecified,
                onClick = onScreenShare
            )

            ActionButton(
                icon = Icons.AutoMirrored.Filled.Chat,
                label = "Chat",
                isActive = false,
                activeTint = Color.Unspecified,
                onClick = onChat
            )
        }
    }
}

@Composable
private fun ActionButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    isActive: Boolean,
    activeTint: Color,
    onClick: () -> Unit
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (isActive && activeTint != Color.Unspecified) {
            FilledIconToggleButton(
                checked = true,
                onCheckedChange = { onClick() },
                modifier = Modifier.size(48.dp)
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = label,
                    modifier = Modifier.size(24.dp),
                    tint = activeTint
                )
            }
        } else {
            OutlinedIconButton(
                onClick = onClick,
                modifier = Modifier.size(48.dp)
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = label,
                    modifier = Modifier.size(24.dp)
                )
            }
        }
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = if (isActive && activeTint != Color.Unspecified)
                activeTint
            else
                MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}