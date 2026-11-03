package com.evcarhud.tramsac

import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlin.math.roundToInt

/** Nguon du lieu tram sac. Moi ham chay tren luong nen, nem loi khi that bai. */
object Sources {
    const val OSM = "osm"
    const val OCM = "ocm"
    const val CUSTOM = "custom"

    private const val UA = "EVCarHUD-TramSac/0.1 (Android)"

    /** Lay tram trong ban kinh radiusM quanh (lat, lon) theo nguon da chon trong cai dat. */
    fun fetch(p: Prefs, lat: Double, lon: Double, radiusM: Int): List<Station> = when (p.source) {
        OCM -> openChargeMap(p.ocmKey, lat, lon, radiusM)
        CUSTOM -> custom(p.customUrl, lat, lon, radiusM)
        else -> overpass(lat, lon, radiusM)
    }

    private fun get(url: String, post: String? = null): String {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 10000
        c.readTimeout = 20000
        c.setRequestProperty("User-Agent", UA)
        c.setRequestProperty("Accept", "application/json")
        if (post != null) {
            c.requestMethod = "POST"
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            c.outputStream.use { it.write(post.toByteArray()) }
        }
        try {
            val code = c.responseCode
            if (code !in 200..299) throw RuntimeException("HTTP $code")
            return c.inputStream.bufferedReader().use { it.readText() }
        } finally {
            c.disconnect()
        }
    }

    // ---------------- OpenStreetMap (Overpass): mien phi, khong can khoa ----------------
    private val OVERPASS = listOf(
        "https://overpass-api.de/api/interpreter",
        "https://overpass.kumi.systems/api/interpreter",
        "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    )

    private fun overpass(lat: Double, lon: Double, radiusM: Int): List<Station> {
        val q = "[out:json][timeout:20];nwr[\"amenity\"=\"charging_station\"](around:$radiusM,$lat,$lon);out center tags 200;"
        var last: Exception? = null
        for (ep in OVERPASS) {
            try {
                val body = get(ep, "data=" + URLEncoder.encode(q, "UTF-8"))
                return parseOverpass(JSONObject(body))
            } catch (e: Exception) {
                last = e
            }
        }
        throw last ?: RuntimeException("Overpass lỗi")
    }

    private fun parseOverpass(o: JSONObject): List<Station> {
        val out = ArrayList<Station>()
        val els = o.optJSONArray("elements") ?: return out
        for (i in 0 until els.length()) {
            val e = els.getJSONObject(i)
            val la: Double; val lo: Double
            if (e.has("lat")) { la = e.getDouble("lat"); lo = e.getDouble("lon") } else {
                val c = e.optJSONObject("center") ?: continue
                la = c.getDouble("lat"); lo = c.getDouble("lon")
            }
            val t = e.optJSONObject("tags") ?: JSONObject()
            val op = t.optString("operator").ifBlank { t.optString("brand") }
            val name = t.optString("name").ifBlank { t.optString("name:vi") }.ifBlank { op }.ifBlank { "Trạm sạc" }
            var kw = 0
            val keys = t.keys()
            while (keys.hasNext()) {
                val k = keys.next()
                if (k.startsWith("socket:") && k.endsWith(":output")) kw = maxOf(kw, parseKw(t.optString(k)))
            }
            val ports = t.optString("capacity").toIntOrNull() ?: 0
            out.add(Station(name, la, lo, op, kw, ports))
        }
        return out
    }

    /** "22 kW", "150kW", "7.4 kW", "11000 W" -> kW nguyen. */
    private fun parseKw(s: String): Int {
        val m = Regex("([0-9]+(?:[.,][0-9]+)?)\\s*(k?W)", RegexOption.IGNORE_CASE).find(s) ?: return 0
        val v = m.groupValues[1].replace(',', '.').toDoubleOrNull() ?: return 0
        return if (m.groupValues[2].length == 2) v.roundToInt() else (v / 1000).roundToInt()   // "kW" hay "W"
    }

    // ---------------- OpenChargeMap: can khoa API mien phi (openchargemap.org) ----------------
    private fun openChargeMap(key: String, lat: Double, lon: Double, radiusM: Int): List<Station> {
        if (key.isBlank()) throw RuntimeException("Chưa nhập khóa OpenChargeMap")
        val km = maxOf(1, radiusM / 1000)
        val url = "https://api.openchargemap.io/v3/poi?output=json&latitude=$lat&longitude=$lon" +
            "&distance=$km&distanceunit=KM&maxresults=200&compact=true&verbose=false&key=" + URLEncoder.encode(key, "UTF-8")
        val arr = JSONArray(get(url))
        val out = ArrayList<Station>()
        for (i in 0 until arr.length()) {
            val p = arr.getJSONObject(i)
            val a = p.optJSONObject("AddressInfo") ?: continue
            var kw = 0.0
            val cons = p.optJSONArray("Connections")
            if (cons != null) for (j in 0 until cons.length()) kw = maxOf(kw, cons.getJSONObject(j).optDouble("PowerKW", 0.0))
            val op = p.optJSONObject("OperatorInfo")?.optString("Title").orEmpty()
            out.add(
                Station(
                    a.optString("Title").ifBlank { "Trạm sạc" },
                    a.getDouble("Latitude"), a.getDouble("Longitude"),
                    op, if (kw.isNaN()) 0 else kw.roundToInt(), p.optInt("NumberOfPoints", 0),
                )
            )
        }
        return out
    }

    // ---------------- May chu rieng (vd. may chu trung gian cho GOFA Places) ----------------
    /**
     * GET <url>?lat=..&lon=..&r=<met>. Tra ve mang JSON, hoac {"stations":[...]}; moi phan tu:
     * {"name":"VinFast ...","lat":10.1,"lon":106.2,"operator":"VinFast","kw":60,"ports":4}
     */
    private fun custom(base: String, lat: Double, lon: Double, radiusM: Int): List<Station> {
        if (base.isBlank()) throw RuntimeException("Chưa nhập địa chỉ máy chủ")
        val sep = if (base.contains('?')) "&" else "?"
        val body = get("$base${sep}lat=$lat&lon=$lon&r=$radiusM").trim()
        val arr = if (body.startsWith("[")) JSONArray(body) else JSONObject(body).optJSONArray("stations") ?: JSONArray()
        val out = ArrayList<Station>()
        for (i in 0 until arr.length()) {
            val s = arr.getJSONObject(i)
            val la = s.optDouble("lat", s.optDouble("latitude", Double.NaN))
            val lo = s.optDouble("lon", s.optDouble("lng", s.optDouble("longitude", Double.NaN)))
            if (la.isNaN() || lo.isNaN()) continue
            out.add(
                Station(
                    s.optString("name").ifBlank { "Trạm sạc" }, la, lo,
                    s.optString("operator"), s.optInt("kw", 0), s.optInt("ports", 0),
                )
            )
        }
        return out
    }
}
