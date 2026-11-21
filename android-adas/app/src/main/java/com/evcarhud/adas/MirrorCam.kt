package com.evcarhud.adas

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.SystemClock
import android.util.Log
import android.util.Size
import android.view.Surface
import org.bytedeco.ffmpeg.global.avutil
import org.bytedeco.javacv.FFmpegFrameGrabber
import org.bytedeco.javacv.Frame
import org.bytedeco.javacv.FrameGrabber
import java.net.HttpURLConnection
import java.net.URL
import java.nio.ByteBuffer

/**
 * Cam guong Wi-Fi (app hang "rearview_wifi", Sinjet): API http://192.168.5.1:8000/app/...,
 * goi app/enterrecorder thi cam mo RTSP rtsp://192.168.5.1:554 (KHONG path).
 * 70mai: rtsp://192.168.0.1:554/livestream/12 sau khi dang ky client.cgi.
 *
 * 20/11: VLC KHONG giai ma noi luong cam nay tren chip may (Vout=0 ca giai ma phan cung lan phan mem,
 * du da co du lieu buf=45-100%). Chuyen sang **ffmpeg truc tiep (JavaCV FFmpegFrameGrabber)** — DUNG
 * engine app hang (media_kit = mpv/ffmpeg) chay UDP on dinh. ffmpeg giai ma -> BGR24 1280x720 ->
 * doi sang YUV (I420) -> cung duong onFrame(YuvFrame) nhu camera dien thoai.
 * Goc nhin: 3 clip 4K 17/11 khop f ~ 520 px o ngang 1280.
 */
