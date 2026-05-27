package com.nevo.voip

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import com.nevo.voip.service.NevoAudioService
import dagger.hilt.android.HiltAndroidApp

@HiltAndroidApp
class NevoApplication : Application() {

    override fun onCreate() {
        super.onCreate()
        instance = this
        createNotificationChannels()
    }

    private fun createNotificationChannels() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val manager = getSystemService(NotificationManager::class.java)

            val voipChannel = NotificationChannel(
                NevoAudioService.CHANNEL_ID,
                getString(R.string.notification_channel_voip_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = getString(R.string.notification_channel_voip_description)
                setShowBadge(false)
                lockscreenVisibility = android.app.Notification.VISIBILITY_PUBLIC
            }

            val messageChannel = NotificationChannel(
                CHANNEL_MESSAGES,
                getString(R.string.notification_channel_messages_name),
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = getString(R.string.notification_channel_messages_description)
                enableVibration(true)
            }

            val updateChannel = NotificationChannel(
                CHANNEL_UPDATES,
                getString(R.string.notification_channel_updates_name),
                NotificationManager.IMPORTANCE_DEFAULT
            ).apply {
                description = getString(R.string.notification_channel_updates_description)
            }

            manager.createNotificationChannels(
                listOf(voipChannel, messageChannel, updateChannel)
            )
        }
    }

    companion object {
        const val CHANNEL_MESSAGES = "nevo_messages_channel"
        const val CHANNEL_UPDATES = "nevo_updates_channel"

        @Volatile
        private var instance: NevoApplication? = null

        fun getInstance(): NevoApplication =
            instance ?: throw IllegalStateException("NevoApplication not initialized")
    }
}