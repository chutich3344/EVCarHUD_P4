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

    init { h.postDelayed(object : Runnable { override fun run() { flush(); h.postDelayed(this, 60_000) } }, 60_000) }

    fun line(s: String) {
        val l = "${fmt.format(Date())} $s\n"
        synchronized(buf) { if (buf.length < 200_000) buf.append(l) }
    }

    fun flushSoon() { h.post { flush() } }

    private fun flush() {
        val body = synchronized(buf) { val b = buf.toString(); buf.setLength(0); b }
        if (body.isEmpty()) return
        try {
            if (file.length() > 2_000_000) file.writeText("")
            file.appendText(body)
        } catch (_: Exception) {}
        // chua noi HUD van gui: dung MAC HUD P4 da dang ky cua chu xe (E8:F6:0A:E5:50:ED, log HUD tu 12/11)
        val macs = if (okMac.isNotEmpty()) listOf(okMac) else (candidates(hudMac) + FALLBACK_MAC).distinct()
        for (m in macs) {
            val st = post(m, body)
            if (st in 200..299) { okMac = m; seq++; lastStatus = "đã gửi lúc ${fmt.format(Date())}"; return }
            if (st != 403) { lastStatus = "lỗi gửi $st (mạng?)"; break }
            lastStatus = "máy chủ chưa nhận MAC HUD"
        }
        // khong gui duoc: tra lai bo dem cho lan sau (giu toi da 200 KB)
        synchronized(buf) { if (buf.length + body.length < 200_000) buf.insert(0, body) }
    }

    private fun candidates(ble: String): List<String> {
        val p = ble.split(":")
        if (p.size != 6) return emptyList()
        val last = p[5].toIntOrNull(16) ?: return emptyList()
        val head = p.take(5).joinToString(":").uppercase()
        return listOf(0, -2, -1, -3).map { d -> "$head:%02X".format((last + d) and 0xff) }
    }

    private fun post(mac: String, body: String): Int = try {
        val u = URL("https://evcarhud.com/api/log?model=p4&mac=$mac&v=app-lan-$ver&boot=$boot&seq=$seq")
        // noi Wi-Fi cam guong (khong Internet): gui qua mang co Internet (4G/5G)
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        @Suppress("DEPRECATION")
        val net = cm.allNetworks.firstOrNull { n -> cm.getNetworkCapabilities(n)?.hasCapability(android.net.NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true }
        ((net?.openConnection(u) ?: u.openConnection()) as HttpURLConnection).run {
            requestMethod = "POST"; doOutput = true; connectTimeout = 10_000; readTimeout = 10_000
            setRequestProperty("Content-Type", "text/plain; charset=utf-8")
            outputStream.use { it.write(("[app-lan]\n" + body).toByteArray()) }
            val c = responseCode
            try { (if (c < 400) inputStream else errorStream)?.close() } catch (_: Exception) {}
            disconnect(); c
        }
    } catch (e: Exception) { Log.w("CloudLog", "post", e); -1 }

    companion object { private const val FALLBACK_MAC = "E8:F6:0A:E5:50:ED" }

    fun close() { flush(); th.quitSafely() }
}
