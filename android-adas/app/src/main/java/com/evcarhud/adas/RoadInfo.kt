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
              val lat: DoubleArray, val lon: DoubleArray, val lanesF: Int? = null, val lanesB: Int? = null)

    class Def(val kmh: Int?, val why: String)

    @Volatile private var ways: List<Way> = emptyList()
    @Volatile private var cLat = 0.0
    @Volatile private var cLon = 0.0
    @Volatile private var busy = false
    @Volatile private var lastTry = 0L
    @Volatile private var good = 0   // may chu chay duoc lan truoc -> thu truoc
    @Volatile var cur: Def? = null; private set
    /** 24/11: duong dang chay theo ban do cho man 3D: [1 chieu 0/1, duong doi 0/1, so lan CHIEU MINH (0 = khong ro)]. */
    @Volatile var dir: IntArray? = null; private set
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
        dir = w?.let {
            val fwd = it.oneway == 1 || headingAlong(it, lat, lon, brg)
            val n = if (it.oneway != 0) it.lanes else (if (fwd) it.lanesF else it.lanesB) ?: it.lanes?.let { l -> if (l >= 2) l / 2 else null }
            intArrayOf(if (it.oneway != 0) 1 else 0, if (it.dual) 1 else 0, (n ?: 0).coerceIn(0, 6))
        }
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
        val q = "[out:json][timeout:12];way(around:400,%.6f,%.6f)[highway~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|road|service)(_link)?$\"][service!~\"parking_aisle|drive-through\"];out tags geom;"
            .format(java.util.Locale.US, lat, lon)
        val body = "data=" + URLEncoder.encode(q, "UTF-8")
        var err: Exception? = null
        for (k in HOSTS.indices) {
            val hi = (good + k) % HOSTS.size; val host = HOSTS[hi]
            try {
                val c = URL(host).openConnection() as HttpURLConnection
                c.connectTimeout = 6000; c.readTimeout = 15000; c.requestMethod = "POST"; c.doOutput = true
                c.setRequestProperty("User-Agent", "EVCarHUD-Lan/0.1 (Android)")
                c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                c.outputStream.use { it.write(body.toByteArray()) }
                if (c.responseCode != 200) { c.disconnect(); throw RuntimeException("HTTP ${c.responseCode}") }
                val txt = c.inputStream.bufferedReader().use { it.readText() }
                ways = parse(txt); cLat = lat; cLon = lon; good = hi
                log("ROAD ${ways.size} duong quanh xe · ${URL(host).host}")
                return
            } catch (e: Exception) { err = e; log("ROAD ${URL(host).host} loi ${e.javaClass.simpleName}") }
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
                if (ow == -1) la.reversedArray() else la, if (ow == -1) lo.reversedArray() else lo,
                t.optString("lanes:forward").toIntOrNull(), t.optString("lanes:backward").toIntOrNull())
        }
        return out
    }

    private fun kmh(s: String): Int? = Regex("^(\\d{2,3})").find(s.trim())?.groupValues?.get(1)?.toIntOrNull()?.takeIf { it in 5..130 }

    /** Duong gan nhat (< 25 m), uu tien cung huong. */
    private fun match(lat: Double, lon: Double, brg: Float, kmh: Float): Way? {
        var best: Way? = null; var bs = 1e9
        for (w in ways) if (w.hw != "service") for (k in 0 until w.lat.size - 1) {   // duong phu (service) chi de ve loi re, khong khop toc do
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

    /**
     * Doan duong phia truoc xe dai distM met theo hinh duong OSM (cho ban do khi dan duong Waze: Waze khong gui toa do tuyen).
     * Bat dau tu duong khop gan nhat, di theo huong xe; het duong thi noi sang duong co dau mut chung, it doi huong nhat.
     * Tra ve (lat[], lon[]) hoac null neu chua co duong quanh xe.
     */
    fun ahead(lat: Double, lon: Double, brg: Float, distM: Int): Pair<DoubleArray, DoubleArray>? {
        val ws = ways; if (ws.isEmpty() || distM <= 0) return null
        var w = match(lat, lon, brg, 30f) ?: return null
        // doan gan nhat + chieu di
        var bk = 0; var bd = 1e9
        for (k in 0 until w.lat.size - 1) { val (d, _) = segDist(lat, lon, w.lat[k], w.lon[k], w.lat[k + 1], w.lon[k + 1]); if (d < bd) { bd = d; bk = k } }
        var fwd = headingAlong(w, lat, lon, brg) || w.oneway != 0
        val la = ArrayList<Double>(); val lo = ArrayList<Double>()
        la += lat; lo += lon
        var left = distM.toDouble()
        var i = if (fwd) bk + 1 else bk
        var guard = 0
        while (left > 0 && guard++ < 400) {
            if (i < 0 || i >= w.lat.size) {
                // het duong: tim duong noi tiep o dau mut, huong gan nhat
                val ex = la.last(); val ey = lo.last()
                val pb = bearingOf(la[la.size - 2], lo[lo.size - 2], ex, ey)
                var best: Way? = null; var bf = true; var bs = 1e9
                for (o in ws) if (o !== w && o.hw != "service") {
                    val n = o.lat.size
                    if (dist(o.lat[0], o.lon[0], ex, ey) < 3) { val b = bearingOf(o.lat[0], o.lon[0], o.lat[1], o.lon[1]); val da = angDiff(b, pb); if (da < bs) { bs = da; best = o; bf = true } }
                    if (o.oneway == 0 && dist(o.lat[n - 1], o.lon[n - 1], ex, ey) < 3) { val b = bearingOf(o.lat[n - 1], o.lon[n - 1], o.lat[n - 2], o.lon[n - 2]); val da = angDiff(b, pb); if (da < bs) { bs = da; best = o; bf = false } }
                }
                if (best == null || bs > 60) break
                w = best; fwd = bf; i = if (fwd) 1 else w.lat.size - 2
                continue
            }
            val d = dist(la.last(), lo.last(), w.lat[i], w.lon[i])
            if (d >= left) {
                val t = left / maxOf(d, 0.01)
                la += la.last() + (w.lat[i] - la.last()) * t; lo += lo.last() + (w.lon[i] - lo.last()) * t
                break
            }
            la += w.lat[i]; lo += w.lon[i]; left -= d
            i += if (fwd) 1 else -1
        }
        return if (la.size >= 2) la.toDoubleArray() to lo.toDoubleArray() else null
    }

    /**
     * 23/11 toi: duong nhanh / duong cat ngang phia truoc theo ban do (OSM), de canh 3D ve loi re / nga tu DUNG cho.
     * Tra ve moi nhanh: [khoang cach doc duong m, ben (-1 trai, 1 phai, 2 cat ngang ca 2 ben), goc so voi huong di (do, 0 = vuong goc ve phia truoc)].
     * Nut giao: duong khac co dinh trung (< 4 m) voi duong minh se di (ahead). Duong dang di + duong noi tiep bo qua.
     */
    fun branches(lat: Double, lon: Double, brg: Float, maxM: Int = 90): List<FloatArray> {
        val ws = ways; if (ws.isEmpty()) return emptyList()
        val me = match(lat, lon, brg, 30f) ?: return emptyList()
        val (pa, po) = ahead(lat, lon, brg, maxM) ?: return emptyList()
        val k0 = cos(Math.toRadians(lat)) * 111320.0
        fun ex(la: Double, lo: Double) = doubleArrayOf((lo - lon) * k0, (la - lat) * 110540.0)   // dong, bac (m)
        val P = pa.indices.map { ex(pa[it], po[it]) }
        val cum = DoubleArray(P.size); for (i in 1 until P.size) cum[i] = cum[i - 1] + Math.hypot(P[i][0] - P[i - 1][0], P[i][1] - P[i - 1][1])
        val out = ArrayList<FloatArray>()
        for (w in ws) {
            if (w === me) continue
            for (j in w.lat.indices) {
                val q = ex(w.lat[j], w.lon[j])
                // diem gan nhat tren duong di
                var bd = 1e9; var bs = 0.0; var bi = 0
                for (i in 0 until P.size - 1) {
                    val ax = P[i][0]; val ay = P[i][1]; val dx = P[i + 1][0] - ax; val dy = P[i + 1][1] - ay; val L2 = dx * dx + dy * dy + 1e-9
                    val t = (((q[0] - ax) * dx + (q[1] - ay) * dy) / L2).coerceIn(0.0, 1.0)
                    val d = Math.hypot(ax + t * dx - q[0], ay + t * dy - q[1])
                    if (d < bd) { bd = d; bs = cum[i] + t * Math.sqrt(L2); bi = i }
                }
                if (bd > 4 || bs < 4 || bs > maxM - 2) continue
                val dx = P[bi + 1][0] - P[bi][0]; val dy = P[bi + 1][1] - P[bi][1]; val dl = Math.hypot(dx, dy) + 1e-9
                var l = false; var r = false; var ang = 0.0
                for (nb in intArrayOf(j - 1, j + 1)) {
                    if (nb < 0 || nb >= w.lat.size) continue
                    val n = ex(w.lat[nb], w.lon[nb]); val vx = n[0] - q[0]; val vy = n[1] - q[1]; val vl = Math.hypot(vx, vy) + 1e-9
                    val cr = (dx * vy - dy * vx) / (dl * vl)        // > 0: ben trai (huong bac-dong)
                    val dot = (dx * vx + dy * vy) / (dl * vl)
                    if (abs(cr) < 0.35) continue                     // gan nhu song song = duong noi tiep / trung duong minh
                    if (cr > 0) l = true else r = true
                    ang = Math.toDegrees(Math.asin(dot.coerceIn(-1.0, 1.0)))
                }
                if (!l && !r) continue
                if (out.any { abs(it[0] - bs) < 8 }) continue
                out += floatArrayOf(bs.toFloat(), if (l && r) 2f else if (l) -1f else 1f, ang.toFloat())
            }
        }
        return out.sortedBy { it[0] }.take(4)
    }

    private fun bearingOf(a1: Double, o1: Double, a2: Double, o2: Double): Float {
        val dy = (a2 - a1) * 110_540.0; val dx = (o2 - o1) * 111_320.0 * cos(Math.toRadians(a1))
        return ((Math.toDegrees(atan2(dx, dy)) + 360) % 360).toFloat()
    }

    private fun angDiff(a: Float, b: Float): Double { var d = abs(((a - b) % 360 + 360) % 360).toDouble(); if (d > 180) d = 360 - d; return d }

    fun release() = io.shutdownNow()

    companion object {
        private val HOSTS = listOf(
            "https://overpass-api.de/api/interpreter",
            "https://overpass.kumi.systems/api/interpreter",
            "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
            "https://overpass.private.coffee/api/interpreter")
    }
}
