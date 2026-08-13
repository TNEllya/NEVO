package com.nevo.voip.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.nevo.voip.R
import com.nevo.voip.feature.voice.data.VoiceEngine

class NevoAudioService : Service() {

    companion object {
        const val CHANNEL_ID = "nevo_voip_channel"
        private const val TAG = "NevoAudioService"
        private const val NOTIFICATION_ID = 1001
        private const val ACTION_STOP = "com.nevo.voip.ACTION_STOP"

        fun createNotificationChannel(context: Context) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val channel = NotificationChannel(
                    CHANNEL_ID,
                    context.getString(R.string.notification_channel_voip_name),
                    NotificationManager.IMPORTANCE_LOW
                ).apply {
                    description = context.getString(R.string.notification_channel_voip_description)
                    setShowBadge(false)
                    lockscreenVisibility = Notification.VISIBILITY_PUBLIC
                }
                val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
                manager.createNotificationChannel(channel)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
        }

        return try {
            val notification = buildNotification()
            startForeground(NOTIFICATION_ID, notification)
            START_STICKY
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start foreground audio service", e)
            stopSelf()
            START_NOT_STICKY
        }
    }

    override fun onDestroy() {
        // 服务销毁即停止语音采集/播放：VoiceEngine 由进程内单例持有，
        // 此前此处为空实现，导致服务停止后通话继续占用麦克风/扬声器。
        // stopAll 会释放 AudioRecord/AudioTrack 并重置会话密钥，
        // 之后重新进入频道时 startVoiceEngine 可正常重新初始化。
        try {
            VoiceEngine.getInstance()?.stopAll()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to stop voice engine on service destroy", e)
        }
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(): Notification {
        val contentIntent = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(
                this, 0, it,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
        }

        val stopIntent = Intent(this, NevoAudioService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 0, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.call_in_progress))
            .setContentText(getString(R.string.nevo_voip_call))
            .setSmallIcon(R.drawable.ic_voip_notification)
            .setOngoing(true)
            .setSilent(true)
            .setContentIntent(contentIntent)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, getString(R.string.hang_up), stopPendingIntent)
            .build()
    }
}