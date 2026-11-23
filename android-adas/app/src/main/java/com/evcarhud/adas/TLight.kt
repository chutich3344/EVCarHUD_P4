package com.evcarhud.adas

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import java.nio.ByteBuffer
import java.nio.FloatBuffer
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Den giao thong + bang dem giay (23/11).
 * - Tim hop den bang YOLOX-nano COCO (lop 9 "traffic light", dung lai assets/xe-coco.onnx, khong them mo hinh)
 *   CHI tren vung giua-tren khung (22..78% ngang x 0..50% cao), dau vao 704x352, chay thua (AdasService quyet dinh nhip).
 * - Mau den: dem diem sang + bao hoa trong hop theo dai mau HSV (do / vang / xanh).
 *   Chong den duong, den pha: den giao thong la cham sang NHO trong vo hop TOI.
 * - So dem giay: LED 7 doan cung mau den, nam ben phai / trai / duoi hop den, hoac YOLO bat nham chinh bang dem
 *   thanh "den" -> doc ben trong hop. Phong to vung, tach chu so theo vo hop den (2 o co dinh), doc 7 doan bang dai quet.
 * Thuat toan doi chieu tren clip 70mai that (Test06: xanh 31 -> 30 -> 29 doc dung 9/10 lan quet, lan sai bi bo loc thoi gian chan).
 * Bo loc thoi gian (mau can 2 lan khop, so giay dem lui theo dong ho) nam o [Track].
 */
class TLight(ctx: Context, private val onRes: (Res) -> Unit) {
    /** Toa do chuan hoa 0..1 theo anh hien thi. cd* = khung bang dem (neu doc duoc). */
    class Lamp(val col: Char, val conf: Float, val x1: Float, val y1: Float, val x2: Float, val y2: Float,
               val sec: Int?, val cx1: Float = 0f, val cy1: Float = 0f, val cx2: Float = 0f, val cy2: Float = 0f)
    /** Ket qua 1 lan quet: mau den chinh (R/Y/G, null = khong thay), so giay doc duoc (null = khong co/khong doc duoc). */
    class Res(val col: Char?, val sec: Int?, val lamps: List<Lamp>, val ms: Long)

    private val th = HandlerThread("tlight").apply { start() }
    private val h = Handler(th.looper)
    private val env = OrtEnvironment.getEnvironment()
    private var sess: OrtSession? = null
    @Volatile private var busy = false
    private var lastAt = 0L
    @Volatile var lastMs = 0L
    @Volatile var info = "đang nạp"
    private var yA = ByteArray(0); private var uA = ByteArray(0); private var vA = ByteArray(0)
    private var yRow = 0; private var uvRow = 0; private var uvPix = 1; private var w = 0; private var hh = 0; private var flip = false
    private val inp = FloatBuffer.allocate(3 * IW * IH)

