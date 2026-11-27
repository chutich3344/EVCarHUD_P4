package com.evcarhud.adas

import android.content.Context
import org.json.JSONObject
import java.io.File
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * CSDL camera tren may: moi lan HUD bao camera (Waze/GOFA, "C1|loai|met"), uoc toa do camera = vi tri xe + met theo huong xe
 * (bo qua duong cong, sai vai chuc met). Gop: cung loai, cach < 80 m -> trung binh toa do, tang so lan.
 * File filesDir/signs/cam.jsonl. Buoc sau: gui len may chu evcarhud.com de dung khi khong noi Waze/GOFA.
 */
class CamDb(ctx: Context) {
    class Rec(val type: Char, var lat: Double, var lon: Double, val brg: Float, var n: Int, var last: Long)

    private val file = File(File(ctx.filesDir, "signs").apply { mkdirs() }, "cam.jsonl")
    private val recs = ArrayList<Rec>()
    private var dirty = false
    val size get() = recs.size

    init {
        try {
            if (file.exists()) file.forEachLine { l ->
                val j = JSONObject(l)
                recs += Rec(j.getString("k")[0], j.getDouble("lat"), j.getDouble("lon"), j.getDouble("b").toFloat(), j.getInt("n"), j.getLong("t"))
            }
        } catch (_: Exception) {}
    }

    @Synchronized
    fun add(type: Char, carLat: Double, carLon: Double, brg: Float, distM: Int) {
        if (carLat == 0.0 && carLon == 0.0 || distM < 0 || distM > 1500) return
        val r = Math.toRadians(brg.toDouble())
        val lat = carLat + distM * cos(r) / 110_540.0
        val lon = carLon + distM * sin(r) / (111_320.0 * cos(Math.toRadians(carLat)))
        val now = System.currentTimeMillis()
        val e = recs.firstOrNull { it.type == type && dist(it.lat, it.lon, lat, lon) < 80 }
        if (e != null) {
            // trung binh co trong so: lan bao o gan (it sai do duong cong) dang tin hon
            val w = if (distM < 200) 0.5 else 0.2
            e.lat += (lat - e.lat) * w; e.lon += (lon - e.lon) * w
            if (now - e.last > 60_000) e.n++
            e.last = now
        } else recs += Rec(type, lat, lon, brg, 1, now)
        dirty = true
    }

    @Synchronized
    fun flush() {
        if (!dirty) return
        try {
            val tmp = File(file.parentFile, "cam.tmp")
            tmp.bufferedWriter().use { w ->
                for (r in recs) {
                    w.write(JSONObject().put("k", r.type.toString()).put("lat", r.lat).put("lon", r.lon).put("b", r.brg.toDouble())
                        .put("n", r.n).put("t", r.last).toString()); w.write("\n")
                }
            }
            tmp.renameTo(file); dirty = false
        } catch (_: Exception) {}
    }

    private fun dist(a1: Double, o1: Double, a2: Double, o2: Double): Double {
        val dy = (a2 - a1) * 111_320.0; val dx = (o2 - o1) * 111_320.0 * cos(Math.toRadians(a1))
        return sqrt(dx * dx + dy * dy)
    }

    companion object {
        fun name(t: Char) = when (t) { 'S' -> "CAM phạt nguội"; 'A' -> "CAM AI"; 'R' -> "CAM đèn đỏ"; else -> "CAM giao thông" }
    }
}
