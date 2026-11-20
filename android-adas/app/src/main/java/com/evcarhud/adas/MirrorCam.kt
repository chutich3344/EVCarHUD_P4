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
    private val log: (String) -> Unit = {},
    /** "mirror" = cam guong Sinjet (goi API :8000 truoc), "70mai" = cam hanh trinh 70mai (RTSP thang /livestream/12, cam 192.168.0.1). */
    private val kind: String = "mirror",
) : FrameSource {
    private val is70 = kind == "70mai"
    private val HOST = if (is70) "192.168.0.1" else "192.168.5.1"
    // 70mai: duong luong cong khai cua dong M300/Pro; thu xoay vong neu cam khac tra loi
    private val paths70 = listOf("/livestream/12", "/livestream/11", "/livestream/1", "/live")
    private val th = HandlerThread("mirror").apply { start() }
    private val h = Handler(th.looper)
    private var player: ExoPlayer? = null
    private var reader: ImageReader? = null
    private var want = false
    private var retry = 0
    private var decFail = 0
    private var frames = 0
    private var frAt = 0L

    @Volatile private var sz = Size(1920, 1080)
    override val size: Size get() = sz
    // 70mai M310 goc rong ~130 do cheo -> f uoc ~450 px o ngang 1280 (tu hieu chinh se chinh lai)
    override val focalPx: Double get() = (if (is70) 450.0 else 520.0) * sz.width / 1280.0
    override val focalSrc = if (is70) "cam 70mai Wi-Fi" else "cam gương Wi-Fi"
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
    } catch (e: Exception) { "lỗi ${e.javaClass.simpleName} ${e.message?.take(80) ?: ""}" }

    /** 70mai (HiSilicon "hisnet", thttpd cong 80): GET /cgi-bin/hisnet/<cgi>. */
    private fun hisnet(net: Network, q: String): String = try {
        val c = net.openConnection(URL("http://$HOST/cgi-bin/hisnet/$q")) as HttpURLConnection
        c.connectTimeout = 2000; c.readTimeout = 3000
        val code = c.responseCode
        val b = (if (code < 400) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText().replace('\n', ' ').take(160) } ?: ""
        c.disconnect(); "$code $b"
    } catch (e: Exception) { "lỗi ${e.javaClass.simpleName} ${e.message?.take(80) ?: ""}" }

    private fun myIp(net: Network): String? {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        return cm.getLinkProperties(net)?.linkAddresses?.firstOrNull { it.address is java.net.Inet4Address }?.address?.hostAddress
    }

    // 70mai: app hang bao "dien thoai dang noi" bang client.cgi register lap lai vai giay/lan; RTSP tra 406 khi chua dang ky
    private var keep: Thread? = null
    private fun startKeep(net: Network) {
        if (keep?.isAlive == true) return
        val ip = myIp(net) ?: return
        keep = Thread {
            var n = 0
            while (want) {
                val r = hisnet(net, "client.cgi?&-operation=register&-ip=$ip")
                if (n++ < 2 || !r.startsWith("200")) log("70mai register: $r")
                try { Thread.sleep(3000) } catch (_: InterruptedException) { break }
            }
        }.apply { isDaemon = true; start() }
    }

    override fun open() {
        h.post {
            want = true
            if (player != null) return@post
            val net = wifi()
            if (net == null) { status = "chưa nối Wi-Fi cam"; onErr(if (is70) "Chưa nối Wi-Fi cam 70mai (70mai_…)" else "Chưa nối Wi-Fi cam gương (mirror_wifi…)"); retryLater(); return@post }
            val r = if (is70) {
                if (retry == 0) log("70mai thong tin: " + hisnet(net, "getdeviceattr.cgi"))
                log("70mai register: " + hisnet(net, "client.cgi?&-operation=register&-ip=${myIp(net)}"))
                startKeep(net)
                try { Thread.sleep(800) } catch (_: InterruptedException) {}
                "70mai"
            } else api(net, "enterrecorder")
            // cam can thoi gian bat may chu RTSP sau enterrecorder; xen ke UDP (mac dinh cua app hang/ffmpeg) va TCP
            val tcp = retry % 2 == 1
            status = "enterrecorder: $r · ${if (tcp) "TCP" else "UDP"}"
            Log.i(TAG, status); log(status)
            if (retry == 0 && !is70) log("rtsp_function: " + api(net, "getparamvalue?param=rtsp_function"))
            if (!is70) try { Thread.sleep(1500) } catch (_: InterruptedException) {}
            val url = if (is70) "rtsp://$HOST:554" + paths70[(retry / 2) % paths70.size] else "rtsp://$HOST:554"
            if (is70) log("70mai thu $url ${if (tcp) "TCP" else "UDP"}")
            if (!want || player != null) return@post
            // cam guong gui SPS trong SDP bi doc ra 13216x16 (20/11) -> bo giai ma tu choi; FixRenderer sua kich thuoc khi cau hinh
            val rf = androidx.media3.exoplayer.RenderersFactory { eh, vl, _, _, _ -> arrayOf(FixRenderer(ctx, eh, vl)) }
            val p = ExoPlayer.Builder(ctx, rf).setLooper(th.looper).build()
            val src = RtspMediaSource.Factory()
                .setForceUseRtpTcp(tcp)
                .setSocketFactory(net.socketFactory)      // Wi-Fi cam khong co Internet: ep di qua Wi-Fi
                .setTimeoutMs(4000)
                .createMediaSource(MediaItem.fromUri(Uri.parse(url)))
            p.addListener(object : Player.Listener {
                override fun onVideoSizeChanged(v: VideoSize) {
                    log("kich thuoc hinh ${v.width}x${v.height}")
                    if (v.width <= 0 || v.height <= 0 || bogus(v.width, v.height)) return
                    if (v.width == sz.width && v.height == sz.height && reader != null) return
                    sz = Size(v.width, v.height)
                    attachReader(p)
                    onSize()
                }
                override fun onRenderedFirstFrame() { log("khung hinh dau tien ${sz.width}x${sz.height}") }
                override fun onPlaybackStateChanged(s: Int) {
                    log("trang thai " + when (s) { Player.STATE_BUFFERING -> "dang nap"; Player.STATE_READY -> "san sang"; Player.STATE_ENDED -> "het"; else -> "nghi" })
                    if (s == Player.STATE_READY) { status = "đang nhận hình ${sz.width}x${sz.height} ${if (tcp) "TCP" else "UDP"} $url"; log(status); onErr("") }
                }
                override fun onPlayerError(e: PlaybackException) {
                    var c: Throwable? = e.cause; var why = ""
                    while (c != null && why.length < 600) { why += " ← " + c.javaClass.simpleName + (c.message?.let { ": " + it.take(220) } ?: ""); c = c.cause }
                    status = "lỗi RTSP ${if (tcp) "TCP" else "UDP"}: ${e.errorCodeName}$why"
                    if (e.errorCode == PlaybackException.ERROR_CODE_DECODER_INIT_FAILED) decFail++
                    Log.w(TAG, "rtsp", e); log(status)
                    why = why.take(160)
                    onErr("${if (is70) "Cam 70mai" else "Cam gương"} ${if (tcp) "TCP" else "UDP"}: ${e.errorCodeName}$why")
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
        // lan 2 tro di sau loi khoi tao bo giai ma: tao ImageReader co co dung (mot so bo giai ma Qualcomm doi co nay)
        val rd = if (decFail > 0 && android.os.Build.VERSION.SDK_INT >= 29)
            ImageReader.newInstance(sz.width, sz.height, ImageFormat.YUV_420_888, 3,
                android.hardware.HardwareBuffer.USAGE_CPU_READ_OFTEN or android.hardware.HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE)
        else ImageReader.newInstance(sz.width, sz.height, ImageFormat.YUV_420_888, 3)
        rd.setOnImageAvailableListener({ ir ->
            val im = try { ir.acquireLatestImage() } catch (_: Exception) { null } ?: return@setOnImageAvailableListener
            frames++
            val now = android.os.SystemClock.elapsedRealtime()
            if (now - frAt > 10_000) { log("nhan $frames khung/10 s · ${im.width}x${im.height}"); frames = 0; frAt = now }
            onFrame(im)     // onFrame tu dong close()
        }, h)
        reader = rd
        p.setVideoSurface(rd.surface)
        // KHONG dong reader cu ngay: setVideoSurface chay bat dong bo tren luong phat, bo giai ma co the dang cau hinh
        // voi surface cu -> dong ngay thi MediaCodec.configure bao IllegalArgumentException (loi DECODER_INIT_FAILED 20/11)
        if (old != null) h.postDelayed({ try { old.close() } catch (_: Exception) {} }, 3000)
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
            if (!is70) wifi()?.let { n -> Thread { api(n, "exitrecorder") }.start() }
            keep?.interrupt(); keep = null
            status = "đã tắt"
        }
    }

    override fun setRate(hz: Int) {}
    override fun setPreview(s: Surface?) {}
    override fun release() { close(); h.post { th.quitSafely() } }

    /** Bo giai ma H.264 bo qua kich thuoc vo ly trong SDP (bo giai ma tu doc SPS trong luong). */
    private class FixRenderer(c: Context, eh: Handler, vl: androidx.media3.exoplayer.video.VideoRendererEventListener) :
        androidx.media3.exoplayer.video.MediaCodecVideoRenderer(c, androidx.media3.exoplayer.mediacodec.MediaCodecSelector.DEFAULT, 0L, eh, vl, 50) {
        override fun getMediaFormat(format: androidx.media3.common.Format, codecMimeType: String, codecMaxValues: CodecMaxValues,
                                    codecOperatingRate: Float, deviceNeedsNoPostProcessWorkaround: Boolean, tunnelingAudioSessionId: Int): android.media.MediaFormat {
            val f = super.getMediaFormat(format, codecMimeType, codecMaxValues, codecOperatingRate, deviceNeedsNoPostProcessWorkaround, tunnelingAudioSessionId)
            if (bogus(format.width, format.height)) {
                f.setInteger(android.media.MediaFormat.KEY_WIDTH, 3840); f.setInteger(android.media.MediaFormat.KEY_HEIGHT, 2160)
                f.setInteger(android.media.MediaFormat.KEY_MAX_WIDTH, 3840); f.setInteger(android.media.MediaFormat.KEY_MAX_HEIGHT, 2160)
                f.setInteger(android.media.MediaFormat.KEY_MAX_INPUT_SIZE, 3840 * 2160 * 3 / 4)
            }
            return f
        }
    }

    companion object {
        fun bogus(w: Int, h: Int) = w > 8192 || h > 8192 || (w > 0 && w < 64) || (h > 0 && h < 64)
        private const val TAG = "MirrorCam"
    }
}
