package com.nevo.voip

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import androidx.navigation.compose.rememberNavController
import com.nevo.voip.core.datastore.NevoPreferences
import com.nevo.voip.ui.navigation.NevoNavGraph
import com.nevo.voip.ui.theme.NevoTheme
import dagger.hilt.android.AndroidEntryPoint
import java.util.Locale

@AndroidEntryPoint
class MainActivity : ComponentActivity() {

    private val permissionsToRequest = buildList {
        add(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_CONNECT)
        }
    }

    private lateinit var preferences: NevoPreferences

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { _ -> }

    override fun attachBaseContext(newBase: Context) {
        val prefs = NevoPreferences(newBase)
        val lang = prefs.languageSnapshot
        val locale = when (lang) {
            NevoPreferences.LANGUAGE_ZH_CN -> Locale.SIMPLIFIED_CHINESE
            NevoPreferences.LANGUAGE_ZH_TW -> Locale.TRADITIONAL_CHINESE
            else -> Locale.ENGLISH
        }
        Locale.setDefault(locale)
        val config = Configuration(newBase.resources.configuration)
        config.setLocale(locale)
        super.attachBaseContext(newBase.createConfigurationContext(config))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        preferences = NevoPreferences(this)
        requestNeededPermissions()

        setContent {
            val themeMode by preferences.themeMode.collectAsState(initial = NevoPreferences.THEME_SYSTEM)
            val isDark = when (themeMode) {
                NevoPreferences.THEME_DARK -> true
                NevoPreferences.THEME_LIGHT -> false
                else -> resources.configuration.uiMode.and(Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
            }
            NevoTheme(darkTheme = isDark) {
                NevoNavGraph(rememberNavController())
            }
        }
    }

    private fun requestNeededPermissions() {
        val ungranted = permissionsToRequest.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (ungranted.isNotEmpty()) {
            permissionLauncher.launch(ungranted.toTypedArray())
        }
    }
}