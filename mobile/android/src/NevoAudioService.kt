/**
 * NevoAudioService.kt
 * NEVO VoIP - Android Foreground Audio Service
 *
 * This service ensures that the VoIP audio pipeline continues running
 * when the app is moved to the background. Android requires a foreground
 * service with a persistent notification for long-running audio operations.
 *
 * Key behaviors:
 *   - Runs as a foreground service with a persistent notification
 *   - Uses START_STICKY to automatically restart if killed by the system
 *   - Creates a notification channel on Android Oreo (8.0) and above
 *   - Displays call duration and status in the notification
 *   - Supports stopping the service via notification action
 *
 * Lifecycle:
 *   1. Activity starts service via startForegroundService()
 *   2. Service creates notification channel and shows notification
 *   3. Service promotes itself to foreground with startForeground()
 *   4. Audio pipeline runs in the background
 *   5. Activity stops service when call ends
 *
 * Thread safety:
 *   - All notification operations run on the main thread
 *   - Audio processing runs on a separate audio thread (managed by the native layer)
 */

package com.nevo.voip

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

/**
 * Foreground service for background VoIP audio.
 *
 * Maintains the audio pipeline when the app is in the background.
 * Displays a persistent notification to inform the user that
 * a VoIP call is active.
 */
class NevoAudioService : Service() {

    companion object {
        /** Notification channel ID for VoIP calls */
        private const val CHANNEL_ID = "nevo_voip_channel"

        /** Notification ID for the foreground service notification */
        private const val NOTIFICATION_ID = 1001

        /** Action string for stopping the service from notification */
        private const val ACTION_STOP = "com.nevo.voip.ACTION_STOP"

        /**
         * Create the notification channel (required on Android Oreo+).
         * Should be called once before posting any notification.
         *
         * @param context Application context
         */
        fun createNotificationChannel(context: Context) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val channel = NotificationChannel(
                    CHANNEL_ID,
                    "NEVO VoIP Call",
                    NotificationManager.IMPORTANCE_LOW  // Low importance: no sound
                ).apply {
                    description = "Ongoing VoIP call notification"
                    setShowBadge(false)
                    lockscreenVisibility = Notification.VISIBILITY_PUBLIC
                }

                val notificationManager =
                    context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
                notificationManager.createNotificationChannel(channel)
            }
        }
    }

    // ================================================================
    // Service Lifecycle
    // ================================================================

    /**
     * Called when the service is created.
     * Initializes the notification channel.
     */
    override fun onCreate() {
        super.onCreate()
        createNotificationChannel(this)
    }

    /**
     * Called each time the service is started via startService() or
     * startForegroundService().
     *
     * Promotes the service to foreground with a persistent notification
     * and returns START_STICKY to ensure restart on system kill.
     *
     * @param intent  The Intent supplied to startService()
     * @param flags   Additional data about this start request
     * @param startId A unique integer representing this specific start request
     * @return START_STICKY to ensure service restart after system kill
     */
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                // User requested stop from notification
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
        }

        // Build and show the foreground notification
        val notification = buildNotification()
        startForeground(NOTIFICATION_ID, notification)

        // Return START_STICKY so the service is restarted if killed
        return START_STICKY
    }

    /**
     * Called when the service is no longer used and is being destroyed.
     * Cleans up any resources held by the service.
     */
    override fun onDestroy() {
        super.onDestroy()
        // The native audio layer is cleaned up by the Qt/C++ side
    }

    /**
     * Binding is not supported for this service.
     *
     * @return null (binding not supported)
     */
    override fun onBind(intent: Intent?): IBinder? = null

    // ================================================================
    // Notification Building
    // ================================================================

    /**
     * Build the foreground service notification.
     *
     * The notification displays:
     *   - App icon
     *   - "NEVO VoIP Call" title
     *   - "Call in progress" message
     *   - A "Hang Up" action button to stop the service
     *
     * @return The constructed Notification
     */
    private fun buildNotification(): Notification {
        // Intent to open the main activity when notification is tapped
        val contentIntent = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(
                this,
                0,
                it,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
        }

        // Intent to stop the service when "Hang Up" is tapped
        val stopIntent = Intent(this, NevoAudioService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this,
            0,
            stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("NEVO VoIP Call")
            .setContentText("Call in progress")
            .setSmallIcon(R.drawable.ic_voip_notification)
            .setOngoing(true)  // User cannot dismiss by swiping
            .setSilent(true)   // No sound for notification
            .setContentIntent(contentIntent)
            .addAction(
                android.R.drawable.ic_menu_close_clear_cancel,
                "Hang Up",
                stopPendingIntent
            )
            .build()
    }
}
