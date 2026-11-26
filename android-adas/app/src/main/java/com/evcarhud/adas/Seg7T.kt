package com.evcarhud.adas

import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * Doc bang dem 2 chu so bang SO KHOP CA SO 0..99 (24/11: "o den 70 bat co 7").
 * LED do 70mai nhoe/quang lam chan phai so 7 dinh vao so 0 -> tach tung o roi doc 7 doan bi hong.
 * O day ve san mau 0..99 (2 kieu rong, 2 khe, 2 net, 3 do nghieng), cat theo vung net, thu ve 40x28,
 * so tuong quan chuan hoa (NCC) voi vung anh tren kenh mau. Ban Python y het: tools/den/tmpl2.py.
 */
object Seg7T {
    private const val CW = 40
    private const val CH = 28
    private const val N = CW * CH
    private val SEG = arrayOf("abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg")

    @Volatile private var vals: IntArray? = null
    private var asps = FloatArray(0)
    private var mats = FloatArray(0)

    /** Ve mau (chay 1 lan tren luong den, ~1-2 s). */
    @Synchronized fun init() {
        if (vals != null) return
        val v = ArrayList<Int>(); val a = ArrayList<Float>(); val m = ArrayList<FloatArray>()
        for (n in 0..99) for (asp in floatArrayOf(0.42f, 0.55f)) for (gap in floatArrayOf(0.18f, 0.4f))
            for (thk in floatArrayOf(0.10f, 0.17f)) for (sl in floatArrayOf(0f, 0.12f, 0.24f)) {
                val (img, w, h) = render(n, asp, gap, thk, sl)
                val r = cropNorm(img, w, h) ?: continue
                v += n; a += r.second; m += r.first
            }
        val mm = FloatArray(m.size * N); for (i in m.indices) System.arraycopy(m[i], 0, mm, i * N, N)
        asps = a.toFloatArray(); mats = mm; vals = v.toIntArray()
    }

    private fun render(n: Int, asp: Float, gap: Float, thk: Float, sl: Float, S: Int = 48): Triple<FloatArray, Int, Int> {
        val lines = ArrayList<FloatArray>()
        val cells = ArrayList<Pair<Int, Float>>()
        if (n >= 10) cells += (n / 10) to 0f
        cells += (n % 10) to (1f + gap)
        for ((d, xo) in cells) for (s in SEG[d]) {
            val p = when (s) {
                'a' -> floatArrayOf(xo, 0f, xo + 1, 0f); 'b' -> floatArrayOf(xo + 1, 0f, xo + 1, .5f); 'c' -> floatArrayOf(xo + 1, .5f, xo + 1, 1f)
                'd' -> floatArrayOf(xo, 1f, xo + 1, 1f); 'e' -> floatArrayOf(xo, .5f, xo, 1f); 'f' -> floatArrayOf(xo, 0f, xo, .5f)
                else -> floatArrayOf(xo, .5f, xo + 1, .5f)
            }
            lines += floatArrayOf(p[0] * asp, p[1], p[2] * asp, p[3])
        }
        val mg = thk * 2.5f; val W = (2 + gap) * asp + 2 * mg + sl; val H = 1 + 2 * mg
        val w = (W * S).roundToInt(); val h = (H * S).roundToInt()
        val out = FloatArray(w * h); val sg = thk * 0.6f
        for (j in 0 until h) {
            val y = (j + 0.5f) / S - mg
            for (i in 0 until w) {
                val x = (i + 0.5f) / S - mg - sl * (1 - y)   // nghieng: phia tren lech phai
                var d = 9f
                for (l in lines) {
                    val dx = l[2] - l[0]; val dy = l[3] - l[1]; val L = dx * dx + dy * dy
                    val t = (((x - l[0]) * dx + (y - l[1]) * dy) / L).coerceIn(0f, 1f)
                    d = min(d, hypot(x - l[0] - t * dx, y - l[1] - t * dy))
                }
                val e = max(0f, d - thk / 2)
                out[j * w + i] = exp(-e * e / (2 * sg * sg))
            }
        }
        return Triple(out, w, h)
    }

    private fun sample(a: FloatArray, w: Int, h: Int, fx0: Float, fy0: Float): Float {
        val fx = fx0.coerceIn(0f, w - 1.001f); val fy = fy0.coerceIn(0f, h - 1.001f)
        val x0 = fx.toInt(); val y0 = fy.toInt(); val ax = fx - x0; val ay = fy - y0
        val p = y0 * w + x0
        return (a[p] * (1 - ax) + a[p + 1] * ax) * (1 - ay) + (a[p + w] * (1 - ax) + a[p + w + 1] * ax) * ay
    }