    init {
        h.post {
            try {
                val o = try {
                    OrtSession.SessionOptions().apply { setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT); setIntraOpNumThreads(1); addXnnpack(mapOf("intra_op_num_threads" to "2")) }
                } catch (e: Throwable) {
                    OrtSession.SessionOptions().apply { setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT); setIntraOpNumThreads(2) }
                }
                sess = env.createSession(ctx.assets.open("xe-coco.onnx").use { it.readBytes() }, o)
                info = "sẵn sàng"
            } catch (e: Throwable) { info = "lỗi nạp: ${e.message}"; Log.e(TAG, "load", e) }
        }
    }

    /** Goi tu onFrame (truoc khi dong khung). Chep anh roi chay tren luong rieng; toi da 1 lan / periodMs. */
    fun offer(im: YuvFrame, flip: Boolean, periodMs: Long) {
        val now = SystemClock.elapsedRealtime()
        if (busy || sess == null || now - lastAt < periodMs) return
        try {
            val p = im.planes
            yA = cp(p[0].buffer, yA); uA = cp(p[1].buffer, uA); vA = cp(p[2].buffer, vA)
            yRow = p[0].rowStride; uvRow = p[1].rowStride; uvPix = p[1].pixelStride; w = im.width; hh = im.height; this.flip = flip
        } catch (e: Exception) { return }
        lastAt = now; busy = true
        h.post { try { run() } catch (e: Throwable) { Log.w(TAG, "run", e) } finally { busy = false } }
    }

    private fun cp(b: ByteBuffer, old: ByteArray): ByteArray {
        val d = b.duplicate(); d.rewind(); val n = d.remaining()
        val a = if (old.size == n) old else ByteArray(n); d.get(a, 0, n); return a
    }

    private fun rgb(x0: Int, y0: Int): Int {
        val x = (if (flip) w - 1 - x0 else x0).coerceIn(0, w - 1); val y = (if (flip) hh - 1 - y0 else y0).coerceIn(0, hh - 1)
        val yy = yA.getOrElse(y * yRow + x) { 0 }.toInt() and 255
        val ui = (y / 2) * uvRow + (x / 2) * uvPix
        val u = (uA.getOrElse(ui) { 128.toByte() }.toInt() and 255) - 128; val v = (vA.getOrElse(ui) { 128.toByte() }.toInt() and 255) - 128
        val r = (yy + 1.402f * v).toInt().coerceIn(0, 255); val g = (yy - 0.344f * u - 0.714f * v).toInt().coerceIn(0, 255); val b = (yy + 1.772f * u).toInt().coerceIn(0, 255)
        return (r shl 16) or (g shl 8) or b
    }

    /** Lay mau song tuyen (de phong to vung bang dem cho net nhu cv2 INTER_CUBIC/LINEAR). */
    private fun rgbF(fx: Float, fy: Float): Int {
        val x0 = fx.toInt(); val y0 = fy.toInt(); val ax = fx - x0; val ay = fy - y0
        val c00 = rgb(x0, y0); val c10 = rgb(x0 + 1, y0); val c01 = rgb(x0, y0 + 1); val c11 = rgb(x0 + 1, y0 + 1)
        fun ch(s: Int): Int {
            val a = (c00 shr s) and 255; val b = (c10 shr s) and 255; val c = (c01 shr s) and 255; val d = (c11 shr s) and 255
            return ((a * (1 - ax) + b * ax) * (1 - ay) + (c * (1 - ax) + d * ax) * ay).roundToInt().coerceIn(0, 255)
        }
        return (ch(16) shl 16) or (ch(8) shl 8) or ch(0)
    }

    // ---------------- 1 lan quet ----------------
    private fun run() {
        val s = sess ?: return
        val t0 = SystemClock.elapsedRealtime()
        val x0 = (RX0 * w).toInt(); val x1 = (RX1 * w).toInt(); val y1 = (RY1 * hh).toInt()
        val sx = (x1 - x0).toFloat() / IW; val sy = y1.toFloat() / IH
        val arr = inp.array(); val pl = IW * IH
        for (iy in 0 until IH) {
            val py = ((iy + 0.5f) * sy).toInt()
            for (ix in 0 until IW) {
                val c = rgb(x0 + ((ix + 0.5f) * sx).toInt(), py); val o = iy * IW + ix
                arr[o] = (c and 255).toFloat(); arr[pl + o] = ((c shr 8) and 255).toFloat(); arr[2 * pl + o] = ((c shr 16) and 255).toFloat()
            }
        }
        inp.clear()
        val out = OnnxTensor.createTensor(env, inp, longArrayOf(1, 3, IH.toLong(), IW.toLong())).use { t ->
            s.run(mapOf("images" to t)).use { r -> (r.get(0) as OnnxTensor).floatBuffer.let { fb -> FloatArray(fb.remaining()).also { fb.get(it) } } }
        }
        // out [1, N, 85]: dx, dy, log w, log h, obj, 80 lop; luoi stride 8/16/32
        val cand = ArrayList<FloatArray>()
        var a = 0
        for (st in intArrayOf(8, 16, 32)) {
            val nx = IW / st; val ny = IH / st
            for (gy in 0 until ny) for (gx in 0 until nx) {
                val b = a * 85; a++
                if (b + 85 > out.size) break
                val sc = out[b + 4] * out[b + 5 + 9]
                if (sc < CONF) continue
                val cx = (out[b] + gx) * st; val cy = (out[b + 1] + gy) * st
                val bw = exp(out[b + 2]) * st; val bh = exp(out[b + 3]) * st
                cand += floatArrayOf(x0 + (cx - bw / 2) * sx, (cy - bh / 2) * sy, x0 + (cx + bw / 2) * sx, (cy + bh / 2) * sy, sc)
            }
        }
        cand.sortByDescending { it[4] }
        val keep = ArrayList<FloatArray>()
        for (c in cand) if (keep.none { iou(it, c) > 0.4f }) keep += c

        class L(val b: IntArray, val conf: Float, val col: Char?, val cd: Cd?)
        val lamps = ArrayList<L>(); val cds = ArrayList<L>()
        for (k in keep.take(8)) {
            val b = intArrayOf(max(0, k[0].toInt()), max(0, k[1].toInt()), min(w, k[2].toInt()), min(hh, k[3].toInt()))
            if (b[2] - b[0] < 4 || b[3] - b[1] < 4) continue
            if ((lamps + cds).any { inside(b, it.b, 0.6f) }) continue
            var col = lampColor(b)
            val bw = b[2] - b[0]; val bh = b[3] - b[1]
            if (col == null) { lamps += L(b, k[4], null, null); continue }
            val own = if (bw >= 0.9f * bh && bw <= 1.7f * bh) countdown(b, col, true) else null
            if (own != null && own.v in 10..99) { cds += L(b, k[4], col, own); continue }   // hop nay chinh la bang dem
            lamps += L(b, k[4], col, countdown(b, col, false))
        }
        val all = lamps + cds
        val lampsF = lamps.filter { l -> all.none { o -> o !== l && o.cd != null && inside(l.b, o.cd.box, 0.4f) } }
        val lit = lampsF.filter { it.col != null }
        var col: Char? = null; var sec: Int? = null
        if (lit.isNotEmpty()) {
            val m = lit.maxByOrNull { (it.b[2] - it.b[0]) * (it.b[3] - it.b[1]) * it.conf }!!
            col = m.col
            sec = m.cd?.v ?: cds.firstOrNull { it.col == m.col }?.cd?.v ?: lit.firstOrNull { it.col == m.col && it.cd != null }?.cd?.v
        }
        val fw = w.toFloat(); val fh = hh.toFloat()
        val res = (lit + cds).map { l ->
            val c = l.cd
            Lamp(l.col!!, l.conf, l.b[0] / fw, l.b[1] / fh, l.b[2] / fw, l.b[3] / fh, c?.v,
                (c?.box?.get(0) ?: 0) / fw, (c?.box?.get(1) ?: 0) / fh, (c?.box?.get(2) ?: 0) / fw, (c?.box?.get(3) ?: 0) / fh)
        }
        lastMs = SystemClock.elapsedRealtime() - t0
        info = "${lastMs} ms · ${lit.size} đèn${if (col != null) " $col${sec ?: ""}" else ""}"
        onRes(Res(col, sec, res, lastMs))
    }

    // ---------------- mau ----------------
    /** HSV kieu OpenCV: h 0..179, s 0..255, v 0..255. Tra ve h shl 16 | s shl 8 | v. */
    private fun hsv(c: Int): Int {
        val r = (c shr 16) and 255; val g = (c shr 8) and 255; val b = c and 255
        val mx = max(r, max(g, b)); val mn = min(r, min(g, b)); val d = mx - mn
        val s = if (mx == 0) 0 else d * 255 / mx
        var hdeg = when { d == 0 -> 0f; mx == r -> 60f * (g - b) / d; mx == g -> 120f + 60f * (b - r) / d; else -> 240f + 60f * (r - g) / d }
        if (hdeg < 0) hdeg += 360f
        return ((hdeg / 2f).toInt().coerceIn(0, 179) shl 16) or (s shl 8) or mx
    }

    /** 'R' / 'Y' / 'G' / null theo diem sang + bao hoa. */
    private fun colOf(hv: Int, vmin: Int, smin: Int): Char? {
        val hh0 = hv shr 16; val s = (hv shr 8) and 255; val v = hv and 255
        if (v <= vmin || s <= smin) return null
        return when { hh0 < 12 || hh0 > 165 -> 'R'; hh0 < 35 -> 'Y'; hh0 in 45..96 -> 'G'; else -> null }
    }

    private fun lampColor(b: IntArray): Char? {
        var r = 0; var y = 0; var g = 0; var bright = 0; var dark = 0; var n = 0
        for (yy in b[1] until b[3]) for (xx in b[0] until b[2]) {
            val hv = hsv(rgb(xx, yy)); n++
            val v = hv and 255
            if (v > 150) bright++; if (v < 90) dark++
            when (colOf(hv, 150, 90)) { 'R' -> r++; 'Y' -> y++; 'G' -> g++; else -> {} }
        }
        val best = maxOf(r, y, g)
        if (best < 3 || n == 0) return null
        val col = if (best == r) 'R' else if (best == g) 'G' else 'Y'
        // den duong / den pha / troi sang: khong co vo toi hoac vung sang qua lon
        if (bright > 0.45f * n || dark < 0.25f * n || best > 0.4f * n) return null
        return col
    }

    // ---------------- bang dem giay ----------------
    class Cd(val v: Int, val box: IntArray)

    /** inner=true: doc ngay trong hop (YOLO bat nham bang dem thanh den); false: tim ben phai / trai / duoi hop den. */
    private fun countdown(b: IntArray, col: Char, inner: Boolean): Cd? {
        val bw = b[2] - b[0]; val bh = b[3] - b[1]
        val sides = if (inner) "I" else "RLB"
        for (side in sides) {
            var s = min(bw, bh).toFloat()
            val r = when (side) {
                'R' -> floatArrayOf(b[2].toFloat(), b[2] + 3 * s, b[1] - 0.5f * s, b[3] + 0.5f * s)
                'L' -> floatArrayOf(b[0] - 3 * s, b[0].toFloat(), b[1] - 0.5f * s, b[3] + 0.5f * s)
                'B' -> floatArrayOf(b[0] - 0.5f * s, b[2] + 0.5f * s, b[3].toFloat(), b[3] + 2.5f * s)
                else -> { s = bh.toFloat(); floatArrayOf(b[0] - 0.15f * bw, b[2] + 0.15f * bw, b[1] - 0.15f * bh, b[3] + 0.15f * bh) }
            }
            val rx0 = max(0, r[0].toInt()); val rx1 = min(w, r[1].toInt()); val ry0 = max(0, r[2].toInt()); val ry1 = min(hh, r[3].toInt())
            if (rx1 - rx0 < 4 || ry1 - ry0 < 4) continue
            for (k in intArrayOf(max(2, (72f / max(8f, s)).roundToInt()), max(1, (48f / max(8f, s)).roundToInt()))) {
                val c = cd1(rx0, ry0, rx1, ry1, k, col, s) ?: continue
                return Cd(c.first, intArrayOf(rx0 + c.second[0] / k, ry0 + c.second[1] / k, rx0 + c.second[2] / k, ry0 + c.second[3] / k))
            }
        }
        return null
    }

    private fun cd1(rx0: Int, ry0: Int, rx1: Int, ry1: Int, k: Int, col: Char, s: Float): Pair<Int, IntArray>? {
        val W = (rx1 - rx0) * k; val H = (ry1 - ry0) * k
        if (W * H > 160_000) return null
        val vv = IntArray(W * H); val m = BooleanArray(W * H)
        for (j in 0 until H) for (i in 0 until W) {
            val hv = hsv(rgbF(rx0 + (i + 0.5f) / k - 0.5f, ry0 + (j + 0.5f) / k - 0.5f))
            vv[j * W + i] = hv and 255; m[j * W + i] = colOf(hv, 95, 60) == col
        }
        val mc = close3(m, W, H)
        val cc = comps(mc, W, H).filter { it[4] >= 6 }
        if (cc.isEmpty()) return null
        val anc = cc.maxByOrNull { it[3] }!!; val hmax = anc[3]
        if (hmax < 0.35f * s * k || hmax > 2.2f * s * k) return null
        val ay0 = anc[1]; val ay1 = anc[1] + anc[3]; val acx = anc[0] + anc[2] / 2f
        val tall = cc.filter { c -> min(ay1, c[1] + c[3]) - max(ay0, c[1]) > 0.3f * min(hmax, c[3]) && abs(c[0] + c[2] / 2f - acx) < 1.3f * hmax && c[3] <= 1.15f * hmax }
        val X0 = tall.minOf { it[0] }; val Y0 = tall.minOf { it[1] }; val X1 = tall.maxOf { it[0] + it[2] }; val Y1 = tall.maxOf { it[1] + it[3] }
        val dw = X1 - X0; val dh = Y1 - Y0
        if (dw < 2 || dh < 7) return null
        // vo hop den cua bang dem -> diem chia doi (2 o chu so co dinh)
        val dark = BooleanArray(W * H) { vv[it] < 90 || mc[it] }
        val kk = max(3, (0.35f * dh).toInt())
        val dOpen = open(dark, W, H, kk)
        val lab = IntArray(W * H); val dcs = comps(dOpen, W, H, lab)
        val L = lab[((Y0 + Y1) / 2) * W + (X0 + X1) / 2]
        var hx = -1f
        if (L > 0) {
            val d = dcs[L - 1]
            if (d[2] >= 0.9f * d[3] && d[2] <= 2.4f * d[3] && d[3] >= 0.9f * dh && d[3] <= 2f * dh && d[0] <= X0 + 2 && d[0] + d[2] >= X1 - 2) hx = d[0] + d[2] / 2f - X0
        }
        if (hx < 0) {
            // khong tach duoc vo: chi nhan neu nen quanh chu so toi
            val g = max(1, dh / 6); var sum = 0L; var n = 0
            for (j in max(0, Y0 - g) until min(H, Y1 + g)) for (i in max(0, X0 - g) until min(W, X1 + g)) {
                if (j in Y0 until Y1 && i in X0 until X1) continue
                if (near(mc, W, H, i, j)) continue
                sum += vv[j * W + i]; n++
            }
            if (n == 0 || sum / n > 110) return null
        }
        val sp = when {
            hx > 0.2f * dw && hx < 0.95f * dw -> hx.roundToInt()
            hx < 0 && dw > 0.95f * dh -> { // khe it diem nhat o 30..70%
                var best = -1; var bv = Int.MAX_VALUE
                for (i in (0.3f * dw).toInt()..(0.7f * dw).toInt()) { var c = 0; for (j in Y0 until Y1) if (mc[j * W + X0 + i]) c++; if (c < bv) { bv = c; best = i } }
                best
            }
            else -> -1
        }
        val parts = if (sp > 0) listOf(X0 to X0 + sp, X0 + sp to X1) else listOf(X0 to X1)
        val digs = ArrayList<Int>()
        for ((a0, a1) in parts) {
            var c0 = -1; var c1 = -1
            for (i in a0 until a1) { var any = false; for (j in Y0 until Y1) if (mc[j * W + i]) { any = true; break }; if (any) { if (c0 < 0) c0 = i; c1 = i } }
            if (c0 < 0) continue
            val qw = c1 - c0 + 1
            var on = 0; for (j in Y0 until Y1) for (i in c0..c1) if (mc[j * W + i]) on++
            if (qw > 0.38f * dh && on > 0.72f * qw * dh) return null   // khoi dac (quang sang) khong phai 7 doan
            digs += read7(mc, W, c0, Y0, qw, dh) ?: return null
        }
        if (digs.isEmpty()) return null
        return digs.joinToString("").toInt() to intArrayOf(X0, Y0, X1, Y1)
    }

    /** Doc 1 chu so 7 doan trong o (x0, y0, W, H) cua mat na m (rong mw): dai quet ngang a, g, d; doc f, b, e, c. */
    private fun read7(m: BooleanArray, mw: Int, x0: Int, y0: Int, W: Int, H: Int): Int? {
        if (H < 7) return null
        if (W < 0.38f * H) return 1
        fun px(i: Int, j: Int) = m[(y0 + j) * mw + x0 + i]
        fun hband(fy: Float): Boolean {
            val r0 = max(0, ((fy - 0.13f) * H).toInt()); val r1 = min(H, ((fy + 0.13f) * H).toInt() + 1)
            val c0 = (0.25f * W).toInt(); val c1 = min(W, (0.75f * W).toInt() + 1)
            if (r1 <= r0 || c1 <= c0) return false
            var hit = 0; for (i in c0 until c1) { for (j in r0 until r1) if (px(i, j)) { hit++; break } }
            return hit > 0.5f * (c1 - c0)
        }
        fun vband(fx: Float, top: Boolean): Boolean {
            val c0 = max(0, ((fx - 0.2f) * W).toInt()); val c1 = min(W, ((fx + 0.2f) * W).toInt() + 1)
            val r0 = if (top) (0.15f * H).toInt() else (0.58f * H).toInt(); val r1 = if (top) (0.42f * H).toInt() + 1 else (0.85f * H).toInt() + 1
            if (r1 <= r0 || c1 <= c0) return false
            var hit = 0; for (j in r0 until min(H, r1)) { for (i in c0 until c1) if (px(i, j)) { hit++; break } }
            return hit > 0.5f * (min(H, r1) - r0)
        }
        val seg = booleanArrayOf(hband(0.08f), vband(0.88f, true), vband(0.88f, false), hband(0.92f), vband(0.12f, false), vband(0.12f, true), hband(0.5f))
        var bits = 0; for (i in 0 until 7) if (seg[i]) bits = bits or (1 shl i)
        return DIG[bits]
    }

    // ---------------- anh nhi phan ----------------
    private fun near(m: BooleanArray, w0: Int, h0: Int, i: Int, j: Int): Boolean {
        for (dj in -1..1) for (di in -1..1) { val x = i + di; val y = j + dj; if (x in 0 until w0 && y in 0 until h0 && m[y * w0 + x]) return true }
        return false
    }
    private fun dil(m: BooleanArray, w0: Int, h0: Int, r: Int): BooleanArray {   // vuong (2r+1), tach 2 chieu
        val t = BooleanArray(m.size); val o = BooleanArray(m.size)
        for (j in 0 until h0) for (i in 0 until w0) { var v = false; for (d in -r..r) { val x = i + d; if (x in 0 until w0 && m[j * w0 + x]) { v = true; break } }; t[j * w0 + i] = v }
        for (j in 0 until h0) for (i in 0 until w0) { var v = false; for (d in -r..r) { val y = j + d; if (y in 0 until h0 && t[y * w0 + i]) { v = true; break } }; o[j * w0 + i] = v }
        return o
    }
    private fun ero(m: BooleanArray, w0: Int, h0: Int, r: Int): BooleanArray {
        val inv = BooleanArray(m.size) { !m[it] }; val d = dil(inv, w0, h0, r); return BooleanArray(m.size) { !d[it] }
    }
    private fun close3(m: BooleanArray, w0: Int, h0: Int) = ero(dil(m, w0, h0, 1), w0, h0, 1)
    private fun open(m: BooleanArray, w0: Int, h0: Int, k: Int): BooleanArray { val r = max(1, k / 2); return dil(ero(m, w0, h0, r), w0, h0, r) }

    /** Thanh phan lien thong (4 lan can): moi phan tu [x, y, w, h, so diem]; lab (neu co) = chi so + 1. */
    private fun comps(m: BooleanArray, w0: Int, h0: Int, lab: IntArray = IntArray(m.size)): List<IntArray> {
        val out = ArrayList<IntArray>(); val q = IntArray(m.size)
        for (p0 in m.indices) {
            if (!m[p0] || lab[p0] != 0) continue
            val id = out.size + 1; var qh = 0; var qt = 0; q[qt++] = p0; lab[p0] = id
            var xa = w0; var ya = h0; var xb = -1; var yb = -1; var n = 0
            while (qh < qt) {
                val p = q[qh++]; val x = p % w0; val y = p / w0; n++
                if (x < xa) xa = x; if (x > xb) xb = x; if (y < ya) ya = y; if (y > yb) yb = y
                if (x > 0 && m[p - 1] && lab[p - 1] == 0) { lab[p - 1] = id; q[qt++] = p - 1 }
                if (x < w0 - 1 && m[p + 1] && lab[p + 1] == 0) { lab[p + 1] = id; q[qt++] = p + 1 }
                if (y > 0 && m[p - w0] && lab[p - w0] == 0) { lab[p - w0] = id; q[qt++] = p - w0 }
                if (y < h0 - 1 && m[p + w0] && lab[p + w0] == 0) { lab[p + w0] = id; q[qt++] = p + w0 }
            }
            out += intArrayOf(xa, ya, xb - xa + 1, yb - ya + 1, n)
        }
        return out
    }

    private fun inside(b: IntArray, o: IntArray, th: Float): Boolean {
        val ix = max(0, min(b[2], o[2]) - max(b[0], o[0])); val iy = max(0, min(b[3], o[3]) - max(b[1], o[1]))
        return ix * iy > th * max(1, (b[2] - b[0]) * (b[3] - b[1]))
    }
    private fun iou(a: FloatArray, b: FloatArray): Float {
        val ix = max(0f, min(a[2], b[2]) - max(a[0], b[0])); val iy = max(0f, min(a[3], b[3]) - max(a[1], b[1]))
        val i = ix * iy; val u = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - i
        return if (u > 0) i / u else 0f
    }

    fun release() { h.post { try { sess?.close() } catch (_: Exception) {}; sess = null; th.quitSafely() } }

    /**
     * Bo loc thoi gian (luong main): mau moi phai khop 2 lan quet lien moi doi; so giay tu dem lui theo dong ho,
     * so doc moi chi nhan khi khop du doan +-2 s, hoac 2 lan doc lien nhau tu khop nhau. Mat den > 3 s -> xoa.
     */
    class Track {
        var col: Char? = null; private set
        private var cand: Char? = null; private var candN = 0
        private var sec = -1; private var secAt = 0L
        private var pendV = -1; private var pendAt = 0L
        var seenAt = 0L; private set
        var lamps: List<Lamp> = emptyList(); private set
        /** Thoi diem doi mau gan nhat (de keu "den xanh roi" khi R -> G). */
        var changedAt = 0L; private set
        var prevCol: Char? = null; private set

        fun update(r: Res, now: Long) {
            if (r.col == null) { if (now - seenAt > 3000) { col = null; sec = -1; lamps = emptyList() }; return }
            seenAt = now; lamps = r.lamps
            if (r.col != col) {
                if (cand == r.col) candN++ else { cand = r.col; candN = 1 }
                if (candN >= 2 || col == null) { prevCol = col; col = r.col; changedAt = now; sec = -1; pendV = -1 } else return
            }
            val v = r.sec ?: return
            val pred = cur(now)
            when {
                pred == null || abs(pred - v) <= 2 -> { sec = v; secAt = now; pendV = -1 }
                pendV >= 0 && abs(pendV - ((now - pendAt) / 1000).toInt() - v) <= 1 -> { sec = v; secAt = now; pendV = -1 }
                else -> { pendV = v; pendAt = now }
            }
        }
        /** So giay hien tai (dem lui theo dong ho), null = khong co / da het. */
        fun cur(now: Long): Int? { if (sec < 0) return null; val v = sec - ((now - secAt) / 1000).toInt(); return if (v >= 0) v else null }
        fun clear() { col = null; sec = -1; lamps = emptyList(); cand = null; candN = 0 }
    }

    companion object {
        private const val TAG = "TLight"
        private const val IW = 704
        private const val IH = 352
        private const val CONF = 0.25f
        private const val RX0 = 0.22f
        private const val RX1 = 0.78f
        private const val RY1 = 0.5f
        /** Bit a..g (0..6) -> chu so. */
        private val DIG = mapOf(0b0111111 to 0, 0b0000110 to 1, 0b1011011 to 2, 0b1001111 to 3, 0b1100110 to 4, 0b1101101 to 5,
            0b1111101 to 6, 0b0000111 to 7, 0b0100111 to 7, 0b1111111 to 8, 0b1101111 to 9, 0b1100111 to 9)
    }
}