class MirrorCam(
    private val ctx: Context,
    private val onFrame: (YuvFrame) -> Unit,
    private val onErr: (String) -> Unit,
    private val onSize: () -> Unit,
    private val log: (String) -> Unit = {},
    /** "mirror" = cam guong Sinjet (goi API :8000 truoc), "70mai" = cam 70mai (RTSP /livestream/12, 192.168.0.1). */
    private val kind: String = "mirror",
) : FrameSource {
    private val is70 = kind == "70mai"
    private val HOST = if (is70) "192.168.0.1" else "192.168.5.1"
    private val paths70 = listOf("/livestream/12", "/livestream/11", "/livestream/1", "/live")

    private val W = 1280
    private val H = 720

    @Volatile private var want = false
    @Volatile private var grabber: FFmpegFrameGrabber? = null
    private var worker: Thread? = null
    private var retry = 0
    @Volatile private var frames = 0
    @Volatile private var gotFrame = false
    private var frAt = 0L

    // 2 bo dem I420 luan phien (buffer chi can hop le trong luc goi onFrame dong bo)
    private val i420 = arrayOf(
        ByteBuffer.allocateDirect(W * H * 3 / 2),
        ByteBuffer.allocateDirect(W * H * 3 / 2),
    )
    private var bufI = 0

    override val size: Size get() = Size(W, H)
    override val focalPx: Double get() = (if (is70) 450.0 else 520.0) * W / 1280.0
    override val focalSrc = if (is70) "cam 70mai Wi-Fi" else "cam gương Wi-Fi"
    override val flip = false
    override val isOpen get() = want
    @Volatile var status = "chưa mở"

    private fun traceStr(udp: Boolean) = "FFMPEG[${if (udp) "UDP" else "TCP"} khung=$frames ${W}x$H]"

    @Volatile private var bound = false
    private fun bindNet(net: Network) {
        try {
            (ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).bindProcessToNetwork(net)
            bound = true; log("bám mạng Wi-Fi cam")
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
        if (worker?.isAlive == true) { want = true; return }
        want = true
        worker = Thread { runLoop() }.apply { isDaemon = true; name = "mirror-ffmpeg"; start() }
    }

    private fun runLoop() {
        avutil.av_log_set_level(avutil.AV_LOG_ERROR)
        while (want) {
            val net = wifi()
            if (net == null) {
                status = "chưa nối Wi-Fi cam"
                onErr(if (is70) "Chưa nối Wi-Fi cam 70mai (70mai_…)" else "Chưa nối Wi-Fi cam gương (mirror_wifi…)")
                sleep(2000); retry++; continue
            }
            bindNet(net)
            // cam guong chi 1 phien RTSP -> reset phien (exit roi enter) de lan nao cung co du lieu
            val info = if (is70) {
                if (retry == 0) log("70mai: " + hisnet(net, "getdeviceattr.cgi"))
                hisnet(net, "client.cgi?&-operation=register&-ip=${myIp(net)}")
            } else {
                api(net, "exitrecorder"); sleep(400); api(net, "enterrecorder")
            }
            startKeep(net)
            log("${if (is70) "70mai" else "cam gương"} mở: $info")

            // app hang (mpv/ffmpeg) mac dinh RTSP = UDP. Cam guong thu UDP truoc (giong hang), xen ke TCP.
            // 70mai thu TCP truoc.
            val udp = if (is70) retry % 2 == 1 else retry % 2 == 0
            val url = "rtsp://$HOST:554" + (if (is70) paths70[(retry / 2) % paths70.size] else "")
            status = "mở ffmpeg ${if (udp) "UDP" else "TCP"} $url"
            log(status); onErr("Cam: " + traceStr(udp))

            var g: FFmpegFrameGrabber? = null
            try {
                g = FFmpegFrameGrabber(url).apply {
                    format = "rtsp"
                    setOption("rtsp_transport", if (udp) "udp" else "tcp")
                    // KHONG set rtsp_flags="" (rong) -> avformat_open_input tra -22 EINVAL. Chi set khi co gia tri.
                    setOption("timeout", "8000000")       // timeout socket (us) cho rtsp (ffmpeg 6)
                    setOption("max_delay", "500000")
                    setOption("buffer_size", "1048576")
                    setOption("fflags", "nobuffer")
                    imageMode = FrameGrabber.ImageMode.COLOR
                    pixelFormat = avutil.AV_PIX_FMT_BGR24
                    imageWidth = W                        // ffmpeg tu scale ve 1280x720 (nguon co the 4K)
                    imageHeight = H
                }
                g.start()
                grabber = g
                log("ffmpeg mở luồng OK ${g.imageWidth}x${g.imageHeight}")
                gotFrame = false
                var lastDeliver = 0L
                val startAt = SystemClock.elapsedRealtime()
                while (want) {
                    val f: Frame? = try { g.grabImage() } catch (e: Exception) { log("grab lỗi ${e.message?.take(80)}"); null }
                    if (f == null) {
                        if (!gotFrame && SystemClock.elapsedRealtime() - startAt > 12_000) { log("không có khung 12 s (${if (udp) "UDP" else "TCP"})"); break }
                        continue
                    }
                    val now = SystemClock.elapsedRealtime()
                    if (now - lastDeliver < 70) continue        // ~14 fps toi da, bo bot cho nhe
                    lastDeliver = now
                    deliver(f)
                }
            } catch (e: Throwable) {
                status = "lỗi ffmpeg ${if (udp) "UDP" else "TCP"}"
                log("ffmpeg lỗi: ${e.javaClass.simpleName} ${e.message?.take(100)}")
                onErr("Cam ${if (udp) "UDP" else "TCP"}: ffmpeg lỗi · ${e.message?.take(60)}")
            } finally {
                grabber = null
                try { g?.stop() } catch (_: Exception) {}
                try { g?.release() } catch (_: Exception) {}
            }
            if (want) { retry++; sleep(minOf(8000L, 1500L * retry)) }
        }
        keep?.interrupt(); keep = null
        if (!is70) wifi()?.let { n -> try { api(n, "exitrecorder") } catch (_: Exception) {} }
        unbindNet()
        status = "đã tắt"
    }

    /** BGR24 (1280x720) -> I420 vao bo dem truc tiep -> YuvFrame -> onFrame. */
    private fun deliver(f: Frame) {
        val src = (f.image?.getOrNull(0) as? ByteBuffer) ?: return
        val stride = f.imageStride                       // byte/hang (co the > W*3 do can le)
        if (f.imageWidth != W || f.imageHeight != H) return
        val buf = i420[bufI]; bufI = bufI xor 1
        buf.clear()
        val y = buf.duplicate()
        val uStart = W * H
        val vStart = W * H + W * H / 4
        // Y
        for (j in 0 until H) {
            val row = j * stride
            var oi = j * W
            var i = row
            var x = 0
            while (x < W) {
                val b = src.get(i).toInt() and 0xff
                val gg = src.get(i + 1).toInt() and 0xff
                val r = src.get(i + 2).toInt() and 0xff
                val yy = (77 * r + 150 * gg + 29 * b) shr 8          // 0.299/0.587/0.114
                y.put(oi, yy.coerceIn(0, 255).toByte())
                i += 3; oi++; x++
            }
        }
        // U, V: lay mau 2x2 (dung diem goc tren-trai moi o)
        var ui = uStart; var vi = vStart
        var j = 0
        while (j < H) {
            val row = j * stride
            var x = 0
            while (x < W) {
                val i = row + x * 3
                val b = src.get(i).toInt() and 0xff
                val gg = src.get(i + 1).toInt() and 0xff
                val r = src.get(i + 2).toInt() and 0xff
                val u = ((-43 * r - 84 * gg + 127 * b) shr 8) + 128
                val v = ((127 * r - 106 * gg - 21 * b) shr 8) + 128
                buf.put(ui, u.coerceIn(0, 255).toByte())
                buf.put(vi, v.coerceIn(0, 255).toByte())
                ui++; vi++
                x += 2
            }
            j += 2
        }
        frames++; gotFrame = true
        if (!gotFrameLogged) { gotFrameLogged = true; log("khung hình đầu tiên ${W}x$H"); onErr("") }
        val now = SystemClock.elapsedRealtime()
        if (now - frAt > 10_000) { log("nhận $frames khung/10 s"); frames = 0; frAt = now }

        val yPlane = sliced(buf, 0, W * H)
        val uPlane = sliced(buf, uStart, W * H / 4)
        val vPlane = sliced(buf, vStart, W * H / 4)
        val fr = YuvFrame(
            W, H, SystemClock.elapsedRealtimeNanos(),
            arrayOf(
                YuvFrame.Plane(yPlane, W, 1),
                YuvFrame.Plane(uPlane, W / 2, 1),
                YuvFrame.Plane(vPlane, W / 2, 1),
            ),
        ) {}
        try { onFrame(fr) } catch (e: Throwable) { Log.w(TAG, "onFrame", e) }
    }

    private fun sliced(buf: ByteBuffer, start: Int, len: Int): ByteBuffer {
        val d = buf.duplicate(); d.position(start); d.limit(start + len); return d.slice()
    }

    @Volatile private var gotFrameLogged = false

    private fun sleep(ms: Long) { try { Thread.sleep(ms) } catch (_: InterruptedException) {} }

    override fun close() {
        want = false
        val g = grabber
        grabber = null
        try { g?.stop() } catch (_: Exception) {}   // mo khoa grabImage dang cho
        keep?.interrupt()
        worker?.interrupt()
    }

    override fun setRate(hz: Int) {}
    override fun setPreview(s: Surface?) {}
    override fun release() { close() }

    companion object {
        private const val TAG = "MirrorCam"
    }
}
