package com.nevo.voip.core.update

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.core.content.FileProvider
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okio.buffer
import okio.sink
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import javax.inject.Inject
import javax.inject.Singleton

data class ReleaseInfo(
    val tagName: String,
    val versionName: String,
    val description: String,
    val assetUrl: String,
    val mirrorAssetUrl: String,
    val assetName: String,
    val assetSize: Long,
    val htmlUrl: String
)

sealed class UpdateState {
    data object Idle : UpdateState()
    data object Checking : UpdateState()
    data class Available(val info: ReleaseInfo) : UpdateState()
    data object UpToDate : UpdateState()
    data class Downloading(val progress: Float, val viaMirror: Boolean = false) : UpdateState()
    data class Ready(val file: File) : UpdateState()
    data class Error(val message: String) : UpdateState()
}

@Singleton
class UpdateManager @Inject constructor(
    @ApplicationContext private val context: Context
) {
    private val client = OkHttpClient.Builder()
        .followRedirects(true)
        .followSslRedirects(true)
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .build()

    private val _state = MutableStateFlow<UpdateState>(UpdateState.Idle)
    val state: StateFlow<UpdateState> = _state.asStateFlow()

    companion object {
        private const val GITHUB_API = "https://api.github.com/repos/TNEllya/nevo-android/releases/latest"
    }

    fun currentVersion(): String {
        return try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            info.versionName ?: "0.0.0"
        } catch (_: PackageManager.NameNotFoundException) {
            "0.0.0"
        }
    }

    fun currentVersionCode(): Long {
        return try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                info.longVersionCode
            } else {
                @Suppress("DEPRECATION")
                info.versionCode.toLong()
            }
        } catch (_: PackageManager.NameNotFoundException) {
            0L
        }
    }

    suspend fun checkForUpdate(): ReleaseInfo? = withContext(Dispatchers.IO) {
        _state.value = UpdateState.Checking
        try {
            val request = Request.Builder()
                .url(GITHUB_API)
                .header("Accept", "application/vnd.github+json")
                .build()
            val response = client.newCall(request).execute()
            if (!response.isSuccessful) {
                _state.value = UpdateState.Error("Server: HTTP ${response.code}")
                return@withContext null
            }
            val body = response.body?.string() ?: run {
                _state.value = UpdateState.Error("Server: empty response")
                return@withContext null
            }
            val json = JSONObject(body)
            val tagName = json.getString("tag_name")
            val description = json.optString("body", "")
            val htmlUrl = json.optString("html_url", "")
            val assets = json.getJSONArray("assets")
            if (assets.length() == 0) {
                _state.value = UpdateState.Error("No assets in release")
                return@withContext null
            }
            val asset = assets.getJSONObject(0)
            val directUrl = asset.getString("browser_download_url")
            val mirrorUrl = toMirrorUrl(directUrl)
            val info = ReleaseInfo(
                tagName = tagName,
                versionName = tagName.removePrefix("v"),
                description = description,
                assetUrl = directUrl,
                mirrorAssetUrl = mirrorUrl,
                assetName = asset.getString("name"),
                assetSize = asset.getLong("size"),
                htmlUrl = htmlUrl
            )
            val latestVersion = info.versionName
            val current = currentVersion()
            if (isNewerVersion(latestVersion, current)) {
                _state.value = UpdateState.Available(info)
                info
            } else {
                _state.value = UpdateState.UpToDate
                null
            }
        } catch (e: IOException) {
            _state.value = UpdateState.Error("Network: ${e.message ?: "unreachable"}")
            null
        }
    }

    suspend fun downloadApk(info: ReleaseInfo): File? = withContext(Dispatchers.IO) {
        _state.value = UpdateState.Downloading(0f, viaMirror = false)
        val dir = File(context.cacheDir, "updates")
        dir.mkdirs()
        val dest = File(dir, "nevo_update_${info.tagName}.apk")
        if (dest.exists()) dest.delete()

        val result = tryDownload(dest, info.mirrorAssetUrl, viaMirror = true)
        if (result != null) return@withContext result

        val directResult = tryDownload(dest, info.assetUrl, viaMirror = false)
        if (directResult != null) return@withContext directResult

        _state.value = UpdateState.Error("Download failed: all mirrors unreachable")
        null
    }

    private fun tryDownload(dest: File, url: String, viaMirror: Boolean): File? {
        try {
            _state.value = UpdateState.Downloading(0f, viaMirror = viaMirror)
            val request = Request.Builder().url(url).build()
            val response = client.newCall(request).execute()
            if (!response.isSuccessful) return null

            val body = response.body ?: return null
            val total = body.contentLength()
            var downloaded = 0L
            val sink = dest.sink().buffer()
            body.byteStream().use { input ->
                sink.use { output ->
                    val buffer = ByteArray(8192)
                    var bytesRead: Int
                    while (input.read(buffer).also { bytesRead = it } != -1) {
                        output.write(buffer, 0, bytesRead)
                        downloaded += bytesRead
                        if (total > 0) {
                            _state.value = UpdateState.Downloading(
                                downloaded.toFloat() / total.toFloat(),
                                viaMirror = viaMirror
                            )
                        }
                    }
                }
            }
            _state.value = UpdateState.Ready(dest)
            return dest
        } catch (_: IOException) {
            return null
        }
    }

    fun installApk(file: File) {
        val uri: Uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            file
        )
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        }
        if (intent.resolveActivity(context.packageManager) != null) {
            context.startActivity(intent)
        }
    }

    fun resetState() {
        _state.value = UpdateState.Idle
    }

    private fun toMirrorUrl(directUrl: String): String {
        return "https://ghproxy.com/$directUrl"
    }

    private fun isNewerVersion(latest: String, current: String): Boolean {
        val latestParts = latest.split(".").map { it.toIntOrNull() ?: 0 }
        val currentParts = current.split(".").map { it.toIntOrNull() ?: 0 }
        val maxLen = maxOf(latestParts.size, currentParts.size)
        for (i in 0 until maxLen) {
            val l = latestParts.getOrElse(i) { 0 }
            val c = currentParts.getOrElse(i) { 0 }
            if (l > c) return true
            if (l < c) return false
        }
        return false
    }
}