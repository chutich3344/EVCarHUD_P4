package com.evcarhud.adas

import android.content.Context
import android.content.Intent
import android.net.Uri
import java.net.HttpURLConnection
import java.net.URLDecoder

/**
 * Lay diem dich tu Intent:
 *  - app EVCarHUD Charge: action com.evcarhud.adas.NAVIGATE, extra lat/lon (double), name, kind ("tramsac")
 *  - chia se tu HERE WeGo / Google Maps (ACTION_SEND text co link): mo link rut gon, tim toa do trong link / trang
 *  - mo dia diem kieu geo:lat,lon?q=... / google.navigation:q=lat,lon (ACTION_VIEW)
 * Google Maps / HERE WeGo khong xuat duong di, chi co diem dich -> app tu tinh tuyen.
 */
object ShareLink {
    const val ACTION_NAV = "com.evcarhud.adas.NAVIGATE"

    /** Nhanh (khong can mang): tra ve Dest neu Intent da co san toa do. */
    fun quick(i: Intent?): Dest? {
        i ?: return null
        if (i.action == ACTION_NAV) {
            val la = i.getDoubleExtra("lat", Double.NaN); val lo = i.getDoubleExtra("lon", Double.NaN)
            if (la.isNaN() || lo.isNaN()) return null
            return Dest(la, lo, i.getStringExtra("name")?.takeIf { it.isNotBlank() } ?: "Trạm sạc", i.getStringExtra("kind") ?: "tramsac")
        }
        if (i.action == Intent.ACTION_VIEW) i.data?.let { u -> return fromText(u.toString(), "") }
        return null
    }

    /** Intent chia se can mo mang (link rut gon)? */
    fun isShare(i: Intent?) = i?.action == Intent.ACTION_SEND && i.getStringExtra(Intent.EXTRA_TEXT) != null

    /** Chay tren luong rieng (co the mo mang). */
    fun resolve(ctx: Context, i: Intent): Dest? {
        val text = i.getStringExtra(Intent.EXTRA_TEXT) ?: return null
        val subj = i.getStringExtra(Intent.EXTRA_SUBJECT) ?: ""
        val url = Regex("https?://\\S+").find(text)?.value
        val name = guessName(text.replace(url ?: "\u0000", ""), subj)
        fromText(text, name)?.let { return it }
        if (url == null) return null
        // theo chuyen huong (maps.app.goo.gl, her.is, share.here.com...) toi link day du
        var u = url
        for (k in 0 until 6) {
            fromText(u, name)?.let { return it }
            val c = Nav.open(ctx, u)
            c.instanceFollowRedirects = false
            try {
                val code = c.responseCode
                val loc = c.getHeaderField("Location")
                if (code in 300..399 && loc != null) { u = java.net.URL(java.net.URL(u), loc).toString(); continue }
                // khong chuyen huong nua: tim toa do trong trang
                val body = c.inputStream.use { s -> s.readBytes().toString(Charsets.UTF_8).take(400_000) }
                return fromText(body, name) ?: fromPage(body, name)
            } finally { c.disconnect() }
        }
        return null
    }

    private fun guessName(t: String, subj: String): String {
        val line = t.lines().map { it.trim() }.firstOrNull { it.isNotEmpty() && !it.startsWith("http") } ?: ""
        val n = (if (line.length in 2..60) line else subj).trim().trimEnd(':', '-', '·').trim()
        return n.ifEmpty { "Điểm đến" }
    }

    private val NUM = "(-?\\d{1,3}\\.\\d{3,})"
    private val PATS = listOf(
        Regex("!3d$NUM!4d$NUM"),                                              // Google: dia diem chinh xac
        Regex("@$NUM,$NUM"),                                                  // Google: tam ban do
        Regex("[?&](?:q|ll|query|destination|daddr|center|sll)=$NUM(?:,|%2C)\\s*\\+?$NUM"),
        Regex("geo:$NUM,$NUM"),
        Regex("google\\.navigation:q=$NUM,$NUM"),
        Regex("/l/$NUM,$NUM"),                                                // share.here.com/l/lat,lon,ten
        Regex("[?&]map=$NUM,$NUM"),                                           // wego.here.com/?map=lat,lon,z
        Regex("/p/[^?]*?\\b$NUM,$NUM"),
    )

    fun fromText(raw: String, name: String): Dest? {
        val t = try { URLDecoder.decode(raw.replace("+", "%2B"), "UTF-8") } catch (_: Exception) { raw }
        for (p in PATS) {
            val m = p.find(t) ?: continue
            val la = m.groupValues[1].toDoubleOrNull() ?: continue
            val lo = m.groupValues[2].toDoubleOrNull() ?: continue
            if (la !in -90.0..90.0 || lo !in -180.0..180.0 || (la == 0.0 && lo == 0.0)) continue
            var n = name
            if (n.isEmpty() || n == "Điểm đến") {
                // geo:..?q=lat,lon(Ten) / share.here.com/l/lat,lon,Ten
                n = Regex("\\(([^)]{2,60})\\)").find(t)?.groupValues?.get(1)
                    ?: Regex("/l/$NUM,$NUM,([^/?&]{2,60})").find(t)?.groupValues?.get(3)?.replace('_', ' ')?.replace('+', ' ')
                    ?: Uri.parse(raw).getQueryParameter("q")?.takeIf { q -> q.none { it.isDigit() } }
                    ?: "Điểm đến"
            }
            return Dest(la, lo, n, "link")
        }
        return null
    }

    /** Trang Google Maps: toa do nam trong og:image / meta (center=lat%2Clon) hoac mang [null,null,lat,lon]. */
    private fun fromPage(b: String, name: String): Dest? {
        Regex("center=$NUM%2C$NUM").find(b)?.let { return Dest(it.groupValues[1].toDouble(), it.groupValues[2].toDouble(), name, "link") }
        Regex("\\[null,null,$NUM,$NUM\\]").find(b)?.let { return Dest(it.groupValues[1].toDouble(), it.groupValues[2].toDouble(), name, "link") }
        return null
    }

    @Suppress("unused") private fun unused(c: HttpURLConnection) = c
}
