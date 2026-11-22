package com.evcarhud.adas

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.SystemClock
import android.util.Log
import android.util.Size
import android.view.Surface
import org.bytedeco.ffmpeg.global.avutil
import org.bytedeco.ffmpeg.global.swscale
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
    /** true = 1080p (net hon, nong + tre hon), false = 720p (mat, muot, do tre). AI tu thu nho nen 720p khong giam do chinh xac. */
    private val hd: Boolean = false,
) : FrameSource {
    private val is70 = kind == "70mai"
    private val HOST = if (is70) "192.168.0.1" else "192.168.5.1"
    private val paths70 = listOf("/livestream/12", "/livestream/11", "/livestream/1", "/live")

    // Nguon cam 4K -> ffmpeg scale ve do phan giai chon: 1080p net / 720p mat + do tre + nhe giai ma.
    private val W = if (hd) 1920 else 1280
    private val H = if (hd) 1080 else 720

    @Volatile private var want = false
    @Volatile private var grabber: FFmpegFrameGrabber? = null
    private var worker: Thread? = null
    private var retry = 0
    @Volatile private var frames = 0
    @Volatile private var gotFrame = false
    @Volatile private var lastFrameAt = 0L      // lan cuoi grabImage ra khung (watchdog đứng hình)
    // 70mai: nho luong (udp/path) da ra khung de uu tien, khoi do lai tu dau moi lan noi lai
    @Volatile private var goodUdp: Boolean? = null
    @Volatile private var goodPath = -1
    private var goodFail = 0
    private var curUdp = false
    private var curPath = 0
    private var watch: Thread? = null
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

    // Cam Wi-Fi KHONG co internet -> he dieu hanh de luong mac dinh ra 5G, socket native cua ffmpeg
    // khong ra duoc cam (-101 ENETUNREACH) du da bindProcessToNetwork. Phai XIN rieng mang Wi-Fi
    // KHONG yeu cau NET_CAPABILITY_INTERNET (requestNetwork), cho onAvailable, roi bam ca tien trinh
    // vao mang do va GIU callback (buong callback = mat mang). Bam lai ngay truoc khi mo ffmpeg.
    private val cm get() = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    @Volatile private var camNet: Network? = null
    @Volatile private var bound = false
    private var netCb: ConnectivityManager.NetworkCallback? = null

    /** Xin mang Wi-Fi cam (khong internet), cho co mang <=timeoutMs, bam tien trinh. Tra Network hoac null. */
    private fun acquireCamNet(timeoutMs: Long): Network? {
        camNet?.let { return it }
        val req = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
            .build()
        val lock = Object()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(n: Network) {
                synchronized(lock) {
                    camNet = n
                    try { cm.bindProcessToNetwork(n); bound = true; log("bám mạng Wi-Fi cam (requestNetwork)") }
                    catch (e: Exception) { log("bind net lỗi ${e.javaClass.simpleName}") }
                    lock.notifyAll()
                }
            }
            override fun onLost(n: Network) {
                synchronized(lock) { if (camNet == n) { camNet = null; bound = false } }
            }
        }
        try {
            cm.requestNetwork(req, cb)
            netCb = cb
        } catch (e: Exception) { log("requestNetwork lỗi ${e.javaClass.simpleName}"); return null }
        synchronized(lock) {
            val end = SystemClock.elapsedRealtime() + timeoutMs
            while (camNet == null && SystemClock.elapsedRealtime() < end) {
                try { lock.wait(end - SystemClock.elapsedRealtime()) } catch (_: InterruptedException) { break }
            }
        }
        return camNet
    }

    /** Bam lai tien trinh vao mang cam (goi ngay truoc khi mo ffmpeg). */
    private fun rebind() {
        camNet?.let { try { cm.bindProcessToNetwork(it); bound = true } catch (_: Exception) {} }
    }

    private fun releaseCamNet() {
        netCb?.let { try { cm.unregisterNetworkCallback(it) } catch (_: Exception) {} }
        netCb = null
        if (bound) { try { cm.bindProcessToNetwork(null) } catch (_: Exception) {} }
        bound = false; camNet = null
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

    // Dashcam cam guong co 2 mat: switchcam(0/1) chon mat SAU / mat TRUOC. Mac dinh cam phat mat SAU;
    // ADAS can mat TRUOC (nhin duong). Doc gia tri hien tai (= cam sau), dao sang gia tri con lai =
    // cam truoc, giu co dinh moi vong (khong doc-dao lai keo lat nguoc). Mac dinh doan 1 neu doc loi.
    @Volatile private var frontVal: String? = null
    @Volatile private var camLogged = false
    private fun selectFrontCam(net: Network) {
        if (frontVal == null) {
            val cur = api(net, "getparamvalue?param=switchcam")       // "<code> <body>"
            val body = cur.substringAfter(' ', cur)
            val after = if (body.contains("switchcam")) body.substringAfter("switchcam") else body
            val rear = after.firstOrNull { it == '0' || it == '1' }?.toString()
            frontVal = when (rear) { "0" -> "1"; "1" -> "0"; else -> "1" }
            log("switchcam đọc='$cur' → cam trước=$frontVal")
        }
        val r = api(net, "setparamvalue?param=switchcam&value=$frontVal")
        if (!camLogged) { camLogged = true; log("đặt cam trước ($frontVal): $r") }
    }

    private var keep: Thread? = null
    private fun startKeep(net: Network) {
        if (keep?.isAlive == true) return
        keep = Thread {
            var n = 0
            while (want) {
                // 70mai: GIU am ket noi bang GET vo hai. KHONG re-register dinh ky — register lai lam cam
                // RESET luong (avformat mo duoc 1920x1080 nhung sau do khung=0). Da register 1 lan luc mo.
                val r = if (is70) hisnet(net, "getdeviceattr.cgi")
                        else api(net, "getparamvalue?param=rec")
                if (n++ < 2) log("${if (is70) "70mai giữ kết nối" else "cam gương giữ phiên"}: $r")
                try { Thread.sleep(if (is70) 3000 else 1500) } catch (_: InterruptedException) { break }
            }
        }.apply { isDaemon = true; start() }
    }

    override fun open() {
        if (worker?.isAlive == true) { want = true; return }
        want = true
        worker = Thread { runLoop() }.apply { isDaemon = true; name = "mirror-ffmpeg"; start() }
        watch = Thread { watchLoop() }.apply { isDaemon = true; name = "mirror-watch"; start() }
    }

    /** Canh luong đứng hình: cam gương hay treo (mat goi I-frame, UDP ngăt) nhung khong bao loi ->
     *  grabImage ket treo mai. Neu >4s khong co khung moi (da tung co khung) thi stop grabber de
     *  pha ket, vong lap bat loi -> noi lai luong. */
    private fun watchLoop() {
        // 70mai hay giat/treo hon cam guong -> bat đứng hình SOM hon (2,5s) de noi lai muot; cam guong giu 4s.
        val froze = if (is70) 2500L else 4000L
        while (want) {
            sleep(800)
            val last = lastFrameAt
            if (gotFrame && last > 0 && SystemClock.elapsedRealtime() - last > froze) {
                log("đứng hình >${froze / 1000.0}s → mở lại luồng")
                lastFrameAt = SystemClock.elapsedRealtime()      // tranh goi stop lien tuc
                try { grabber?.stop() } catch (_: Exception) {}   // pha grabImage dang ket
            }
        }
    }

    private fun runLoop() {
        avutil.av_log_set_level(avutil.AV_LOG_ERROR)
        while (want) {
            val net = acquireCamNet(6000)
            if (net == null) {
                status = "chưa nối Wi-Fi cam"
                onErr(if (is70) "Chưa nối Wi-Fi cam 70mai (70mai_…)" else "Chưa nối Wi-Fi cam gương (mirror_wifi…)")
                sleep(2000); retry++; continue
            }
            // cam guong chi 1 phien RTSP -> reset phien (exit roi enter) de lan nao cung co du lieu
            val info = if (is70) {
                if (retry == 0) log("70mai: " + hisnet(net, "getdeviceattr.cgi"))
                // Chi dang ky (nhu 0.1.86 chay OK). KHONG unregister: no lam cam ngung day hinh (khung=0).
                hisnet(net, "client.cgi?&-operation=register&-ip=${myIp(net)}")
            } else {
                api(net, "exitrecorder"); sleep(400)
                selectFrontCam(net)                 // dashcam 2 mat: chon cam TRUOC (mat duong)
                api(net, "enterrecorder")
            }
            startKeep(net)
            log("${if (is70) "70mai" else "cam gương"} mở: $info")

            // REALTIME: dung UDP (bo goi cu -> tre co dinh thap, giong app hang). TCP khong mat goi nhung
            // neu giai ma khong kip thi don lai -> tre tang dan toi vai PHUT, va 70mai TCP chay ~6s roi
            // DUNG HAN (log 22/11: lap mo-treo moi 16s). 70mai CHI chay UDP /livestream/12 that su (da xac
            // nhan chay muot 7 phut o 0.1.95). => 70mai ep UDP + /12 luon; cam guong uu tien UDP (2/3 lan),
            // thinh thoang thu TCP phong UDP bi chan.
            val udp = if (is70) true else (retry % 3 != 2)
            val pathIdx = 0                                   // 70mai: /livestream/12 (duy nhat chay)
            curUdp = udp; curPath = pathIdx
            val url = "rtsp://$HOST:554" + (if (is70) paths70[pathIdx] else "")
            status = "mở ffmpeg ${if (udp) "UDP" else "TCP"} $url"
            log(status); onErr("Cam: " + traceStr(udp))

            var g: FFmpegFrameGrabber? = null
            try {
                rebind()   // bam lai mang cam ngay truoc khi ffmpeg mo socket (tranh -101 ra 5G)
                g = FFmpegFrameGrabber(url).apply {
                    format = "rtsp"
                    setOption("rtsp_transport", if (udp) "udp" else "tcp")
                    // KHONG set rtsp_flags="" (rong) -> avformat_open_input tra -22 EINVAL. Chi set khi co gia tri.
                    setOption("timeout", "8000000")       // timeout socket (us) cho rtsp (ffmpeg 6)
                    setOption("buffer_size", "1048576")
                    setOption("fflags", "nobuffer")       // khong don khung -> khong tre dan (realtime)
                    setOption("flags", "low_delay")
                    if (is70) {
                        // 70mai: giu cau hinh ON DINH nhu 0.1.95 (da chay muot 7 phut). Cac option giam tre
                        // ganh (max_delay=0/reorder=0/rtbufsize nho) lam luong UDP 70mai RUNG/RƠT -> khong dung.
                        setOption("max_delay", "100000")  // 0.1s
                    } else {
                        // Cam guong (Sinjet) trễ ~2s LUC CHAY: ep realtime manh de bot tre dong.
                        setOption("max_delay", "0")           // khong cho tre do sap xep goi
                        setOption("reorder_queue_size", "0")  // UDP: khong cho sap xep lai goi -> bot tre
                        setOption("rtbufsize", "256000")      // bo dem thoi gian thuc nho
                        setOption("probesize", "500000")      // mo luong nhanh hon (mac dinh 5MB)
                        setOption("analyzeduration", "500000") // phan tich toi da 0.5s truoc khi ra khung
                    }
                    imageMode = FrameGrabber.ImageMode.COLOR
                    // Lay thang YUV420P (I420) tu ffmpeg -> KHONG phai doi mau BGR->YUV tung diem tren Kotlin
                    // (vong lap 2M diem/khung = nang, tre + nong). Chi can chep khoi -> nhanh hon nhieu.
                    pixelFormat = avutil.AV_PIX_FMT_YUV420P
                    imageWidth = W                        // ffmpeg tu scale ve 1920x1080 (nguon co the 4K)
                    imageHeight = H
                    imageScalingFlags = swscale.SWS_BICUBIC   // co anh net hon mac dinh (bilinear)
                }
                g.start()
                grabber = g
                log("ffmpeg mở luồng OK ${g.imageWidth}x${g.imageHeight} codec=${try { g.videoCodecName } catch (_: Exception) { "?" }} fps=${try { "%.0f".format(g.frameRate) } catch (_: Exception) { "?" }}")
                gotFrame = false
                lastFrameAt = 0L
                var lastDeliver = 0L
                var failN = 0                                 // so lan grabImage loi/null LIEN TIEP
                val startAt = SystemClock.elapsedRealtime()
                while (want) {
                    val f: Frame? = try { g.grabImage() } catch (e: Exception) { if (failN < 2) log("grab lỗi ${e.message?.take(80)}"); null }
                    if (f == null) {
                        failN++
                        // 70mai: neu mo luong duoc ma 5 s khong co khung thi chuyen luong khac cho nhanh (dò /12→/11→/1→/live)
                        if (!gotFrame && SystemClock.elapsedRealtime() - startAt > (if (is70) 5_000L else 12_000L)) {
                            log("không có khung ${if (is70) 5 else 12} s (${if (udp) "UDP" else "TCP"} $url)")
                            if (is70 && goodPath >= 0 && ++goodFail >= 2) { goodPath = -1; goodFail = 0; log("bỏ nhớ luồng, dò lại từ đầu") }
                            break
                        }
                        // TREO: luong đã từng có khung rồi grabImage lỗi liên tục (watchLoop stop grabber để phá
                        // đứng hình). Trước đây vòng lặp quay vòng mãi "No AVFormatContext" (log 1840 lần) KHÔNG nối
                        // lại. Giờ thoát ra để mở lại luồng (backoff nhanh vì gotFrame=true).
                        if (gotFrame && failN >= 10) { log("luồng lỗi liên tục sau khi treo → nối lại"); break }
                        sleep(5)                                 // tránh quay vòng rát CPU khi grab lỗi liên tục
                        continue
                    }
                    failN = 0
                    val now = SystemClock.elapsedRealtime()
                    lastFrameAt = now                           // luong con song (watchdog theo doi)
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
            if (want) {
                retry++
                // Luong vua roi DA tung ra khung (chi giat/treo) -> noi lai NHANH cho muot, nhat la 70mai
                // (da nho goodPath nen mo thang luong cu). Chua bao gio ra khung -> giai do dan tranh dap cam.
                val quick = gotFrame
                sleep(if (quick) (if (is70) 500L else 900L) else minOf(8000L, 1500L * retry))
            }
        }
        keep?.interrupt(); keep = null
        if (!is70) camNet?.let { n -> try { api(n, "exitrecorder") } catch (_: Exception) {} }
        releaseCamNet()
        status = "đã tắt"
    }

    /** ffmpeg xuat YUV420P. Tuy build JavaCV: Y/U/V co the o 3 BUFFER RIENG (image[0],[1],[2])
     *  HOAC LIEN KHOI trong 1 buffer image[0] (limit co the bi dat = W*H). Xu ly ca 2 kieu de
     *  khong vut khung (loi 0.1.88: ep 3 buffer, image[1]/[2] null -> deliver thoat som -> den man).
     *  Chep tung mat phang sang bo dem (stride chuan W) -> YuvFrame -> onFrame. Khong doi mau tung diem. */
    private fun deliver(f: Frame) {
        val img = f.image ?: return
        val yb = (img.getOrNull(0) as? ByteBuffer) ?: return
        if (f.imageWidth != W || f.imageHeight != H) return
        val yStride = f.imageStride                      // stride plane Y (>= W do can le)
        val cStride = yStride / 2                         // YUV420P: stride U/V = 1/2 plane Y
        val cw = W / 2; val ch = H / 2
        val buf = i420[bufI]; bufI = bufI xor 1
        buf.clear()
        val uStart = W * H
        val vStart = W * H + W * H / 4
        val ub = img.getOrNull(1) as? ByteBuffer
        val vb = img.getOrNull(2) as? ByteBuffer
        if (!layoutLogged) {
            layoutLogged = true
            log("YUV layout: planes=${img.size} y.lim=${yb.limit()} y.cap=${yb.capacity()} stride=$yStride" +
                (if (ub != null) " u.lim=${ub.limit()} u.cap=${ub.capacity()}" else " u=null") +
                (if (vb != null) " v.lim=${vb.limit()}" else " v=null"))
        }
        if (ub != null && vb != null) {                  // 3 buffer rieng
            copyPlane(yb, 0, yStride, buf, 0, W, W, H)
            copyPlane(ub, 0, cStride, buf, uStart, cw, cw, ch)
            copyPlane(vb, 0, cStride, buf, vStart, cw, cw, ch)
        } else {                                         // lien khoi trong image[0]: doc theo capacity
            val src = yb.duplicate(); src.position(0); src.limit(yb.capacity())
            val uOff = yStride * H                        // U bat dau sau plane Y
            val vOff = uOff + cStride * ch                // V sau plane U
            copyPlane(src, 0, yStride, buf, 0, W, W, H)
            copyPlane(src, uOff, cStride, buf, uStart, cw, cw, ch)
            copyPlane(src, vOff, cStride, buf, vStart, cw, cw, ch)
        }
        frames++; gotFrame = true
        if (is70 && goodPath < 0) { goodPath = curPath; goodUdp = curUdp; goodFail = 0; log("nhớ luồng chạy được: ${if (curUdp) "UDP" else "TCP"} ${paths70[curPath]}") }
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

    /** Chep 1 mat phang: tu src (base, srcStride) sang dst (dstStart, dstStride=rowW) rong rowW cao rows.
     *  Chong tran: neu stride nguon la ma le khac du doan thi chep tung hang va bo qua hang vuot gioi han. */
    private fun copyPlane(src: ByteBuffer, base: Int, srcStride: Int, dst: ByteBuffer, dstStart: Int, rowW: Int, dstStride: Int, rows: Int) {
        val cap = src.capacity()
        if (srcStride == dstStride && base + srcStride * rows <= cap) {   // lien tuc + vua -> chep 1 phat
            val s = src.duplicate(); s.position(base); s.limit(base + srcStride * rows)
            val d = dst.duplicate(); d.position(dstStart); d.put(s)
            return
        }
        for (j in 0 until rows) {                            // co dem le: chep tung hang
            val sp = base + j * srcStride
            if (sp + rowW > cap) break                       // het du lieu nguon -> dung
            val s = src.duplicate(); s.position(sp); s.limit(sp + rowW)
            val d = dst.duplicate(); d.position(dstStart + j * dstStride); d.put(s)
        }
    }

    @Volatile private var gotFrameLogged = false
    @Volatile private var layoutLogged = false

    private fun sleep(ms: Long) { try { Thread.sleep(ms) } catch (_: InterruptedException) {} }

    override fun close() {
        want = false
        val g = grabber
        grabber = null
        try { g?.stop() } catch (_: Exception) {}   // mo khoa grabImage dang cho
        keep?.interrupt()
        watch?.interrupt()
        worker?.interrupt()
    }

    override fun setRate(hz: Int) {}
    override fun setPreview(s: Surface?) {}
    override fun release() { close() }

    companion object {
        private const val TAG = "MirrorCam"
    }
}
