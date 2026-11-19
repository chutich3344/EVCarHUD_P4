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
    /**
     * Chay tren luong rieng (co the mo mang). Link khong co toa do (Google Maps ban moi chi gui ten + ftid, Waze gui geohash)
     * -> tim theo ten dia diem: HERE (neu da dan khoa, uu tien gan vi tri xe) hoac OpenStreetMap Nominatim.
     * log: ghi tung buoc len log may chu de do loi link la.
     */
    fun resolve(ctx: Context, i: Intent, here: Pair<Double, Double>?, hereKey: String, log: (String) -> Unit): Dest? {
        val text = i.getStringExtra(Intent.EXTRA_TEXT) ?: return null
        val subj = i.getStringExtra(Intent.EXTRA_SUBJECT) ?: ""
        log("LINK nhan: ${text.replace('\n', ' ').take(300)} | subj=${subj.take(80)}")
        val url = Regex("https?://\\S+").find(text)?.value
        val name = guessName(text.replace(url ?: "\u0000", ""), subj)
        fromText(text, name)?.let { return it }
        waze(text, name)?.let { return it }
        var place: String? = null
        if (url != null) {
            // theo chuyen huong (maps.app.goo.gl, her.is, share.here.com, waze.com/ul...) toi link day du
            var u: String = url
            for (k in 0 until 6) {
                fromText(u, name)?.let { return it }
                waze(u, name)?.let { return it }
                place = place ?: placeName(u)
                val c = Nav.open(ctx, u)
                c.instanceFollowRedirects = false
                try {
                    val code = c.responseCode
                    val loc = c.getHeaderField("Location")
                    log("LINK $code ${u.take(200)}")
                    if (code in 300..399 && loc != null) { u = java.net.URL(java.net.URL(u), loc).toString(); continue }
                    // khong chuyen huong nua: tim toa do trong trang
                    val body = (if (code in 200..299) c.inputStream else c.errorStream)?.use { s -> s.readBytes().toString(Charsets.UTF_8).take(400_000) } ?: ""
                    (fromText(body, name) ?: fromPage(body, name))?.let { return it }
                    break
                } catch (e: Exception) { log("LINK loi ${e.javaClass.simpleName}"); break } finally { c.disconnect() }
            }
        }
        // khong co toa do: tim theo ten
        val q = (place ?: name.takeIf { it != "Điểm đến" } ?: subj.trim()).trim()
        if (q.length < 3) return null
        val d = try { if (hereKey.isNotBlank()) hereFind(ctx, q, here, hereKey.trim()) else null } catch (e: Exception) { log("LINK HERE loi ${e.message}"); null }
            ?: try { osmFind(ctx, q) } catch (e: Exception) { log("LINK OSM loi ${e.message}"); null }
        log("LINK tim ten '$q' -> ${d?.let { "${it.lat},${it.lon}" } ?: "khong thay"}")
        return d?.let { Dest(it.lat, it.lon, name.takeIf { n -> n != "Điểm đến" } ?: q, "link") }
    }

    /** Ten dia diem trong link Google: /maps/place/Ten/... hoac ?q=Ten (khong phai so). */
    private fun placeName(u: String): String? {
        val t = try { URLDecoder.decode(u.replace("+", " ").replace("%2B", " "), "UTF-8") } catch (_: Exception) { u }
        Regex("/maps/place/([^/@?]{2,120})").find(t)?.let { return it.groupValues[1].trim() }
        val q = Regex("[?&](?:q|query|daddr|destination)=([^&]{2,160})").find(t)?.groupValues?.get(1)?.trim() ?: return null
        return q.takeIf { x -> Regex("^-?\\d+\\.\\d+\\s*,").find(x) == null }
    }

    /** Waze: waze.com/ul?ll=lat,lon (PATS da bat) hoac waze.com/ul/h<geohash>. */
    private fun waze(t: String, name: String): Dest? {
        val g = Regex("waze\\.com/ul/h([0-9b-hjkmnp-z]{5,12})").find(t)?.groupValues?.get(1) ?: return null
        val B = "0123456789bcdefghjkmnpqrstuvwxyz"
        var la0 = -90.0; var la1 = 90.0; var lo0 = -180.0; var lo1 = 180.0; var even = true
        for (ch in g) {
            val v = B.indexOf(ch); if (v < 0) return null
            for (b in 4 downTo 0) {
                val bit = (v shr b) and 1
                if (even) { val m = (lo0 + lo1) / 2; if (bit == 1) lo0 = m else lo1 = m } else { val m = (la0 + la1) / 2; if (bit == 1) la0 = m else la1 = m }
                even = !even
            }
        }
        return Dest((la0 + la1) / 2, (lo0 + lo1) / 2, name, "link")
    }

    private fun getJson(ctx: Context, u: String): String {
        val c = Nav.open(ctx, u)
        try {
            val code = c.responseCode
            if (code !in 200..299) throw Exception("mã $code")
            return c.inputStream.use { it.readBytes().toString(Charsets.UTF_8) }
        } finally { c.disconnect() }
    }

    /** HERE Discover: tim dia diem theo ten, gan vi tri xe (khong co GPS thi trong Viet Nam). */
    private fun hereFind(ctx: Context, q: String, at: Pair<Double, Double>?, key: String): Dest? {
        val e = java.net.URLEncoder.encode(q, "UTF-8")
        val near = if (at != null) "at=${at.first},${at.second}" else "in=countryCode:VNM"
        val o = org.json.JSONObject(getJson(ctx, "https://discover.search.hereapi.com/v1/discover?$near&q=$e&limit=1&lang=vi&apiKey=$key"))
        val x = o.optJSONArray("items")?.optJSONObject(0) ?: return null
        val p = x.getJSONObject("position")
        return Dest(p.getDouble("lat"), p.getDouble("lng"), x.optString("title", q), "link")
    }

    /** OpenStreetMap Nominatim (khong can khoa, chi gui ten dia diem). */
    private fun osmFind(ctx: Context, q: String): Dest? {
        val e = java.net.URLEncoder.encode(q, "UTF-8")
        val a = org.json.JSONArray(getJson(ctx, "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&countrycodes=vn&accept-language=vi&q=$e"))
        val x = a.optJSONObject(0) ?: return null
        return Dest(x.getString("lat").toDouble(), x.getString("lon").toDouble(), q, "link")
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
