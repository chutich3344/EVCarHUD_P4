package com.evcarhud.adas

import android.content.Context
import android.graphics.ImageFormat
import android.media.Image
import android.media.ImageReader
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import android.view.Surface
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.VideoSize
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.rtsp.RtspMediaSource
import java.net.HttpURLConnection
import java.net.URL

/**
 * Cam guong Wi-Fi (app hang "rearview_wifi", Sinjet): API http://192.168.5.1:8000/app/...,
 * goi app/enterrecorder thi cam mo RTSP rtsp://192.168.5.1:554. Giai ma bang Media3 ra ImageReader YUV
 * -> cung duong onFrame(Image) nhu camera dien thoai.
 * Goc nhin: 3 clip 4K 17/11 khop f ~ 520 px o ngang 1280 (~102 do).
 */
class MirrorCam(
    private val ctx: Context,
    private val onFrame: (Image) -> Unit,
    private val onErr: (String) -> Unit,
    private val onSize: () -> Unit,
) : FrameSource {
    private val th = HandlerThread("mirror").apply { start() }
    private val h = Handler(th.looper)
    private var player: ExoPlayer? = null
    private var reader: ImageReader? = null
    private var want = false
    private var retry = 0

    @Volatile private var sz = Size(1920, 1080)
    override val size: Size get() = sz
    override val focalPx: Double get() = 520.0 * sz.width / 1280.0
    override val focalSrc = "cam gương Wi-Fi"
    override val flip = false
    override val isOpen get() = want
    @Volatile var status = "chưa mở"

    private fun wifi(): Network? {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        @Suppress("DEPRECATION")
        return cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
    }

    private fun api(net: Network, q: String): String = try {
        val c = net.openConnection(URL("http://$HOST:8000/app/$q")) as HttpURLConnection
        c.connectTimeout = 2000; c.readTimeout = 3000
        val code = c.responseCode
        val b = (if (code < 400) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText().take(120) } ?: ""
        c.disconnect(); "$code $b"
    } catch (e: Exception) { "lỗi ${e.javaClass.simpleName}" }

    override fun open() {
        h.post {
            want = true
            if (player != null) return@post
            val net = wifi()
            if (net == null) { status = "chưa nối Wi-Fi cam gương"; onErr("Chưa nối Wi-Fi cam gương (mirror_wifi…)"); retryLater(); return@post }
            val r = api(net, "enterrecorder")
            status = "enterrecorder: $r"
            Log.i(TAG, status)
            val p = ExoPlayer.Builder(ctx).setLooper(th.looper).build()
            val src = RtspMediaSource.Factory()
                .setForceUseRtpTcp(true)
                .setSocketFactory(net.socketFactory)      // Wi-Fi cam khong co Internet: ep di qua Wi-Fi
                .setTimeoutMs(4000)
                .createMediaSource(MediaItem.fromUri(Uri.parse("rtsp://$HOST:554")))
            p.addListener(object : Player.Listener {
                override fun onVideoSizeChanged(v: VideoSize) {
                    if (v.width <= 0 || v.height <= 0) return
                    if (v.width == sz.width && v.height == sz.height && reader != null) return
                    sz = Size(v.width, v.height)
                    attachReader(p)
                    onSize()
                }
                override fun onPlaybackStateChanged(s: Int) {
                    if (s == Player.STATE_READY) { status = "đang nhận hình ${sz.width}x${sz.height}"; retry = 0; onErr("") }
                }
                override fun onPlayerError(e: PlaybackException) {
                    status = "lỗi RTSP: ${e.errorCodeName}"
                    Log.w(TAG, "rtsp", e)
                    onErr("Cam gương: ${e.errorCodeName}")
                    stopPlayer(); retryLater()
                }
            })
            attachReader(p)
            p.setMediaSource(src)
            p.volume = 0f
            p.playWhenReady = true
            p.prepare()
            player = p
        }
    }

    private fun attachReader(p: ExoPlayer) {
        val old = reader
        val rd = ImageReader.newInstance(sz.width, sz.height, ImageFormat.YUV_420_888, 3)
        rd.setOnImageAvailableListener({ ir ->
            val im = try { ir.acquireLatestImage() } catch (_: Exception) { null } ?: return@setOnImageAvailableListener
            onFrame(im)     // onFrame tu dong close()
        }, h)
        reader = rd
        p.setVideoSurface(rd.surface)
        old?.close()
    }

    private fun retryLater() {
        if (!want) return
        retry++
        h.postDelayed({ if (want && player == null) open() }, minOf(15_000L, 2000L * retry))
    }

    private fun stopPlayer() {
        try { player?.release() } catch (_: Exception) {}
        player = null
        reader?.close(); reader = null
    }

    override fun close() {
        h.post {
            want = false
            stopPlayer()
            wifi()?.let { n -> Thread { api(n, "exitrecorder") }.start() }
            status = "đã tắt"
        }
    }

    override fun setRate(hz: Int) {}
    override fun setPreview(s: Surface?) {}
    override fun release() { close(); h.post { th.quitSafely() } }

    companion object {
        private const val TAG = "MirrorCam"
        private const val HOST = "192.168.5.1"
    }
}
