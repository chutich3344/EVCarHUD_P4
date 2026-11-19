package com.evcarhud.adas

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sqrt

/**
 * Toc do mac dinh khi chua gap bien (Thong tu 38/2024, ap dung 01/01/2025, xe con <= 28 cho):
 * - Duong co maxspeed tren OSM -> dung so do.
 * - Cao toc: khong doan (theo bien).
 * - Con lai theo muc khu dong dan cu (than trong, vi app chua doc duoc bien R.420/R.421):
 *   duong doi / mot chieu tu 2 lan -> 60, duong hai chieu / mot chieu 1 lan -> 50.
 * Lay duong quanh xe 1 lan (ban kinh 500 m, Overpass), di qua 300 m moi tai lai; khop duong gan nhat theo khoang cach + huong.
 */
class RoadInfo(private val log: (String) -> Unit = {}) {
    class Way(val hw: String, val oneway: Int, val lanes: Int?, val maxF: Int?, val maxB: Int?, val dual: Boolean, val name: String?,
              val lat: DoubleArray, val lon: DoubleArray)

    class Def(val kmh: Int?, val why: String)

    @Volatile private var ways: List<Way> = emptyList()
    @Volatile private var cLat = 0.0
    @Volatile private var cLon = 0.0
    @Volatile private var busy = false
    @Volatile private var lastTry = 0L
    @Volatile var cur: Def? = null; private set
    private val io = Executors.newSingleThreadExecutor()

    /** Goi moi lan co GPS (luong chinh). */
    fun onFix(lat: Double, lon: Double, brg: Float, kmh: Float) {
        if (lat == 0.0 && lon == 0.0) return
        val now = System.currentTimeMillis()
        if ((ways.isEmpty() || dist(cLat, cLon, lat, lon) > 300) && !busy && now - lastTry > 20_000) {
            busy = true; lastTry = now
            io.execute { try { fetch(lat, lon) } catch (e: Exception) { log("ROAD loi ${e.javaClass.simpleName}") } finally { busy = false } }
        }
        val w = match(lat, lon, brg, kmh)
        val prev = cur
        cur = if (w == null) (if (ways.isEmpty()) Def(50, "luật · 2 chiều") else cur ?: Def(50, "luật · 2 chiều")) else rule(w, lat, lon, brg)
        cur?.let { c -> if (prev?.kmh != c.kmh || prev?.why != c.why) log("TOC DO MAC DINH ${c.kmh ?: "-"} (${c.why}${w?.let { " · " + it.hw + (it.name?.let { n -> " " + n } ?: "") } ?: ""})") }
    }

    private fun rule(w: Way, lat: Double, lon: Double, brg: Float): Def {
        val fwd = w.oneway == 1 || headingAlong(w, lat, lon, brg)
        val tag = if (fwd) w.maxF else w.maxB
        if (tag != null) return Def(tag, "theo bản đồ")
        if (w.hw.startsWith("motorway")) return Def(null, "cao tốc")
        val divided = w.dual || (w.oneway != 0 && ((w.lanes ?: 0) >= 2 || (w.lanes == null && w.hw.substringBefore('_') in setOf("trunk", "primary"))))
        return if (divided) Def(60, "luật · đường đôi") else Def(50, "luật · 2 chiều")
    }

    private fun fetch(lat: Double, lon: Double) {
        val q = "[out:json][timeout:15];way(around:500,%.6f,%.6f)[highway~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|road)(_link)?$\"];out tags geom;"
            .format(java.util.Locale.US, lat, lon)
        val body = "data=" + URLEncoder.encode(q, "UTF-8")
        var err: Exception? = null
        for (host in HOSTS) {
            try {
                val c = URL(host).openConnection() as HttpURLConnection
                c.connectTimeout = 8000; c.readTimeout = 20000; c.requestMethod = "POST"; c.doOutput = true
                c.setRequestProperty("User-Agent", "EVCarHUD-Lan/0.1 (Android)")
                c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                c.outputStream.use { it.write(body.toByteArray()) }
                if (c.responseCode != 200) { c.disconnect(); throw RuntimeException("HTTP ${c.responseCode}") }
                val txt = c.inputStream.bufferedReader().use { it.readText() }
                ways = parse(txt); cLat = lat; cLon = lon
                log("ROAD ${ways.size} duong quanh xe")
                return
            } catch (e: Exception) { err = e }
        }
        throw err ?: RuntimeException("?")
    }

