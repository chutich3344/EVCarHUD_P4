package com.evcarhud.adas

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.Calendar
import java.util.concurrent.Executors
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** 1 chi dan re tren tuyen. trn: ma huong re giong Waze HLP (HUD da co mui ten): 1 thang, 2 trai, 3 phai,
 *  4/5 cheo trai/phai, 6/7 gat trai/phai, 8 quay dau, 11 vong xuyen, 13/14 di theo nhanh trai/phai, 15/16 vao/ra duong nhanh, 17 toi noi. */
class Step(val along: Double, val trn: Int, val exit: Int, val name: String)

class Route(
    val lat: DoubleArray, val lon: DoubleArray,
    val cum: DoubleArray,          // quang duong cong don tu diem dau (m)
    val steps: List<Step>,
    val totalS: Double,            // thoi gian du kien (s)
    val src: String,
) {
    val total get() = cum.last()
}

/** Diem dich (tram sac, diem tu link chia se...). */
class Dest(val lat: Double, val lon: Double, val name: String, val kind: String)

/**
 * Dan duong ngay trong app Lan: tinh tuyen (HERE Routing v8 neu co khoa, khong thi OSRM), bam vi tri GPS vao tuyen,
 * chi dan re tiep theo, tinh lai tuyen khi di sai. Moi ham goi tren luong chinh (tinh tuyen chay luong rieng).
 * Dung chung cho ca app (dich vu chay nen + ban do PiP) nen la object.
 */
object Nav {
    private const val TAG = "Nav"
    private val main = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor()

    var dest: Dest? = null; private set
    var route: Route? = null; private set
    /** Quang duong da di tren tuyen (m). */
    var along = 0.0; private set
    var msg = ""; private set
    var arrived = false; private set
    private var seg = 0
    private var offCount = 0
    private var routing = false
    private var lastRouteAt = 0L
    private var arrivedAt = 0L
    private var gen = 0
    private var lastLat = 0.0
    private var lastLon = 0.0
    private var lastBearing = -1f
    private var hasFix = false
    private var hereKey = ""
    private var appCtx: Context? = null
    val listeners = ArrayList<() -> Unit>()

    val active get() = dest != null

    fun start(ctx: Context, d: Dest, key: String) {
        appCtx = ctx.applicationContext
        hereKey = key.trim()
        dest = d; route = null; along = 0.0; seg = 0; offCount = 0; arrived = false; arrivedAt = 0L
        gen++
        msg = "Đang tìm đường tới ${d.name}…"
        changed()
        if (hasFix) reroute() else msg = "Chờ GPS để tìm đường tới ${d.name}…"
    }

    fun stop() {
        if (dest == null) return
        dest = null; route = null; arrived = false; gen++
        msg = ""
        changed()
    }

    private fun changed() { listeners.toList().forEach { it() } }

    /** GPS moi (1 lan/giay). */
    fun onFix(lat: Double, lon: Double, speedMs: Float, bearing: Float?, acc: Float) {
        lastLat = lat; lastLon = lon; hasFix = true
        if (bearing != null && speedMs > 2f) lastBearing = bearing
        val d = dest ?: return
        val r = route
        if (r == null) { if (!routing) reroute(); return }
        // bam vao tuyen: tim doan gan nhat quanh doan dang di
        var best = Double.MAX_VALUE; var bestI = seg; var bestT = 0.0
        val from = max(0, seg - 8); val to = minOf(r.lat.size - 2, seg + 80)
        for (i in from..to) {
            val (dd, t) = projDist(lat, lon, r.lat[i], r.lon[i], r.lat[i + 1], r.lon[i + 1])
            if (dd < best) { best = dd; bestI = i; bestT = t }
        }
        val tol = max(45.0, acc * 1.5)
        if (best > tol) {
            offCount++
            // sai duong 3 lan GPS lien tiep: tinh lai (toi thieu 8 giay/lan)
            if (offCount >= 3 && !routing && SystemClock.elapsedRealtime() - lastRouteAt > 8000) {
                msg = "Đi khác tuyến, đang tìm đường lại…"
                reroute()
            }
        } else {
            offCount = 0
            seg = bestI
            val segLen = r.cum[bestI + 1] - r.cum[bestI]
            along = (r.cum[bestI] + bestT * segLen).coerceAtMost(r.total)
        }
        val left = r.total - along
        val direct = dist(lat, lon, d.lat, d.lon)
        if (!arrived && (left < 30 || direct < 25)) { arrived = true; arrivedAt = SystemClock.elapsedRealtime(); msg = "Đã đến ${d.name}" }
        // toi noi 25 giay thi tu ket thuc
        if (arrived && SystemClock.elapsedRealtime() - arrivedAt > 25_000) { stop(); return }
        changed()
    }

