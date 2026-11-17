package com.evcarhud.adas

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.URL

/**
 * Do cam guong / dash cam Wi-Fi: tim cong mo + thu RTSP/HTTP hay gap tren cam Trung Quoc.
 * Wi-Fi cua cam khong co Internet nen Android hay dua ket noi qua 4G: phai dung dung Network Wi-Fi.
 */
object Probe {
    private val PORTS = intArrayOf(554, 8554, 80, 8080, 8081, 81, 7070, 6035, 3333, 5000, 8000, 8888, 9000, 10080, 34567, 2222, 21, 23)
    private val RTSP_PATHS = listOf("", "/live", "/live/main", "/live/ch0", "/livestream/0", "/stream", "/stream0", "/h264",
        "/av0_0", "/11", "/1", "/ch0", "/ch1", "/cam1/h264", "/front", "/video", "/main", "/tcp/av0_0")
    private val HTTP_PATHS = listOf("/", "/?custom=1&cmd=3014", "/cgi-bin/hisnet/getdeviceattr.cgi", "/app/getdeviceattr", "/livestream", "/video.mjpg")

    fun run(ctx: Context, log: (String) -> Unit) {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        @Suppress("DEPRECATION")
        val net: Network? = cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
        if (net == null) { log("Chưa nối Wi-Fi nào. Vào Cài đặt điện thoại nối Wi-Fi cam gương rồi thử lại."); return }
        val lp = cm.getLinkProperties(net)
        val me = lp?.linkAddresses?.mapNotNull { (it.address as? Inet4Address)?.hostAddress }?.firstOrNull() ?: "?"
        val gw = lp?.routes?.mapNotNull { r -> r.gateway?.let { it as? Inet4Address }?.hostAddress }?.firstOrNull { it != "0.0.0.0" }
        log("Wi-Fi: ${lp?.interfaceName} · máy mình $me · cổng mạng ${gw ?: "?"}")
        val hosts = linkedSetOf<String>()
        gw?.let { hosts += it }
        val pre = me.substringBeforeLast('.', "192.168.5")
        hosts += "$pre.1"; hosts += "$pre.254"; hosts += "192.168.1.254"; hosts += "192.168.0.1"
        val sf = net.socketFactory
        for (h in hosts) {
            val open = PORTS.filter { p ->
                try { sf.createSocket().use { s -> s.connect(InetSocketAddress(h, p), 400) }; true } catch (_: Exception) { false }
            }
            if (open.isEmpty()) { log("$h: không cổng nào mở"); continue }
            log("$h: cổng mở ${open.joinToString()}")
            for (p in open) {
                if (p == 554 || p == 8554) rtsp(sf, h, p, log)
                else if (p in intArrayOf(80, 8080, 8081, 81, 8000, 8888, 9000, 10080, 5000, 7070)) http(net, h, p, log)
                else banner(sf, h, p, log)
            }
        }
        log("Xong. Bấm Sao chép rồi dán gửi em.")
    }

    private fun rtsp(sf: javax.net.SocketFactory, h: String, p: Int, log: (String) -> Unit) {
        var found = 0
        for (path in RTSP_PATHS) {
            val url = "rtsp://$h:$p$path"
            try {
                sf.createSocket().use { s ->
                    s.connect(InetSocketAddress(h, p), 800); s.soTimeout = 1500
                    s.getOutputStream().write("DESCRIBE $url RTSP/1.0\r\nCSeq: 2\r\nAccept: application/sdp\r\nUser-Agent: EVCarHUD\r\n\r\n".toByteArray())
                    val r = BufferedReader(InputStreamReader(s.getInputStream()))
                    val st = r.readLine() ?: ""
                    val code = st.split(" ").getOrNull(1) ?: "?"
                    val lines = mutableListOf<String>()
                    if (code == "200") repeat(40) { r.readLine()?.let { l -> if (l.startsWith("m=") || l.startsWith("a=rtpmap") || l.startsWith("a=control") || l.startsWith("a=framerate") || l.startsWith("Server")) lines += l } }
                    if (code == "200" || code == "401") { found++; log("  RTSP $url -> $code ${lines.joinToString(" | ")}") }
                    else if (path == "") log("  RTSP $url -> $st")
                }
            } catch (e: Exception) { if (path == "") log("  RTSP $url lỗi ${e.javaClass.simpleName}") }
            if (found >= 3) break
        }
        if (found == 0) log("  RTSP: không đường dẫn nào trả 200")
    }

    private fun http(net: Network, h: String, p: Int, log: (String) -> Unit) {
        for (path in HTTP_PATHS) {
            try {
                val c = net.openConnection(URL("http://$h:$p$path")) as HttpURLConnection
                c.connectTimeout = 1000; c.readTimeout = 1500
                val code = c.responseCode
                val type = c.contentType ?: ""
                val server = c.getHeaderField("Server") ?: ""
                val body = if (type.startsWith("text") || type.contains("xml") || type.contains("json"))
                    (if (code < 400) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText().take(160) }?.replace(Regex("\\s+"), " ") ?: "" else ""
                c.disconnect()
                if (path == "/" || code == 200) log("  HTTP :$p$path -> $code $type $server $body")
            } catch (e: Exception) { if (path == "/") log("  HTTP :$p$path lỗi ${e.javaClass.simpleName}") }
        }
    }

    private fun banner(sf: javax.net.SocketFactory, h: String, p: Int, log: (String) -> Unit) {
        try {
            sf.createSocket().use { s ->
                s.connect(InetSocketAddress(h, p), 800); s.soTimeout = 1200
                val b = ByteArray(64); val n = try { s.getInputStream().read(b) } catch (_: Exception) { 0 }
                log("  :$p chào ${if (n > 0) b.copyOf(n).joinToString("") { c -> if (c in 32..126) c.toInt().toChar().toString() else "." } else "(im lặng)"}")
            }
        } catch (e: Exception) { log("  :$p lỗi ${e.javaClass.simpleName}") }
    }
}
