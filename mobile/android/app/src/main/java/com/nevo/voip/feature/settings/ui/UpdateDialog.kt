package com.nevo.voip.feature.settings.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.nevo.voip.core.update.ReleaseInfo
import com.nevo.voip.core.update.UpdateState
import java.util.Locale

@Composable
fun UpdateDialog(
    state: UpdateState,
    currentVersion: String,
    onDismiss: () -> Unit,
    onDownload: (ReleaseInfo) -> Unit,
    onInstall: () -> Unit,
    onRetry: () -> Unit
) {
    Dialog(
        onDismissRequest = {
            if (state !is UpdateState.Downloading) onDismiss()
        },
        properties = DialogProperties(dismissOnBackPress = state !is UpdateState.Downloading)
    ) {
        androidx.compose.material3.Surface(
            shape = MaterialTheme.shapes.extraLarge,
            tonalElevation = 6.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                when (state) {
                    is UpdateState.Checking -> {
                        CircularProgressIndicator()
                        Spacer(Modifier.height(16.dp))
                        Text("Checking for updates...", style = MaterialTheme.typography.bodyLarge)
                    }

                    is UpdateState.Available -> {
                        Text("Update Available", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(8.dp))
                        Text("Version ${state.info.versionName}", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
                        Spacer(Modifier.height(12.dp))
                        if (state.info.description.isNotBlank()) {
                            Text(
                                state.info.description,
                                style = MaterialTheme.typography.bodyMedium,
                                textAlign = TextAlign.Center,
                                modifier = Modifier.fillMaxWidth()
                            )
                            Spacer(Modifier.height(8.dp))
                        }
                        Text(
                            "Size: ${formatSize(state.info.assetSize)}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(20.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            OutlinedButton(onClick = onDismiss) { Text("Later") }
                            Spacer(Modifier.width(12.dp))
                            Button(onClick = { onDownload(state.info) }) { Text("Download") }
                        }
                    }

                    is UpdateState.UpToDate -> {
                        Text("You're up to date!", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "Current version: $currentVersion",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(20.dp))
                        Button(onClick = onDismiss) { Text("OK") }
                    }

                    is UpdateState.Downloading -> {
                        Text("Downloading Update", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(16.dp))
                        LinearProgressIndicator(
                            progress = { state.progress },
                            modifier = Modifier.fillMaxWidth()
                        )
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "${(state.progress * 100).toInt()}%",
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }

                    is UpdateState.Ready -> {
                        Text("Download Complete", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "The update is ready to install.",
                            style = MaterialTheme.typography.bodyMedium
                        )
                        Spacer(Modifier.height(20.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            OutlinedButton(onClick = onDismiss) { Text("Later") }
                            Spacer(Modifier.width(12.dp))
                            Button(onClick = onInstall) { Text("Install") }
                        }
                    }

                    is UpdateState.Error -> {
                        Text("Update Failed", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.error)
                        Spacer(Modifier.height(8.dp))
                        Text(
                            state.message,
                            style = MaterialTheme.typography.bodyMedium,
                            textAlign = TextAlign.Center
                        )
                        Spacer(Modifier.height(20.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            OutlinedButton(onClick = onDismiss) { Text("Close") }
                            Spacer(Modifier.width(12.dp))
                            Button(onClick = onRetry) { Text("Retry") }
                        }
                    }

                    is UpdateState.Idle -> {}
                }
            }
        }
    }
}

private fun formatSize(bytes: Long): String {
    val mb = bytes / (1024.0 * 1024.0)
    return if (mb >= 1) String.format(Locale.US, "%.1f MB", mb)
    else String.format(Locale.US, "%.0f KB", bytes / 1024.0)
}