    /** Vung net (> giua min..max) -> vector chuan hoa 40x28 + ti le rong/cao vung net. */
    private fun cropNorm(a: FloatArray, w: Int, h: Int): Pair<FloatArray, Float>? {
        var mn = Float.MAX_VALUE; var mx = -Float.MAX_VALUE
        for (v in a) { if (v < mn) mn = v; if (v > mx) mx = v }
        if (mx - mn < 1e-3f) return null
        val t = mn + 0.5f * (mx - mn)
        var x0 = w; var x1 = -1; var y0 = h; var y1 = -1
        for (j in 0 until h) for (i in 0 until w) if (a[j * w + i] > t) { if (i < x0) x0 = i; if (i > x1) x1 = i; if (j < y0) y0 = j; if (j > y1) y1 = j }
        if (x1 < 0 || y1 - y0 + 1 < 3 || x1 - x0 + 1 < 2) return null
        val bw = (x1 - x0 + 1).toFloat(); val bh = (y1 - y0 + 1).toFloat()
        val q = FloatArray(N); val uv = floatArrayOf(1f / 6, .5f, 5f / 6)
        for (j in 0 until CH) for (i in 0 until CW) {
            var s = 0f
            for (u in uv) for (v in uv) s += sample(a, w, h, x0 + (i + u) * bw / CW - .5f, y0 + (j + v) * bh / CH - .5f)
            q[j * CW + i] = s / 9
        }
        var mean = 0f; for (v in q) mean += v; mean /= N
        var ss = 0f; for (i in 0 until N) { q[i] -= mean; ss += q[i] * q[i] }
        val sd = sqrt(ss / N); if (sd < 1e-6f) return null
        for (i in 0 until N) q[i] /= sd
        return q to bw / bh
    }

    /** Co khe doc (nghieng 0/.12/.25) tach so 1 dau khong: tranh doc 8 -> 13, 6 -> 16. */
    private fun hasGap(a: FloatArray, w: Int, h: Int): Boolean {
        var mn = Float.MAX_VALUE; var mx = -Float.MAX_VALUE
        for (v in a) { if (v < mn) mn = v; if (v > mx) mx = v }
        val t = mn + 0.5f * (mx - mn)
        var x0 = w; var x1 = -1; var y0 = h; var y1 = -1
        for (j in 0 until h) for (i in 0 until w) if (a[j * w + i] > t) { if (i < x0) x0 = i; if (i > x1) x1 = i; if (j < y0) y0 = j; if (j > y1) y1 = j }
        if (x1 < 0) return false
        val qw = x1 - x0 + 1; val qh = y1 - y0 + 1
        for (sl in floatArrayOf(0f, 0.12f, 0.25f)) {
            val off = (sl * qh).toInt(); val prof = IntArray(qw + off + 2)
            for (j in 0 until qh) {
                val o = (sl * (qh - 1 - j)).roundToInt()
                for (i in 0 until qw) if (a[(y0 + j) * w + x0 + i] > t) { val k = i - o + off; if (k in prof.indices) prof[k]++ }
            }
            var f = -1; var l = -1; var pm = 0
            for (k in prof.indices) if (prof[k] > 0) { if (f < 0) f = k; l = k; pm = max(pm, prof[k]) }
            if (f < 0) continue
            val W = l - f + 1; val lo = max(1, (0.08f * W).toInt()); val hi = max(lo + 1, (0.5f * W).toInt())
            var m = Int.MAX_VALUE; for (k in lo until hi) m = min(m, prof[f + k])
            if (m <= 0.28f * pm) return true
        }
        return false
    }

    /** a: vung kenh mau (w x h, da cat am ve 0). Tra ve (so, chenh voi so thu 2, diem) hoac null. */
    fun read(a: FloatArray, w: Int, h: Int): Triple<Int, Float, Float>? {
        val vv = vals ?: return null
        if (w < 4 || h < 6) return null
        val (v, ar) = cropNorm(a, w, h) ?: return null
        val gap = hasGap(a, w, h)
        val best = FloatArray(100) { -9f }
        for (t in vv.indices) {
            val n = vv[t]
            if (abs(ln(ar / asps[t])) >= 0.35f) continue
            if (!gap && n in 10..19) continue
            var s = 0f; val o = t * N
            for (i in 0 until N) s += mats[o + i] * v[i]
            s /= N
            if (s > best[n]) best[n] = s
        }
        var b1 = -1; var b2 = -1
        for (n in 0..99) { if (best[n] <= -9f) continue; if (b1 < 0 || best[n] > best[b1]) { b2 = b1; b1 = n } else if (b2 < 0 || best[n] > best[b2]) b2 = n }
        if (b1 < 0 || b2 < 0) return null
        return Triple(b1, best[b1] - best[b2], best[b1])
    }

    /** Doc tin cay (diem >= 0.55, hon so thu 2 >= 0.01) hoac null. */
    fun sure(a: FloatArray, w: Int, h: Int): Int? {
        val r = read(a, w, h) ?: return null
        return if (r.third >= 0.55f && r.second >= 0.01f) r.first else null
    }
}
