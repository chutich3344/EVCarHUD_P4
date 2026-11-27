package com.evcarhud.adas

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * Doc so tren bien "toc do toi da" (P.127): tim nen trang trong vong do, tach tung chu so den,
 * so khop mau 0-9 (assets/digits.bin, 4 phong chu x 3 do day), rang buoc so hop le (x0 hoac 1x0).
 * Dau vao: anh 96x96 ARGB cat quanh bien. Ban Python doi chieu: tools/num.py.
 */
object NumRead {
    private const val N = 96
    private const val DW = 16
    private const val DH = 24
    private var tpl: List<Pair<Int, FloatArray>> = emptyList()

    fun load(b: ByteArray) {
        val bb = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        val n = bb.int
        tpl = List(n) { val d = bb.int; d to FloatArray(DW * DH) { bb.float } }
    }

    /** Tra ve (so km/h, do tin cay) hoac (null, 0). */
    fun read(px: IntArray): Pair<Int?, Float> {
        if (tpl.isEmpty() || px.size != N * N) return null to 0f
        val v = IntArray(N * N); val s = IntArray(N * N); val g = IntArray(N * N)
        for (i in px.indices) {
            val c = px[i]; val r = (c shr 16) and 255; val gg = (c shr 8) and 255; val b = c and 255
            val mx = max(r, max(gg, b)); val mn = min(r, min(gg, b))
            v[i] = mx; s[i] = if (mx == 0) 0 else (mx - mn) * 255 / mx
            g[i] = (0.299 * r + 0.587 * gg + 0.114 * b + 0.5).toInt()
        }
        // nguong sang: phan vi 40 cua V
        val hist = IntArray(256); for (x in v) hist[x]++
        var acc = 0; var p40 = 0
        for (k in 0..255) { acc += hist[k]; if (acc >= 0.4 * N * N) { p40 = k; break } }
        val wt = BooleanArray(N * N) { s[it] < 90 && v[it] > p40 }
        val (lab, cnt) = label(wt)
        val area = IntArray(cnt + 1); for (l in lab) if (l > 0) area[l]++
        var best = 0
        for (y in 0 until N) for (x in 0 until N) {
            val l = lab[y * N + x]
            if (l > 0 && (x - 48) * (x - 48) + (y - 48) * (y - 48) < 18 * 18 && (best == 0 || area[l] > area[best])) best = l
        }
        if (best == 0) return null to 0f
        // bao loi cua nen trang -> dia (lap lo chu so), co 2 lan
        val pts = ArrayList<IntArray>()
        for (y in 0 until N) { var a = -1; var b = -1
            for (x in 0 until N) if (lab[y * N + x] == best) { if (a < 0) a = x; b = x }
            if (a >= 0) { pts += intArrayOf(a, y); if (b != a) pts += intArrayOf(b, y) } }
        val hull = hull(pts)
        var disk = BooleanArray(N * N) { inHull(hull, it % N, it / N) }
        repeat(2) { disk = erode(disk) }
        var dn = 0; var gmin = 255; var gmax = 0; var ysum = 0L
        for (i in disk.indices) if (disk[i]) { dn++; gmin = min(gmin, g[i]); gmax = max(gmax, g[i]); ysum += i / N }
        if (dn < 900 || gmax - gmin < 50) return null to 0f
        val cy = ysum.toFloat() / dn
        val gh = IntArray(256); for (i in disk.indices) if (disk[i]) gh[g[i]]++
        val t = otsu(gh, dn)
        val dark = BooleanArray(N * N) { disk[it] && g[it] < t }
        val (dl, dc) = label(dark)
        val st = Array(dc + 1) { intArrayOf(0, N, N, -1, -1, 0) } // area, left, top, right, bottom, ysum
        for (i in dl.indices) { val l = dl[i]; if (l == 0) continue
            val x = i % N; val y = i / N; val a = st[l]
            a[0]++; a[1] = min(a[1], x); a[2] = min(a[2], y); a[3] = max(a[3], x); a[4] = max(a[4], y); a[5] += y }
        val comps = (1..dc).filter { val a = st[it]; a[0] >= 25 && a[4] - a[2] + 1 >= 16 && abs(a[5].toFloat() / a[0] - cy) < 16 }
            .sortedBy { st[it][1] }
        if (comps.size !in 2..3) return null to 0f
        val hs = comps.map { st[it][4] - st[it][2] + 1 }
        if (hs.max() > 1.35f * hs.min()) return null to 0f
        val sc = comps.map { digit(dl, it, st[it]) }
        if (comps.size == 2) {
            if (sc[1][0] < 0.35f) return null to 0f
            val d = (2..9).maxBy { sc[0][it] }
            return d * 10 to (sc[0][d] + sc[1][0]) / 2
        }
        if (sc[0][1] < 0.3f || sc[2][0] < 0.35f) return null to 0f
        val d = (0..2).maxBy { sc[1][it] }
        return 100 + d * 10 to (sc[0][1] + sc[1][d] + sc[2][0]) / 3
    }