    /** Chi dan re tiep theo (bo qua cac buoc da qua). */
    fun next(): Step? {
        val r = route ?: return null
        return r.steps.firstOrNull { it.along > along + 5 }
    }

    fun nextDist(): Int {
        val s = next() ?: return -1
        return (s.along - along).roundToInt().coerceAtLeast(0)
    }

    fun remainM(): Int { val r = route ?: return 0; return (r.total - along).roundToInt().coerceAtLeast(0) }

    fun remainS(): Int {
        val r = route ?: return 0
        if (r.total <= 0) return 0
        return (r.totalS * (r.total - along) / r.total).roundToInt().coerceAtLeast(0)
    }

    fun eta(): String {
        val c = Calendar.getInstance(); c.add(Calendar.SECOND, remainS())
        return "%02d:%02d".format(c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE))
    }

    /**
     * Goi D1 gui HUD (char 4556d005):
     * D1|ma re|met toi cho re|so loi ra vong xuyen|met con lai|phut con lai|gio den HH:MM|ten duong sau khi re
     * "D1|0" = het dan duong.
     */
    fun d1(): String {
        if (!active) return "D1|0"
        val r = route ?: return "D1|1|-1|0|0|0||${clean(msg, 28)}"
        if (arrived) return "D1|17|0|0|0|0|${eta()}|${clean(dest?.name ?: "", 32)}"
        val s = next()
        val trn = s?.trn ?: 1
        val name = if (s == null || s.trn == 17) dest?.name ?: "" else s.name
        return "D1|$trn|${nextDist()}|${s?.exit ?: 0}|${remainM()}|${(remainS() + 59) / 60}|${eta()}|${clean(name, 32)}"
    }

    private fun clean(s: String, n: Int) = s.replace('|', '/').replace('\n', ' ').trim().take(n)

    private fun reroute() {
        val d = dest ?: return
        val ctx = appCtx ?: return
        routing = true
        lastRouteAt = SystemClock.elapsedRealtime()
        val g = gen
        val fLat = lastLat; val fLon = lastLon; val br = lastBearing; val key = hereKey
        changed()
        io.execute {
            var r: Route? = null
            var err = ""
            if (key.isNotEmpty()) {
                try { r = here(ctx, fLat, fLon, d, br, key) } catch (e: Exception) { err = "HERE: ${e.message}"; Log.w(TAG, "here", e) }
            }
            if (r == null) {
                for (base in OSRM) {
                    try { r = osrm(ctx, base, fLat, fLon, d, br); break } catch (e: Exception) { err = "OSRM: ${e.message}"; Log.w(TAG, "osrm $base", e) }
                }
            }
            main.post {
                routing = false
                if (g != gen) return@post
                if (r != null) {
                    route = r; along = 0.0; seg = 0; offCount = 0
                    msg = "Dẫn đường tới ${d.name}"
                } else msg = "Không tìm được đường ($err), thử lại…"
                changed()
                if (r == null) main.postDelayed({ if (g == gen && route == null && !routing) reroute() }, 15_000)
            }
        }
    }

    // ---------------- dich vu tinh tuyen ----------------
    private val OSRM = listOf("https://router.project-osrm.org", "https://routing.openstreetmap.de/routed-car")

    private fun osrm(ctx: Context, base: String, aLat: Double, aLon: Double, d: Dest, br: Float): Route {
        val bear = if (br >= 0) "&bearings=${br.roundToInt() % 360},60;" else ""
        val u = "$base/route/v1/driving/$aLon,$aLat;${d.lon},${d.lat}?overview=full&geometries=polyline6&steps=true$bear"
        val j = JSONObject(get(ctx, u))
        if (j.optString("code") != "Ok") throw Exception(j.optString("code", "lỗi"))
        val rt = j.getJSONArray("routes").getJSONObject(0)
        val pts = polyline(rt.getString("geometry"), 1e6)
        val steps = ArrayList<Step>()
        var acc = 0.0
        val legs = rt.getJSONArray("legs")
        for (li in 0 until legs.length()) {
            val ss = legs.getJSONObject(li).getJSONArray("steps")
            for (i in 0 until ss.length()) {
                val s = ss.getJSONObject(i)
                val m = s.getJSONObject("maneuver")
                val type = m.optString("type"); val mod = m.optString("modifier")
                val trn = osrmTurn(type, mod)
                val name = s.optString("name").ifEmpty { s.optString("ref") }
                if (trn > 0) steps += Step(acc, trn, if (type.startsWith("roundabout") || type == "rotary") m.optInt("exit", 0) else 0, name)
                acc += s.optDouble("distance", 0.0)
            }
        }
        return build(pts, steps, rt.optDouble("duration", 0.0), "OSRM")
    }

    private fun osrmTurn(type: String, mod: String): Int {
        when (type) {
            "depart", "new name", "notification" -> return 0
            "arrive" -> return 17
            "roundabout", "rotary", "roundabout turn", "exit roundabout", "exit rotary" -> return if (type.startsWith("exit")) 0 else 11
            "continue" -> if (mod == "straight" || mod.isEmpty()) return 0
            "fork" -> return if (mod.contains("left")) 13 else if (mod.contains("right")) 14 else 1
            "on ramp", "off ramp" -> return if (mod.contains("left")) 15 else if (mod.contains("right")) 16 else 1
            "merge" -> return if (mod.contains("left")) 4 else if (mod.contains("right")) 5 else 1
        }
        return when (mod) {
            "uturn" -> 8
            "sharp left" -> 6
            "left" -> 2
            "slight left" -> 4
            "straight" -> 1
            "slight right" -> 5
            "right" -> 3
            "sharp right" -> 7
            else -> 0
        }
    }

    private fun here(ctx: Context, aLat: Double, aLon: Double, d: Dest, br: Float, key: String): Route {
        val crs = if (br >= 0) ";course=${br.roundToInt() % 360}" else ""
        val u = "https://router.hereapi.com/v8/routes?transportMode=car&origin=$aLat,$aLon$crs&destination=${d.lat},${d.lon}" +
            "&return=polyline,turnByTurnActions,summary&lang=vi-VN&apiKey=" + URLEncoder.encode(key, "UTF-8")
        val j = JSONObject(get(ctx, u))
        val routes = j.optJSONArray("routes")
        if (routes == null || routes.length() == 0) throw Exception(j.optString("title", "không có tuyến"))
        val secs = routes.getJSONObject(0).getJSONArray("sections")
        val la = ArrayList<Double>(); val lo = ArrayList<Double>()
        val pend = ArrayList<Triple<Int, Int, Pair<Int, String>>>()   // chi so diem, ma re, (loi ra, ten)
        var dur = 0.0
        for (si in 0 until secs.length()) {
            val sec = secs.getJSONObject(si)
            val base = la.size
            val p = flexPolyline(sec.getString("polyline"))
            for (k in p.indices step 2) { la += p[k]; lo += p[k + 1] }
            dur += sec.optJSONObject("summary")?.optDouble("duration", 0.0) ?: 0.0
            val acts = sec.optJSONArray("turnByTurnActions") ?: continue
            for (i in 0 until acts.length()) {
                val a = acts.getJSONObject(i)
                val trn = hereTurn(a.optString("action"), a.optString("direction"), a.optString("severity"))
                if (trn <= 0) continue
                val nm = a.optJSONObject("nextRoad")?.let { nr ->
                    nr.optJSONArray("name")?.optJSONObject(0)?.optString("value")?.takeIf { it.isNotEmpty() }
                        ?: nr.optJSONArray("number")?.optJSONObject(0)?.optString("value")
                } ?: ""
                pend += Triple(base + a.optInt("offset", 0), trn, Pair(a.optInt("exit", 0), nm))
            }
        }
        val pts = DoubleArray(la.size * 2).also { for (i in la.indices) { it[2 * i] = la[i]; it[2 * i + 1] = lo[i] } }
        val cum = cumul(pts)
        val steps = pend.map { (i, trn, x) -> Step(cum[i.coerceIn(0, cum.size - 1)], trn, x.first, x.second) }
        return Route(DoubleArray(la.size) { la[it] }, DoubleArray(lo.size) { lo[it] }, cum, steps, dur, "HERE")
    }

    private fun hereTurn(action: String, dir: String, sev: String): Int {
        val l = dir == "left"; val r = dir == "right"
        return when (action) {
            "depart", "continue" -> 0
            "arrive" -> 17
            "uTurn" -> 8
            "roundaboutEnter", "roundaboutPass" -> 11
            "roundaboutExit" -> 0
            "keep" -> if (l) 13 else if (r) 14 else 1
            "exit", "ramp" -> if (l) 15 else 16
            "turn" -> when {
                l && sev == "light" -> 4; r && sev == "light" -> 5
                l && sev == "heavy" -> 6; r && sev == "heavy" -> 7
                l -> 2; r -> 3; else -> 1
            }
            else -> 0
        }
    }

    private fun build(pts: DoubleArray, steps: List<Step>, dur: Double, src: String): Route {
        val n = pts.size / 2
        return Route(DoubleArray(n) { pts[2 * it] }, DoubleArray(n) { pts[2 * it + 1] }, cumul(pts), steps, dur, src)
    }

    private fun cumul(pts: DoubleArray): DoubleArray {
        val n = pts.size / 2
        val c = DoubleArray(maxOf(n, 2))
        for (i in 1 until n) c[i] = c[i - 1] + dist(pts[2 * i - 2], pts[2 * i - 1], pts[2 * i], pts[2 * i + 1])
        if (n < 2) c[1] = c[0]
        return c
    }

    // ---------------- mang (qua 4G/5G neu Wi-Fi cam guong khong co Internet) ----------------
    fun inetNet(ctx: Context): Network? {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        fun ok(n: Network?) = n != null && cm.getNetworkCapabilities(n)?.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true
        val a = cm.activeNetwork
        if (ok(a)) return a
        @Suppress("DEPRECATION")
        return cm.allNetworks.firstOrNull { ok(it) }
    }

    fun open(ctx: Context, u: String): HttpURLConnection {
        val c = (inetNet(ctx)?.openConnection(URL(u)) ?: URL(u).openConnection()) as HttpURLConnection
        c.setRequestProperty("User-Agent", "EVCarHUD-Lan/0.1 (Android)")
        c.connectTimeout = 10000; c.readTimeout = 12000
        return c
    }

    private fun get(ctx: Context, u: String): String {
        val c = open(ctx, u)
        try {
            val code = c.responseCode
            val body = (if (code in 200..299) c.inputStream else c.errorStream)?.use { it.readBytes().toString(Charsets.UTF_8) } ?: ""
            if (code !in 200..299) throw Exception("mã $code")
            return body
        } finally { c.disconnect() }
    }

    // ---------------- hinh hoc ----------------
    fun dist(a: Double, b: Double, c: Double, d: Double): Double {
        val r = 6371000.0
        val p1 = Math.toRadians(a); val p2 = Math.toRadians(c)
        val dp = p2 - p1; val dl = Math.toRadians(d - b)
        val h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2)
        return 2 * r * atan2(sqrt(h), sqrt(1 - h))
    }

    /** Khoang cach tu P toi doan AB (m) + vi tri chieu t (0..1). Phang hoa cuc bo (doan ngan). */
    private fun projDist(pLat: Double, pLon: Double, aLat: Double, aLon: Double, bLat: Double, bLon: Double): Pair<Double, Double> {
        val k = cos(Math.toRadians(pLat)) * 111320.0
        val ax = (aLon - pLon) * k; val ay = (aLat - pLat) * 110540.0
        val bx = (bLon - pLon) * k; val by = (bLat - pLat) * 110540.0
        val dx = bx - ax; val dy = by - ay
        val l2 = dx * dx + dy * dy
        val t = if (l2 < 1e-6) 0.0 else ((-ax * dx - ay * dy) / l2).coerceIn(0.0, 1.0)
        val x = ax + t * dx; val y = ay + t * dy
        return Pair(sqrt(x * x + y * y), t)
    }

    /** Giai ma polyline Google/OSRM (do chinh xac 1e5 / 1e6) -> [lat0, lon0, lat1, lon1...]. */
    fun polyline(s: String, f: Double): DoubleArray {
        val out = ArrayList<Double>()
        var i = 0; var lat = 0L; var lon = 0L
        while (i < s.length) {
            for (k in 0..1) {
                var res = 0L; var sh = 0
                while (true) {
                    val b = s[i++].code - 63
                    res = res or ((b and 0x1f).toLong() shl sh); sh += 5
                    if (b < 0x20) break
                }
                val v = if (res and 1L != 0L) (res shr 1).inv() else res shr 1
                if (k == 0) lat += v else lon += v
            }
            out += lat / f; out += lon / f
        }
        return out.toDoubleArray()
    }

    /** Giai ma HERE Flexible Polyline -> [lat0, lon0, ...] (bo chieu thu 3 neu co). */
    fun flexPolyline(s: String): DoubleArray {
        val tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
        var i = 0
        fun uint(): Long {
            var res = 0L; var sh = 0
            while (true) {
                val v = tbl.indexOf(s[i++]).toLong()
                res = res or ((v and 0x1f) shl sh)
                if (v and 0x20 == 0L) return res
                sh += 5
            }
        }
        fun sint(): Long { val u = uint(); return if (u and 1L != 0L) (u shr 1).inv() else u shr 1 }
        if (uint() != 1L) throw Exception("polyline lạ")
        val h = uint()
        val prec = (h and 15).toInt(); val third = ((h shr 4) and 7).toInt()
        val f = Math.pow(10.0, prec.toDouble())
        val out = ArrayList<Double>()
        var lat = 0L; var lon = 0L
        while (i < s.length) {
            lat += sint(); lon += sint()
            if (third != 0) sint()
            out += lat / f; out += lon / f
        }
        return out.toDoubleArray()
    }

    /** Huong (do, 0 = bac) tu A toi B. */
    fun bearing(a: Double, b: Double, c: Double, d: Double): Double {
        val p1 = Math.toRadians(a); val p2 = Math.toRadians(c); val dl = Math.toRadians(d - b)
        val y = sin(dl) * cos(p2); val x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)
        return (Math.toDegrees(atan2(y, x)) + 360) % 360
    }

}
