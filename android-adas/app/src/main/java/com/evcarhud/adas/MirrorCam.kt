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
    // dau vet su kien VLC (hien len man hinh, khong can log may chu)
    @Volatile private var evOpen = false
    @Volatile private var evPlay = false
    @Volatile private var evBufMax = 0
    @Volatile private var evVout = 0
    @Volatile private var evErr = ""
    private fun traceStr(tcp: Boolean) =
        "VLC[${if (tcp) "TCP" else "UDP"} O=${if (evOpen) 1 else 0} buf=$evBufMax% P=${if (evPlay) 1 else 0} Vout=$evVout khung=$frames]$evErr"
    private fun showTrace(tcp: Boolean) { status = traceStr(tcp); onErr("Cam: ${traceStr(tcp)}") }

    @Volatile private var bound = false
    private fun bindNet(net: Network) {
        try {
            val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            cm.bindProcessToNetwork(net); bound = true; log("bám mạng Wi-Fi cam")
        } catch (e: Exception) { log("bind net lỗi ${e.javaClass.simpleName}") }
    }
    private fun unbindNet() {
        if (!bound) return
        try { (ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).bindProcessToNetwork(null) } catch (_: Exception) {}
        bound = false
    }

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

    // Giu phien song: 70mai = client.cgi register; cam guong = goi getparamvalue lap lai (cam ngung day RTP
    // "VLC het luong" khi khong co client hoi tham dinh ky -> app hang giu phien lien tuc).
    private var keep: Thread? = null
    private fun startKeep(net: Network) {
        if (keep?.isAlive == true) return
        keep = Thread {
            var n = 0
            while (want) {
                val r = if (is70) hisnet(net, "client.cgi?&-operation=register&-ip=${myIp(net)}")
                        else api(net, "getparamvalue?param=rec")
                if (n++ < 2) log("${if (is70) "70mai register" else "cam gương giữ phiên"}: $r")
                try { Thread.sleep(if (is70) 3000 else 1500) } catch (_: InterruptedException) { break }
            }
        }.apply { isDaemon = true; start() }
    }

    override fun open() {
        h.post {
            want = true
            if (player != null) return@post
            val net = wifi()
            if (net == null) { status = "chưa nối Wi-Fi cam"; onErr(if (is70) "Chưa nối Wi-Fi cam 70mai (70mai_…)" else "Chưa nối Wi-Fi cam gương (mirror_wifi…)"); retryLater(); return@post }
            // VLC/live555 mo socket UDP tren MANG MAC DINH (4G neu Wi-Fi cam khong co mang) -> goi RTP cam gui toi
            // IP wlan0 khong vao duoc socket do -> buf=0%. Ep ca tien trinh bam mang Wi-Fi cam khi dang xem.
            bindNet(net)
            val r = if (is70) {
                if (retry == 0) log("70mai thong tin: " + hisnet(net, "getdeviceattr.cgi"))
                log("70mai register: " + hisnet(net, "client.cgi?&-operation=register&-ip=${myIp(net)}"))
                startKeep(net)
                try { Thread.sleep(800) } catch (_: InterruptedException) {}
                "70mai"
            } else { val rr = api(net, "enterrecorder"); startKeep(net); rr }
            // App hang (Sinjet, mpv/ffmpeg) chi goi enterrecorder roi mo rtsp://192.168.5.1:554, KHONG path,
            // va ffmpeg mac dinh RTSP = UDP. 0.1.65 ep TCP truoc -> tat. Nay cam guong UU TIEN UDP (giong mpv),
            // xen ke sang TCP neu UDP khong duoc. 70mai van TCP truoc.
            // 20/11: sau khi bam mang Wi-Fi cam, TCP vao duoc buf=75% + Vout=1 (UDP chi 45%, Vout=0)
            // -> cam guong UU TIEN TCP. Van xen ke UDP de phong.
            val tcp = retry % 2 == 0
            status = "enterrecorder: $r · ${if (tcp) "TCP" else "UDP"}"
            Log.i(TAG, status); log(status)
            if (!is70) try { Thread.sleep(1200) } catch (_: InterruptedException) {}
            // cam guong: URL tran nhu app hang; 70mai xoay vong duong luong
            val url = "rtsp://$HOST:554" + (if (is70) paths70[(retry / 2) % paths70.size] else "")
            log((if (is70) "70mai" else "cam gương") + " thu $url ${if (tcp) "TCP" else "UDP"} (VLC)")
            if (!want || player != null) return@post

            // libVLC: it can keo luong RTSP cam re hon ExoPlayer.
            // 0.1.71: buf=100% nhung Vout=0 khung=0 -> bo giai ma PHAN CUNG (MediaCodec) khong nhai duoc
            // luong cam dam cam re (profile/chroma la). App hang dung ffmpeg giai ma PHAN MEM (libavcodec).
            // Ep VLC giai ma phan mem (chinh libavcodec ma app hang dung) + xuat chroma mem (I420).
            val args = arrayListOf(
                "--no-audio",
                "--rtsp-timeout=5",
                "--network-caching=250",
                "--live-caching=250",
                "--no-stats",
                "--quiet",
                "--avcodec-hw=none",          // tat giai ma phan cung -> dung libavcodec phan mem
                "--codec=avcodec",            // ep bo giai ma ffmpeg (giong app hang)
                if (tcp) "--rtsp-tcp" else "--no-rtsp-tcp",
            )
            val lib = LibVLC(ctx, args)
            val p = MediaPlayer(lib)
            val media = Media(lib, Uri.parse(url))
            media.setHWDecoderEnabled(false, false)  // giai ma phan mem (libavcodec) nhu app hang; HW tra Vout=0
            media.addOption(":network-caching=250")
            media.addOption(if (tcp) ":rtsp-tcp" else ":no-rtsp-tcp")
            p.media = media
            media.release()

            // dau vet hien thang len man hinh (khong phu thuoc log may chu, vi Wi-Fi cam khong co mang)
            evOpen = false; evPlay = false; evBufMax = 0; evVout = 0; evErr = ""
            p.setEventListener { e ->
                when (e.type) {
                    MediaPlayer.Event.Opening -> { evOpen = true; log("VLC mo luong"); showTrace(tcp) }
                    MediaPlayer.Event.Buffering -> { val b = e.buffering.toInt(); if (b > evBufMax) { evBufMax = b; showTrace(tcp) }; if (e.buffering >= 100f) log("VLC nap xong 100%") }
                    MediaPlayer.Event.Playing -> {
                        evPlay = true; status = "đang phát ${if (tcp) "TCP" else "UDP"} $url"; log("VLC Playing · $status"); showTrace(tcp)
                    }
                    MediaPlayer.Event.Vout -> {
                        evVout = e.voutCount; log("VLC Vout x${e.voutCount}"); showTrace(tcp)
                        if (e.voutCount > 0) fitTrack(p)
                    }
                    MediaPlayer.Event.EncounteredError -> {
                        evErr = "E"; status = "lỗi VLC ${if (tcp) "TCP" else "UDP"} $url"; log(status)
                        onErr("Cam ${if (tcp) "TCP" else "UDP"}: VLC lỗi luồng · ${traceStr(tcp)}")
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
            // canh giu: 12 s khong co khung hinh nao thi bo, thu lai bang kieu truyen kia
            h.postDelayed({
                if (player === p && !gotFrame) {
                    status = "không có hình sau 12 s · ${traceStr(tcp)}"; log("không có hình 12 s · $url ${traceStr(tcp)}"); onErr("Cam: $status")
                    stopPlayer(); retryLater()
                }
            }, 12_000)
        }
    }

    /** Khi VLC bao co luong hinh: CHI ghi lai kich thuoc that (de log), KHONG tao lai ImageReader.
     * Doi surface giua chung lam MediaCodec cua VLC mat khung (Vout=1 nhung khung=0, 20/11). */
    private fun fitTrack(p: MediaPlayer) {
        val t = try { p.currentVideoTrack } catch (_: Exception) { null } ?: return
        if (t.width >= 64 && t.height >= 64) log("kich thuoc that ${t.width}x${t.height} (VLC thu nho ve ${sz.width}x${sz.height})")
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
            unbindNet()           // tra mang mac dinh (4G) cho phan con lai cua app
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
