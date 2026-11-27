package com.evcarhud.adas

import android.content.Context
import android.graphics.Bitmap
import org.json.JSONObject
import java.io.File
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sqrt

/**
 * CSDL bien tren may (buoc 1, chua gui may chu): moi bien = loai, so (bien toc do), toa do, huong xe, so lan thay, anh cat.
 * Gop: cung loai (va cung so), cach < 30 m, cung chieu (lech huong < 45 do) -> tang so lan thay.
 * File: filesDir/signs/db.jsonl (1 dong/bien, ghi lai ca file khi doi) + anh signs/<id>.jpg (+ <id>-phu.jpg).
 */
class SignDb(ctx: Context) {
    class Rec(val id: String, val cls: Int, val value: Int?, var lat: Double, var lon: Double, val brg: Float, var seen: Int, var last: Long, var aux: Boolean)

    private val dir = File(ctx.filesDir, "signs").apply { mkdirs() }
    private val file = File(dir, "db.jsonl")
    private val recs = ArrayList<Rec>()
    private var dirty = false
    val size get() = recs.size

    init {
        try {
            if (file.exists()) file.forEachLine { l ->
                val j = JSONObject(l)
                recs += Rec(j.getString("id"), j.getInt("c"), if (j.has("v")) j.getInt("v") else null, j.getDouble("lat"), j.getDouble("lon"),
                    j.getDouble("b").toFloat(), j.getInt("n"), j.getLong("t"), j.optBoolean("phu"))
            }
        } catch (_: Exception) {}
    }

    /** Ghi nhan 1 lan thay bien tai vi tri xe (lat/lon cua xe, du lech vai chuc met so voi bien). */
    @Synchronized
    fun add(cls: Int, value: Int?, lat: Double, lon: Double, brg: Float, crop: Bitmap, aux: Bitmap?): Rec? {
        if (lat == 0.0 && lon == 0.0) return null
        val now = System.currentTimeMillis()
        val r = recs.firstOrNull { it.cls == cls && it.value == value && dist(it.lat, it.lon, lat, lon) < 30 && angle(it.brg, brg) < 45 }
        if (r != null) {
            if (now - r.last > 20_000) r.seen++   // lan thay moi (khong tinh cac khung lien tiep cua cung 1 lan di qua)
            r.last = now
            if (aux != null && !r.aux) { save(aux, "${r.id}-phu"); r.aux = true }
            dirty = true
            return r
        }
        val id = "%x%04x".format(now / 1000, (Math.random() * 0xffff).toInt())
        val n = Rec(id, cls, value, lat, lon, brg, 1, now, aux != null)
        save(crop, id); aux?.let { save(it, "$id-phu") }
        recs += n; dirty = true
        return n
    }

    private fun save(b: Bitmap, name: String) {
        try { File(dir, "$name.jpg").outputStream().use { b.compress(Bitmap.CompressFormat.JPEG, 80, it) } } catch (_: Exception) {}
    }

    @Synchronized
    fun flush() {
        if (!dirty) return
        try {
            val tmp = File(dir, "db.tmp")
            tmp.bufferedWriter().use { w ->
                for (r in recs) {
                    val j = JSONObject().put("id", r.id).put("c", r.cls).put("lat", r.lat).put("lon", r.lon).put("b", r.brg.toDouble())
                        .put("n", r.seen).put("t", r.last).put("phu", r.aux)
                    r.value?.let { j.put("v", it) }
                    w.write(j.toString()); w.write("\n")
                }
            }
            tmp.renameTo(file); dirty = false
        } catch (_: Exception) {}
    }

    private fun dist(a1: Double, o1: Double, a2: Double, o2: Double): Double {
        val dy = (a2 - a1) * 111_320.0; val dx = (o2 - o1) * 111_320.0 * cos(Math.toRadians(a1))
        return sqrt(dx * dx + dy * dy)
    }

    private fun angle(a: Float, b: Float): Float { val d = abs(a - b) % 360f; return if (d > 180f) 360f - d else d }
}