    /** Diem tuong quan cao nhat voi tung chu so 0-9. */
    private fun digit(lab: IntArray, l: Int, a: IntArray): FloatArray {
        val x0 = a[1]; val y0 = a[2]; val w = a[3] - x0 + 1; val h = a[4] - y0 + 1
        val src = FloatArray(w * h) { if (lab[(y0 + it / w) * N + x0 + it % w] == l) 1f else 0f }
        val nw = (w * DH.toFloat() / h).roundToInt().coerceIn(1, DW)
        val r = resizeArea(src, w, h, nw, DH)
        val q = FloatArray(DW * DH); val off = (DW - nw) / 2
        for (y in 0 until DH) for (x in 0 until nw) q[y * DW + off + x] = r[y * nw + x]
        val m = q.average().toFloat(); var sd = 0f; for (x in q) sd += (x - m) * (x - m); sd = sqrt(sd / q.size) + 1e-6f
        for (i in q.indices) q[i] = (q[i] - m) / sd
        val best = FloatArray(10) { -9f }
        for ((d, t) in tpl) { var sum = 0f; for (i in q.indices) sum += q[i] * t[i]; val sv = sum / q.size; if (sv > best[d]) best[d] = sv }
        return best
    }

    /** Thu nho kieu INTER_AREA (trung binh theo dien tich); phong to thi noi suy song tuyen. */
    private fun resizeArea(s: FloatArray, w: Int, h: Int, nw: Int, nh: Int): FloatArray {
        val o = FloatArray(nw * nh)
        val fx = w.toFloat() / nw; val fy = h.toFloat() / nh
        for (oy in 0 until nh) for (ox in 0 until nw) {
            if (fx >= 1f && fy >= 1f) {
                val ax = ox * fx; val bx = ax + fx; val ay = oy * fy; val by = ay + fy
                var sum = 0f; var wsum = 0f
                var y = ay.toInt()
                while (y < by && y < h) {
                    val wy = min(by, y + 1f) - max(ay, y.toFloat())
                    var x = ax.toInt()
                    while (x < bx && x < w) { val wx = min(bx, x + 1f) - max(ax, x.toFloat()); sum += s[y * w + x] * wx * wy; wsum += wx * wy; x++ }
                    y++
                }
                o[oy * nw + ox] = if (wsum > 0) sum / wsum else 0f
            } else {
                val sx = ((ox + 0.5f) * fx - 0.5f).coerceIn(0f, (w - 1).toFloat()); val sy = ((oy + 0.5f) * fy - 0.5f).coerceIn(0f, (h - 1).toFloat())
                val x0 = sx.toInt(); val y0 = sy.toInt(); val x1 = min(w - 1, x0 + 1); val y1 = min(h - 1, y0 + 1); val ax = sx - x0; val ay = sy - y0
                o[oy * nw + ox] = (s[y0 * w + x0] * (1 - ax) + s[y0 * w + x1] * ax) * (1 - ay) + (s[y1 * w + x0] * (1 - ax) + s[y1 * w + x1] * ax) * ay
            }
        }
        return o
    }

    private fun otsu(h: IntArray, n: Int): Int {
        var sum = 0.0; for (i in 0..255) sum += i * h[i].toDouble()
        var sb = 0.0; var wb = 0.0; var best = 0.0; var t = 0
        for (i in 0..255) {
            wb += h[i]; if (wb == 0.0) continue
            val wf = n - wb; if (wf == 0.0) break
            sb += i * h[i].toDouble()
            val mb = sb / wb; val mf = (sum - sb) / wf
            val bv = wb * wf * (mb - mf) * (mb - mf)
            if (bv > best) { best = bv; t = i }
        }
        return t   // giong ban Python: dark = g < t
    }

    /** Gan nhan thanh phan lien thong 8 huong. */
    private fun label(m: BooleanArray): Pair<IntArray, Int> {
        val lab = IntArray(N * N); var n = 0; val st = IntArray(N * N)
        for (i in m.indices) {
            if (!m[i] || lab[i] != 0) continue
            n++; var sp = 0; st[sp++] = i; lab[i] = n
            while (sp > 0) {
                val p = st[--sp]; val x = p % N; val y = p / N
                for (dy in -1..1) for (dx in -1..1) {
                    val xx = x + dx; val yy = y + dy
                    if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue
                    val q = yy * N + xx
                    if (m[q] && lab[q] == 0) { lab[q] = n; st[sp++] = q }
                }
            }
        }
        return lab to n
    }

    private fun erode(m: BooleanArray): BooleanArray = BooleanArray(N * N) { i ->
        val x = i % N; val y = i / N
        var ok = true
        for (dy in -1..1) for (dx in -1..1) { val xx = x + dx; val yy = y + dy
            if (xx in 0 until N && yy in 0 until N && !m[yy * N + xx]) ok = false }
        ok
    }

    private fun hull(p: List<IntArray>): List<IntArray> {
        if (p.size < 3) return p
        val s = p.sortedWith(compareBy({ it[0] }, { it[1] }))
        fun cr(o: IntArray, a: IntArray, b: IntArray) = (a[0] - o[0]).toLong() * (b[1] - o[1]) - (a[1] - o[1]).toLong() * (b[0] - o[0])
        val lo = ArrayList<IntArray>(); for (q in s) { while (lo.size >= 2 && cr(lo[lo.size - 2], lo[lo.size - 1], q) <= 0) lo.removeAt(lo.size - 1); lo += q }
        val up = ArrayList<IntArray>(); for (q in s.asReversed()) { while (up.size >= 2 && cr(up[up.size - 2], up[up.size - 1], q) <= 0) up.removeAt(up.size - 1); up += q }
        lo.removeAt(lo.size - 1); up.removeAt(up.size - 1)
        return lo + up
    }

    private fun inHull(h: List<IntArray>, x: Int, y: Int): Boolean {
        if (h.size < 3) return false
        for (i in h.indices) {
            val a = h[i]; val b = h[(i + 1) % h.size]
            if ((b[0] - a[0]).toLong() * (y - a[1]) - (b[1] - a[1]).toLong() * (x - a[0]) < 0) return false
        }
        return true
    }
}
