package com.evcarhud.adas

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Tu ghi log khi chay (nguoi dung lai xe khong bam duoc) va gui len evcarhud.com/api/log
 * (cung kenh voi HUD P4, luu theo MAC cua HUD dang noi, ma "v" = app-lan-...) moi 60 s.
 * Ban sao cuc bo: filesDir/log-lan.txt (giu ~2 MB gan nhat).
 */
class CloudLog(private val ctx: Context, private val ver: String) {
    private val th = HandlerThread("cloudlog").apply { start() }
    private val h = Handler(th.looper)
    private val buf = StringBuilder()
    private val boot = "%08x".format((System.currentTimeMillis() / 1000).toInt())
    private var seq = 0
    private val fmt = SimpleDateFormat("HH:mm:ss", Locale.US)
    private val file = File(ctx.filesDir, "log-lan.txt")
    /** MAC HUD dang noi (BLE); may chu can MAC goc da dang ky: thu MAC BLE, -2, -1. */
    @Volatile var hudMac = ""
    private var okMac = ""
    @Volatile var lastStatus = "chưa gửi"

    init { h.postDelayed(object : Runnable { override fun run() { flush(); h.postDelayed(this, 60_000) } }, 15_000) }

    fun line(s: String) {
        val l = "${fmt.format(Date())} $s\n"
        synchronized(buf) { if (buf.length < 200_000) buf.append(l) }
    }

    fun flushSoon() { h.post { flush() } }

    /** Cac goi cho gui (moi goi <= 40 KB, may chu nhan toi da 64 KB); giu toi da ~20 goi gan nhat. */
    private val pending = ArrayDeque<String>()

    private fun flush() {
        val body = synchronized(buf) { val b = buf.toString(); buf.setLength(0); b }
        if (body.isNotEmpty()) {
            try {
                if (file.length() > 2_000_000) file.writeText("")
                file.appendText(body)
            } catch (_: Exception) {}
            var i = 0
            while (i < body.length) {
                var j = minOf(body.length, i + 40_000)
                if (j < body.length) { val nl = body.lastIndexOf('\n', j); if (nl > i) j = nl + 1 }
                pending.addLast(body.substring(i, j)); i = j
            }
            while (pending.size > 20) pending.removeFirst()
        }
        // chua noi HUD van gui: dung MAC HUD P4 da dang ky cua chu xe (E8:F6:0A:E5:50:ED, log HUD tu 12/11)
        val macs = if (okMac.isNotEmpty()) listOf(okMac) else (candidates(hudMac) + FALLBACK_MAC).distinct()
        while (pending.isNotEmpty()) {
            val part = pending.first()
            var sent = false
            for (m in macs) {
                val st = post(m, part)
                if (st in 200..299) { okMac = m; seq++; sent = true; lastStatus = "đã gửi lúc ${fmt.format(Date())}"; short = "✓"; break }
                if (st == 413) { sent = true; lastStatus = "bỏ 1 gói quá dài"; break }
                if (st != 403) { lastStatus = "lỗi gửi $st $lastErr lúc ${fmt.format(Date())}"; short = "✗ " + (if (st < 0) lastErr else "$st"); return }
                lastStatus = "máy chủ chưa nhận MAC $m"; short = "✗ 403"
            }
            if (!sent) return
            pending.removeFirst()
        }
    }

    private fun candidates(ble: String): List<String> {
        val p = ble.split(":")
        if (p.size != 6) return emptyList()
        val last = p[5].toIntOrNull(16) ?: return emptyList()
        val head = p.take(5).joinToString(":").uppercase()
        return listOf(0, -2, -1, -3).map { d -> "$head:%02X".format((last + d) and 0xff) }
    }

    /** Thu lan luot: mang mac dinh (giong ban do), roi tung mang co Internet (4G/Wi-Fi), roi 4G xin rieng. */
    private fun post(mac: String, body: String): Int {
        val u = URL("https://evcarhud.com/api/log?model=p4&mac=$mac&v=app-lan-$ver&boot=$boot&seq=$seq")
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        val routes = ArrayList<android.net.Network?>()
        routes += null
        try {
            @Suppress("DEPRECATION")
            val all = cm.allNetworks.filter { n -> cm.getNetworkCapabilities(n)?.hasCapability(android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET) == true }
            routes += all.sortedByDescending { n -> if (cm.getNetworkCapabilities(n)?.hasCapability(android.net.NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true) 1 else 0 }
        } catch (e: Exception) { lastErr = "mạng: ${e.javaClass.simpleName}" }
        val cn = cellNet
        if (cn == null) wantCell() else if (cn !in routes) routes.add(cn)
        val errs = ArrayList<String>()
        for ((i, net) in routes.withIndex()) {
            try {
                ((net?.openConnection(u) ?: u.openConnection()) as HttpURLConnection).run {
                    requestMethod = "POST"; doOutput = true; connectTimeout = 8_000; readTimeout = 10_000
                    setRequestProperty("Content-Type", "text/plain; charset=utf-8")
                    setRequestProperty("User-Agent", "EVCarHUD-Lan/$ver")
                    outputStream.use { it.write(("[app-lan]\n" + body).toByteArray()) }
                    val c = responseCode
                    try { (if (c < 400) inputStream else errorStream)?.close() } catch (_: Exception) {}
                    disconnect()
                    if (c in 200..299 && errs.isNotEmpty()) line("log: duong $i ok, truoc do loi ${errs.joinToString("; ")}")
                    return c
                }
            } catch (e: Exception) {
                Log.w("CloudLog", "post route $i", e)
                errs += "$i:" + e.javaClass.simpleName.removeSuffix("Exception")
            }
        }
        lastErr = errs.joinToString(" ")
        return -1
    }

    /** Ma ngan hien tren dong trang thai: "✓" hoac ly do loi. */
    @Volatile var short = "chờ"
    @Volatile var lastErr = ""

    /** Wi-Fi cam guong khong co Internet nen Android co the tat 4G: xin giu 4G de gui log. */
    @Volatile private var cellNet: android.net.Network? = null
    private var cellAsked = false
    private fun wantCell() {
        if (cellAsked) return
        cellAsked = true
        try {
            val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
            val req = android.net.NetworkRequest.Builder()
                .addTransportType(android.net.NetworkCapabilities.TRANSPORT_CELLULAR)
                .addCapability(android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET).build()
            cm.requestNetwork(req, object : android.net.ConnectivityManager.NetworkCallback() {
                override fun onAvailable(n: android.net.Network) { cellNet = n; flushSoon() }
                override fun onLost(n: android.net.Network) { if (cellNet == n) cellNet = null }
            })
        } catch (e: Exception) { lastErr = "xin 4G: ${e.javaClass.simpleName}"; cellAsked = false }
    }

    companion object { private const val FALLBACK_MAC = "E8:F6:0A:E5:50:ED" }

    fun close() { flush(); th.quitSafely() }
}
