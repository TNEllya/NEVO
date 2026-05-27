package com.nevo.voip.feature.settings.ui

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.nevo.voip.core.datastore.NevoPreferences
import com.nevo.voip.core.update.ReleaseInfo
import com.nevo.voip.core.update.UpdateState
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsContent(
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val activity = remember { context.findActivity() }
    val preferences = remember { NevoPreferences(context) }
    val scope = rememberCoroutineScope()
    val updateViewModel: UpdateViewModel = hiltViewModel()
    val updateManager = updateViewModel.updateManager
    val updateState by updateManager.state.collectAsState()

    var themeMode by remember { mutableStateOf("") }
    var language by remember { mutableStateOf("") }
    var pttEnabled by remember { mutableStateOf(false) }
    var vadSensitivity by remember { mutableFloatStateOf(0f) }
    var noiseSuppression by remember { mutableFloatStateOf(0f) }
    var showUpdateDialog by remember { mutableStateOf(false) }
    var pendingReleaseInfo by remember { mutableStateOf<ReleaseInfo?>(null) }

    LaunchedEffect(Unit) {
        themeMode = preferences.themeMode.first()
        language = preferences.language.first()
        pttEnabled = preferences.pttEnabled.first()
        vadSensitivity = preferences.vadSensitivity.first().toFloat()
        noiseSuppression = preferences.noiseSuppressionLevel.first().toFloat()
    }

    if (showUpdateDialog) {
        UpdateDialog(
            state = updateState,
            currentVersion = updateManager.currentVersion(),
            onDismiss = {
                showUpdateDialog = false
                updateManager.resetState()
            },
            onDownload = { info ->
                pendingReleaseInfo = info
                scope.launch { updateManager.downloadApk(info) }
            },
            onInstall = {
                val state = updateState
                if (state is UpdateState.Ready) {
                    updateManager.installApk(state.file)
                    showUpdateDialog = false
                    updateManager.resetState()
                }
            },
            onRetry = {
                val info = pendingReleaseInfo
                if (info != null) {
                    scope.launch { updateManager.downloadApk(info) }
                } else {
                    scope.launch { updateManager.checkForUpdate() }
                    pendingReleaseInfo = null
                }
            }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
        ) {
            SettingsSectionHeader("Appearance")
            Text(
                text = "Theme",
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.padding(start = 16.dp, top = 12.dp)
            )
            ThemeOption("Light", themeMode == NevoPreferences.THEME_LIGHT) {
                themeMode = NevoPreferences.THEME_LIGHT
                scope.launch { preferences.setThemeMode(NevoPreferences.THEME_LIGHT) }
            }
            ThemeOption("Dark", themeMode == NevoPreferences.THEME_DARK) {
                themeMode = NevoPreferences.THEME_DARK
                scope.launch { preferences.setThemeMode(NevoPreferences.THEME_DARK) }
            }
            ThemeOption("System default", themeMode == NevoPreferences.THEME_SYSTEM) {
                themeMode = NevoPreferences.THEME_SYSTEM
                scope.launch { preferences.setThemeMode(NevoPreferences.THEME_SYSTEM) }
            }

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()

            SettingsSectionHeader("Language")
            LanguageOption("English", language == NevoPreferences.LANGUAGE_EN) {
                language = NevoPreferences.LANGUAGE_EN
                scope.launch {
                    preferences.setLanguage(NevoPreferences.LANGUAGE_EN)
                    activity?.recreate()
                }
            }
            LanguageOption("简体中文", language == NevoPreferences.LANGUAGE_ZH_CN) {
                language = NevoPreferences.LANGUAGE_ZH_CN
                scope.launch {
                    preferences.setLanguage(NevoPreferences.LANGUAGE_ZH_CN)
                    activity?.recreate()
                }
            }
            LanguageOption("繁體中文", language == NevoPreferences.LANGUAGE_ZH_TW) {
                language = NevoPreferences.LANGUAGE_ZH_TW
                scope.launch {
                    preferences.setLanguage(NevoPreferences.LANGUAGE_ZH_TW)
                    activity?.recreate()
                }
            }

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()

            SettingsSectionHeader("Audio")
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Push to Talk",
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.weight(1f)
                )
                Switch(
                    checked = pttEnabled,
                    onCheckedChange = {
                        pttEnabled = it
                        scope.launch { preferences.setPttEnabled(it) }
                    }
                )
            }

            Text(
                text = "VAD Sensitivity: ${vadSensitivity.toInt()}",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(start = 16.dp, top = 8.dp)
            )
            Slider(
                value = vadSensitivity,
                onValueChange = { vadSensitivity = it },
                onValueChangeFinished = {
                    scope.launch {
                        preferences.setVadSensitivity(vadSensitivity.toInt())
                    }
                },
                valueRange = 0f..3f,
                steps = 2,
                modifier = Modifier.padding(horizontal = 16.dp)
            )

            Text(
                text = "Noise Suppression: ${noiseSuppression.toInt()}",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(start = 16.dp, top = 8.dp)
            )
            Slider(
                value = noiseSuppression,
                onValueChange = { noiseSuppression = it },
                onValueChangeFinished = {
                    scope.launch {
                        preferences.setNoiseSuppressionLevel(noiseSuppression.toInt())
                    }
                },
                valueRange = 0f..3f,
                steps = 2,
                modifier = Modifier.padding(horizontal = 16.dp)
            )

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()

            SettingsSectionHeader("About")
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "NEVO VoIP",
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        text = "Version ${updateManager.currentVersion()}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = "Build ${updateManager.currentVersionCode()}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
                Button(
                    onClick = {
                        showUpdateDialog = true
                        pendingReleaseInfo = null
                        scope.launch { updateManager.checkForUpdate() }
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                ) {
                    Icon(
                        Icons.Filled.SystemUpdate,
                        contentDescription = null,
                        modifier = Modifier.padding(end = 6.dp)
                    )
                    Text("Check Updates")
                }
            }
            Spacer(Modifier.height(24.dp))
        }
    }
}

@Composable
private fun SettingsSectionHeader(title: String) {
    Text(
        text = title,
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 8.dp)
    )
}

@Composable
private fun ThemeOption(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RadioButton(selected = selected, onClick = null)
        Spacer(Modifier.padding(start = 8.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium
        )
    }
}

@Composable
private fun LanguageOption(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RadioButton(selected = selected, onClick = null)
        Spacer(Modifier.padding(start = 8.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium
        )
    }
}

private fun Context.findActivity(): Activity? {
    var ctx = this
    while (ctx is ContextWrapper) {
        if (ctx is Activity) return ctx
        ctx = ctx.baseContext
    }
    return null
}