    private fun parse(txt: String): List<Way> {
        val el = JSONObject(txt).getJSONArray("elements"); val out = ArrayList<Way>()
        for (i in 0 until el.length()) {
            val e = el.getJSONObject(i); val t = e.optJSONObject("tags") ?: continue; val g = e.optJSONArray("geometry") ?: continue
            if (g.length() < 2) continue
            val ow = when (t.optString("oneway")) { "yes", "1", "true" -> 1; "-1", "reverse" -> -1; else -> if (t.optString("junction") == "roundabout") 1 else 0 }
            val all = kmh(t.optString("maxspeed"))
            val la = DoubleArray(g.length()) { g.getJSONObject(it).getDouble("lat") }; val lo = DoubleArray(g.length()) { g.getJSONObject(it).getDouble("lon") }
            out += Way(t.optString("highway"), ow, t.optString("lanes").toIntOrNull(),
                kmh(t.optString("maxspeed:forward")) ?: all, kmh(t.optString("maxspeed:backward")) ?: all,
                t.optString("dual_carriageway") == "yes", t.optString("name").ifEmpty { null },
                if (ow == -1) la.reversedArray() else la, if (ow == -1) lo.reversedArray() else lo)
        }
        return out
    }

    private fun kmh(s: String): Int? = Regex("^(\\d{2,3})").find(s.trim())?.groupValues?.get(1)?.toIntOrNull()?.takeIf { it in 5..130 }

    /** Duong gan nhat (< 25 m), uu tien cung huong. */
    private fun match(lat: Double, lon: Double, brg: Float, kmh: Float): Way? {
        var best: Way? = null; var bs = 1e9
        for (w in ways) for (k in 0 until w.lat.size - 1) {
            val (d, segB) = segDist(lat, lon, w.lat[k], w.lon[k], w.lat[k + 1], w.lon[k + 1])
            if (d > 25) continue
            var da = abs(((segB - brg) % 360 + 360) % 360); if (da > 180) da = 360 - da
            if (w.oneway == 0 && da > 90) da = 180 - da
            val sc = d + (if (kmh > 8) da / 4 else 0f)
            if (sc < bs) { bs = sc; best = w }
        }
        return best
    }

    private fun headingAlong(w: Way, lat: Double, lon: Double, brg: Float): Boolean {
        var bd = 1e9; var sb = 0f
        for (k in 0 until w.lat.size - 1) { val (d, b) = segDist(lat, lon, w.lat[k], w.lon[k], w.lat[k + 1], w.lon[k + 1]); if (d < bd) { bd = d; sb = b } }
        var da = abs(((sb - brg) % 360 + 360) % 360); if (da > 180) da = 360 - da
        return da <= 90
    }

    /** Khoang cach (m) tu diem toi doan AB va huong doan (do). */
    private fun segDist(lat: Double, lon: Double, a1: Double, o1: Double, a2: Double, o2: Double): Pair<Double, Float> {
        val kx = 111_320.0 * cos(Math.toRadians(lat)); val ky = 110_540.0
        val ax = (o1 - lon) * kx; val ay = (a1 - lat) * ky; val bx = (o2 - lon) * kx; val by = (a2 - lat) * ky
        val dx = bx - ax; val dy = by - ay; val l2 = dx * dx + dy * dy
        val t = if (l2 == 0.0) 0.0 else (-(ax * dx + ay * dy) / l2).coerceIn(0.0, 1.0)
        val px = ax + t * dx; val py = ay + t * dy
        val b = Math.toDegrees(atan2(dx, dy)).toFloat()
        return sqrt(px * px + py * py) to ((b + 360) % 360)
    }

    private fun dist(a1: Double, o1: Double, a2: Double, o2: Double): Double {
        val dy = (a2 - a1) * 111_320.0; val dx = (o2 - o1) * 111_320.0 * cos(Math.toRadians(a1))
        return sqrt(dx * dx + dy * dy)
    }

    fun release() = io.shutdownNow()

    companion object {
        private val HOSTS = listOf("https://overpass-api.de/api/interpreter", "https://overpass.private.coffee/api/interpreter")
    }
}
