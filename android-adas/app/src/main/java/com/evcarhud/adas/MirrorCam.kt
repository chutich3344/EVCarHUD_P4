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
import org.videolan.libvlc.LibVLC
import org.videolan.libvlc.Media
import org.videolan.libvlc.MediaPlayer
import java.net.HttpURLConnection
import java.net.URL

/**
 * Cam guong Wi-Fi (app hang "rearview_wifi", Sinjet): API http://192.168.5.1:8000/app/...,
 * goi app/enterrecorder thi cam mo RTSP rtsp://192.168.5.1:554.
 * 70mai: RTSP thang rtsp://192.168.0.1:554/livestream/12 sau khi dang ky client.cgi.
 *
 * 20/11: ExoPlayer/Media3 noi duoc RTSP nhung khong keo duoc goi RTP tu may dam cam re
 * (nap mai khong co khung hinh). Chuyen sang libVLC (cung dong giai ma mpv/VLC ma app hang dung):
 * VLC keo luong -> bo giai ma phan cung ghi thang vao Surface cua ImageReader(YUV_420_888)
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
    private var vlc: LibVLC? = null
    private var player: MediaPlayer? = null
    private var reader: ImageReader? = null
    private var attached = false
    private var want = false
    private var retry = 0
    private var frames = 0
    @Volatile private var gotFrame = false
    private var frAt = 0L

    @Volatile private var sz = Size(1280, 720)
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
        val b = (if (code < 400) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText().replace(Regex("\\s+"), " ").take(220) } ?: ""
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
            // cam can thoi gian bat may chu RTSP sau enterrecorder; xen ke TCP/UDP qua moi lan thu lai
            val tcp = retry % 2 == 0
            status = "enterrecorder: $r · ${if (tcp) "TCP" else "UDP"}"
            Log.i(TAG, status); log(status)
            if (!is70 && retry < 2) { log("rtsp_function: " + api(net, "getparamvalue?param=rtsp_function")); log("rtsp_url: " + api(net, "getparamvalue?param=rtsp_url")); log("stream: " + api(net, "getparamvalue?param=stream_info")) }
            if (!is70) try { Thread.sleep(1500) } catch (_: InterruptedException) {}
            val url = if (is70) "rtsp://$HOST:554" + paths70[(retry / 2) % paths70.size] else "rtsp://$HOST:554"
            log((if (is70) "70mai" else "cam gương") + " thu $url ${if (tcp) "TCP" else "UDP"} (VLC)")
            if (!want || player != null) return@post

            // libVLC: it can keo luong RTSP cam re hon ExoPlayer
            val args = arrayListOf(
                "--no-audio",
                "--rtsp-timeout=5",
                "--network-caching=250",
                "--live-caching=250",
                "--no-stats",
                "--quiet",
                if (tcp) "--rtsp-tcp" else "--no-rtsp-tcp",
            )
            val lib = LibVLC(ctx, args)
            val p = MediaPlayer(lib)
            val media = Media(lib, Uri.parse(url))
            media.setHWDecoderEnabled(true, true)   // uu tien giai ma phan cung, cho fallback phan mem
            media.addOption(":network-caching=250")
            media.addOption(if (tcp) ":rtsp-tcp" else ":no-rtsp-tcp")
            p.media = media
            media.release()

            p.setEventListener { e ->
                when (e.type) {
                    MediaPlayer.Event.Opening -> log("VLC mo luong")
                    MediaPlayer.Event.Buffering -> if (e.buffering >= 100f) log("VLC nap xong 100%")
                    MediaPlayer.Event.Playing -> {
                        status = "đang phát ${if (tcp) "TCP" else "UDP"} $url"; log("VLC Playing · $status")
                    }
                    MediaPlayer.Event.Vout -> {
                        log("VLC Vout x${e.voutCount}")
                        if (e.voutCount > 0) fitTrack(p)
                    }
                    MediaPlayer.Event.EncounteredError -> {
                        status = "lỗi VLC ${if (tcp) "TCP" else "UDP"} $url"; log(status)
                        onErr("${if (is70) "Cam 70mai" else "Cam gương"} ${if (tcp) "TCP" else "UDP"}: VLC báo lỗi luồng")
                        stopPlayer(); retryLater()
                    }
                    MediaPlayer.Event.EndReached -> { log("VLC het luong"); stopPlayer(); retryLater() }
                }
            }

            vlc = lib
            player = p
            attachReader(p)
            p.play()
            gotFrame = false
            // canh giu: 10 s khong co khung hinh nao thi bo, thu lai bang kieu truyen kia
            h.postDelayed({
                if (player === p && !gotFrame) {
                    status = "không có hình sau 10 s (${if (tcp) "TCP" else "UDP"})"; log(status); onErr("Cam: $status")
                    stopPlayer(); retryLater()
                }
            }, 10_000)
        }
    }

    /** Khi VLC bao co luong hinh: lay kich thuoc that de ImageReader dung ti le, tranh meo hinh. */
    private fun fitTrack(p: MediaPlayer) {
        val t = try { p.currentVideoTrack } catch (_: Exception) { null } ?: return
        val w = t.width; val hh = t.height
        if (w < 64 || hh < 64 || (w == sz.width && hh == sz.height)) return
        sz = Size(w, hh)
        log("kich thuoc hinh ${w}x$hh")
        attachReader(p)
        onSize()
    }

    private fun attachReader(p: MediaPlayer) {
        val old = reader
        val rd = ImageReader.newInstance(sz.width, sz.height, ImageFormat.YUV_420_888, 3)
        rd.setOnImageAvailableListener({ ir ->
            val im = try { ir.acquireLatestImage() } catch (_: Exception) { null } ?: return@setOnImageAvailableListener
            frames++; gotFrame = true
            if (!gotFrameLogged) { gotFrameLogged = true; log("khung hinh dau tien ${im.width}x${im.height}"); onErr("") }
            val now = android.os.SystemClock.elapsedRealtime()
            if (now - frAt > 10_000) { log("nhan $frames khung/10 s · ${im.width}x${im.height}"); frames = 0; frAt = now }
            onFrame(im)     // onFrame tu dong close()
        }, h)
        reader = rd
        val vout = p.vlcVout
        if (attached) { try { vout.detachViews() } catch (_: Exception) {}; attached = false }
        vout.setVideoSurface(rd.surface, null)
        vout.setWindowSize(sz.width, sz.height)
        vout.attachViews()
        attached = true
        // KHONG dong reader cu ngay: bo giai ma co the dang doi surface -> dong cham mot nhip
        if (old != null) h.postDelayed({ try { old.close() } catch (_: Exception) {} }, 3000)
    }

    @Volatile private var gotFrameLogged = false

    private fun retryLater() {
        if (!want) return
        retry++
        h.postDelayed({ if (want && player == null) open() }, minOf(15_000L, 2000L * retry))
    }

    private fun stopPlayer() {
        gotFrameLogged = false
        try {
            player?.let { p ->
                p.setEventListener(null)
                if (attached) { try { p.vlcVout.detachViews() } catch (_: Exception) {} }
                p.stop()
                p.release()
            }
        } catch (_: Exception) {}
        attached = false
        try { vlc?.release() } catch (_: Exception) {}
        player = null; vlc = null
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

    companion object {
        private const val TAG = "MirrorCam"
    }
}
