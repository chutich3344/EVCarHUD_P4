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
               val sec: Int?, val cx1: Float = 0f, val cy1: Float = 0f, val cx2: Float = 0f, val cy2: Float = 0f,
               /** Mui ten sang trong hop: 'U' di thang / 'L' re trai / 'R' re phai (null = khong co), mau mui ten. */
               val arrow: Char? = null, val arrowCol: Char? = null)
    /** Ket qua 1 lan quet: mau den chinh (R/Y/G, null = khong thay), so giay doc duoc (null = khong co/khong doc duoc). */
    class Res(val col: Char?, val sec: Int?, val lamps: List<Lamp>, val ms: Long) {
        /** Huong duoc di theo mui ten XANH trong lan quet nay (vd "U", "UL"). */
        val go: String get() = lamps.filter { it.arrow != null && it.arrowCol == 'G' }.mapNotNull { it.arrow }.distinct().sorted().joinToString("")
    }

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

        class L(val b: IntArray, val conf: Float, val col: Char?, val cd: Cd?, val ar: Pair<Char, Char?>? = null)
        val lamps = ArrayList<L>(); val cds = ArrayList<L>()
        for (k in keep.take(8)) {
            val b = intArrayOf(max(0, k[0].toInt()), max(0, k[1].toInt()), min(w, k[2].toInt()), min(hh, k[3].toInt()))
            if (b[2] - b[0] < 4 || b[3] - b[1] < 4) continue
            if ((lamps + cds).any { inside(b, it.b, 0.6f) }) continue
            var col = lampColor(b)
            val bw = b[2] - b[0]; val bh = b[3] - b[1]
            if (col == null) { lamps += L(b, k[4], null, null); continue }
            val own = if (bw >= 0.9f * bh && bw <= 1.7f * bh) countdown(b, col, true) else null
            val ar = try { arrow(b) } catch (e: Throwable) { null }
            // 24/11: bang dem bi YOLO bat thanh "den" luc con 1 chu so (0..9) cung phai nhan ra (truoc chi nhan 10..99)
            if (ar == null && own != null && own.v in 10..99) { cds += L(b, k[4], col, own); continue }   // hop nay chinh la bang dem
            val ob = if (ar == null && bw >= 0.9f * bh && bw <= 1.7f * bh) readBox(b, col) else null
            if (ob != null && ob >= 10) { cds += L(b, k[4], col, Cd(ob, b)); continue }
            if (ar == null && own != null && own.v in 0..9) { cds += L(b, k[4], col, own); continue }
            lamps += L(b, k[4], col, countdown(b, col, false), ar)
        }
        val all = lamps + cds
        val lampsF = lamps.filter { l -> all.none { o -> o !== l && o.cd != null && inside(l.b, o.cd.box, 0.4f) } }
        val lit = lampsF.filter { it.col != null }
        var col: Char? = null; var sec: Int? = null
        if (lit.isNotEmpty()) {
            // den to + tin cay + gan giua ngang (den 2 mep vung quet thuong cua huong khac) duoc uu tien
            val m = lit.maxByOrNull { val cxn = (it.b[0] + it.b[2]) / 2f / w; (it.b[2] - it.b[0]) * (it.b[3] - it.b[1]) * it.conf * (1f - 1.2f * abs(cxn - 0.5f)) }!!
            col = m.col
            sec = m.cd?.v ?: cds.firstOrNull { it.col == m.col }?.cd?.v ?: lit.firstOrNull { it.col == m.col && it.cd != null }?.cd?.v
        }
        val fw = w.toFloat(); val fh = hh.toFloat()
        val res = (lit + cds).map { l ->
            val c = l.cd
            Lamp(l.col!!, l.conf, l.b[0] / fw, l.b[1] / fh, l.b[2] / fw, l.b[3] / fh, c?.v,
                (c?.box?.get(0) ?: 0) / fw, (c?.box?.get(1) ?: 0) / fh, (c?.box?.get(2) ?: 0) / fw, (c?.box?.get(3) ?: 0) / fh,
                l.ar?.first, l.ar?.second)
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

    // ---------------- mui ten ----------------
    /**
     * Mui ten sang trong hop den (24/11: nga tu den tron DO + mui ten XANH di thang, app chi bao DO).
     * Loi sang (V >= 80% sang nhat) -> dong khe -> thanh phan khong dac (dien tich/khung < 0,62: bong tron chay trang bi loai)
     * nam trong vo toi. Huong: dau mui ten (chu V) rong hon than: tren rong hon duoi = di thang; trai cao hon phai = re trai...
     * Mau: vien quanh loi. Den VN tich hop mui ten xanh tren nen bong do sang: camera chay loi mui ten thanh trang, vien do
     * -> loi trang + dia quanh phan lon do sang thi mui ten la XANH (anh Kevin 24/11 ra dung 'U' xanh).
     */
    private fun arrow(b: IntArray): Pair<Char, Char?>? {
        val bw = b[2] - b[0]; val bh = b[3] - b[1]
        val X0 = max(0, (b[0] - 0.1f * bw).toInt()); val Y0 = max(0, (b[1] - 0.1f * bh).toInt())
        val X1 = min(w, (b[2] + 0.1f * bw).toInt()); val Y1 = min(hh, (b[3] + 0.1f * bh).toInt())
        val k = max(1, (40f / max(8, min(bw, bh))).roundToInt())
        val W = (X1 - X0) * k; val H = (Y1 - Y0) * k
        if (W < 8 || H < 8 || W * H > 120_000) return null
        val hA = IntArray(W * H); val sA = IntArray(W * H); val vA2 = IntArray(W * H)
        var vmax = 0
        for (j in 0 until H) for (i in 0 until W) {
            val hv = hsv(rgbF(X0 + (i + 0.5f) / k - 0.5f, Y0 + (j + 0.5f) / k - 0.5f)); val p = j * W + i
            hA[p] = hv shr 16; sA[p] = (hv shr 8) and 255; vA2[p] = hv and 255; if (vA2[p] > vmax) vmax = vA2[p]
        }
        if (vmax < 150) return null
        val th = max(170f, 0.8f * vmax)
        var core = BooleanArray(W * H) { vA2[it] >= th }
        val kc = max(3, (0.12f * min(bw, bh) * k).toInt() or 1)
        core = ero(dil(core, W, H, kc / 2), W, H, kc / 2)
        val lab = IntArray(W * H); val cc = comps(core, W, H, lab)
        var best: Pair<Char, Char?>? = null; var bestA = 0
        for ((idx, c) in cc.withIndex()) {
            val cx = c[0]; val cy = c[1]; val cw = c[2]; val ch = c[3]; val a = c[4]
            if (a < 10 || max(cw, ch) < 6) continue
            if (a.toFloat() / (cw * ch) > 0.62f) continue   // bong tron / khoi dac
            val id = idx + 1
            fun on(i: Int, j: Int) = lab[(cy + j) * W + cx + i] == id
            val t = max(1, (0.35f * ch).toInt()); val l = max(1, (0.35f * cw).toInt())
            var wTop = 0; var wBot = 0; var hL = 0; var hR = 0
            for (i in 0 until cw) { if ((0 until t).any { on(i, it) }) wTop++; if ((ch - t until ch).any { on(i, it) }) wBot++ }
            for (j in 0 until ch) { if ((0 until l).any { on(it, j) }) hL++; if ((cw - l until cw).any { on(it, j) }) hR++ }
            val d = when {
                ch >= 0.8f * cw && wTop >= 1.5f * max(1, wBot) -> 'U'
                cw >= 0.8f * ch && hL >= 1.5f * max(1, hR) -> 'L'
                cw >= 0.8f * ch && hR >= 1.5f * max(1, hL) -> 'R'
                else -> null
            } ?: continue
            val mm = BooleanArray(W * H) { lab[it] == id }
            val d1 = dil(mm, W, H, k + 1); val d15 = dil(mm, W, H, (3 * k + 3) / 2); val d3 = dil(mm, W, H, 3 * k + 2); val d25 = dil(mm, W, H, (5 * k + 5) / 2)
            // phai nam trong vo toi
            var fs = 0L; var fn = 0
            for (p in mm.indices) if (d3[p] && !d15[p]) { fs += vA2[p]; fn++ }
            if (fn > 0 && fs / fn > 110) continue
            // mau vien + loi
            var nr = 0; var ny = 0; var ng = 0; var nok = 0
            for (p in mm.indices) if (d1[p] && sA[p] > 70 && vA2[p] > 80) {
                nok++; val hh0 = hA[p]
                when { hh0 < 12 || hh0 > 165 -> nr++; hh0 < 35 -> ny++; hh0 in 45..96 -> ng++ }
            }
            val mx = maxOf(nr, ny, ng)
            var col: Char? = if (mx >= max(4, (0.25f * nok).toInt())) (if (mx == ng) 'G' else if (mx == nr) 'R' else 'Y') else null
            if (col == 'R') {
                var dn = 0; var dr = 0; var cs = 0L; var cn = 0
                for (p in mm.indices) if (mm[p]) { cs += sA[p]; cn++ }
                val dIn = dil(mm, W, H, (k + 2) / 2)
                for (p in mm.indices) if (d25[p] && !dIn[p]) { dn++; val hh0 = hA[p]; if (sA[p] > 70 && vA2[p] > 70 && (hh0 < 12 || hh0 > 165)) dr++ }
                if (dn > 0 && dr > 0.45f * dn && cn > 0 && cs / cn < 70) col = 'G'
            }
            if (a > bestA) { bestA = a; best = d to col }
        }
        return best
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
        val vv = IntArray(W * H); val m = BooleanArray(W * H); val chan = FloatArray(W * H)
        for (j in 0 until H) for (i in 0 until W) {
            val c = rgbF(rx0 + (i + 0.5f) / k - 0.5f, ry0 + (j + 0.5f) / k - 0.5f); val hv = hsv(c)
            vv[j * W + i] = hv and 255; m[j * W + i] = colOf(hv, 95, 60) == col; chan[j * W + i] = chanOf(c, col)
        }
        // 24/11 (log 07:07: so do hay doc thanh 8/88): quang LED do lam sang ca doan tat -> chi giu net >= 60% do sang net sang nhat
        val vs = ArrayList<Int>(); for (p in m.indices) if (m[p]) vs += vv[p]
        if (vs.size >= 8) { vs.sort(); val vref = vs[(vs.size * 0.95f).toInt().coerceAtMost(vs.size - 1)]; for (p in m.indices) if (m[p] && vv[p] < 0.6f * vref) m[p] = false }
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
        // 24/11 ("den nguoi ta may chuc giay ma" app chi ra 1 so): so do 70mai nhoe/quang dinh 2 chu so -> doc tung o theo do sang
        // tuong doi tren kenh mau (doan sang hon doan tat), khong phu thuoc nguong mat na; khong duoc moi doc mat na 7 doan
        run {
            val vs2 = parts.map { (a0, a1) -> read7v(chan, W, a0, Y0, a1 - a0, dh) }
            if (vs2.isNotEmpty() && vs2.all { it != null }) return vs2.joinToString("").toInt() to intArrayOf(X0, Y0, X1, Y1)
        }
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

    /** Kenh "do sang theo mau den": do = R - (G+B)/2, xanh = G - R/2, vang = (R+G)/2 - B. */
    private fun chanOf(c: Int, col: Char): Float {
        val r = ((c shr 16) and 255).toFloat(); val g = ((c shr 8) and 255).toFloat(); val b = (c and 255).toFloat()
        return when (col) { 'R' -> r - 0.5f * (g + b); 'G' -> g - 0.5f * r; else -> 0.5f * (r + g) - b }
    }

    /**
     * Doc 1 chu so trong o (x0, y0, W, H) cua anh muc xam g (rong gw) bang do sang TUONG DOI cua 7 dai doan:
     * doan > trung diem (sang nhat + toi nhat)/2 la sang. Chiu duoc quang LED (doan tat cung sang len nhung van toi hon).
     * Sai dung 1 doan thi lay chu so gan nhat neu duy nhat.
     */
    private fun read7v(g: FloatArray, gw: Int, x0: Int, y0: Int, W: Int, H: Int): Int? {
        if (H < 7 || W < 3) return null
        var mn = Float.MAX_VALUE; var mx = -Float.MAX_VALUE
        for (j in 0 until H) for (i in 0 until W) { val v = g[(y0 + j) * gw + x0 + i]; if (v < mn) mn = v; if (v > mx) mx = v }
        if (mx - mn < 20f) return null
        val t = mn + 0.5f * (mx - mn)
        var xa = W; var xb = -1; var ya = H; var yb = -1
        for (j in 0 until H) for (i in 0 until W) if (g[(y0 + j) * gw + x0 + i] > t) { if (i < xa) xa = i; if (i > xb) xb = i; if (j < ya) ya = j; if (j > yb) yb = j }
        if (xb < 0) return null
        val w = xb - xa + 1; val h = yb - ya + 1
        if (h < 0.5f * H) return null
        if (w < 0.38f * h) return 1
        fun band(cx: Float, cy: Float, rx: Float, ry: Float): Float {
            val r0 = max(0, ((cy - ry) * h).toInt()); val r1 = min(h, ((cy + ry) * h).toInt() + 1)
            val c0 = max(0, ((cx - rx) * w).toInt()); val c1 = min(w, ((cx + rx) * w).toInt() + 1)
            var s = 0f; var n = 0
            for (j in r0 until r1) for (i in c0 until c1) { s += g[(y0 + ya + j) * gw + x0 + xa + i]; n++ }
            return if (n > 0) s / n else 0f
        }
        val m = floatArrayOf(band(.5f, .08f, .25f, .08f), band(.85f, .28f, .13f, .13f), band(.85f, .72f, .13f, .13f), band(.5f, .92f, .25f, .08f),
            band(.15f, .72f, .13f, .13f), band(.15f, .28f, .13f, .13f), band(.5f, .5f, .25f, .08f))
        val lo = m.minOrNull()!!; val hi = m.maxOrNull()!!
        if (hi - lo < 15f) return null
        val th = (lo + hi) / 2; var bits = 0
        for (i in 0 until 7) if (m[i] > th) bits = bits or (1 shl i)
        DIG[bits]?.let { return it }
        val near = DIG.entries.map { Integer.bitCount(bits xor it.key) to it.value }.sortedBy { it.first }
        return if (near[0].first == 1 && near[1].first > 1) near[0].second else null
    }

    /** YOLO bat chinh vo bang dem thanh "den": chia doi hop (2 o co dinh), doc tung o theo do sang tuong doi. */
    private fun readBox(b: IntArray, col: Char): Int? {
        val bw = b[2] - b[0]; val bh = b[3] - b[1]
        val k = max(1, (64f / max(8, bh)).roundToInt())
        val W = bw * k; val H = bh * k
        if (W < 8 || H < 8 || W * H > 120_000) return null
        val g = FloatArray(W * H)
        for (j in 0 until H) for (i in 0 until W) g[j * W + i] = chanOf(rgbF(b[0] + (i + 0.5f) / k - 0.5f, b[1] + (j + 0.5f) / k - 0.5f), col)
        val a = read7v(g, W, 0, 0, W / 2, H); val c = read7v(g, W, W / 2, 0, W - W / 2, H) ?: return null
        return if (a != null) a * 10 + c else c
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

        /** Moi mau: [luc bat dau xuat hien lien tuc, lan cuoi thay]. Mat > 2,5 s thi tinh lai tu dau. */
        private val streak = HashMap<Char, LongArray>()
        /** Mau duoc chon o lan quet vua roi (de ghi log). */
        var want: Char? = null; private set

        /** Mui ten xanh: huong -> [lan dau, lan cuoi, so lan]. [go] = huong duoc di da khop >= 2 lan quet, moi nhat <= 2,5 s. */
        private val goSeen = HashMap<Char, LongArray>()
        var go: String = ""; private set

        fun update(r: Res, now: Long) {
            for (d in r.go) { val a = goSeen[d]; if (a == null || now - a[1] > 2500) goSeen[d] = longArrayOf(now, now, 1) else { a[1] = now; a[2]++ } }
            go = goSeen.filter { now - it.value[1] <= 2500 && it.value[2] >= 2 }.keys.sorted().joinToString("")
            if (r.col == null) { if (now - seenAt > 3000) { col = null; sec = -1; lamps = emptyList(); streak.clear() }; want = null; return }
            seenAt = now; lamps = r.lamps
            val present = r.lamps.map { it.col }.toSet().ifEmpty { setOf(r.col) }
            for (c in present) { val st = streak[c]; if (st == null || now - st[1] > 2500) streak[c] = longArrayOf(now, now) else st[1] = now }
            // 23/11 test that: dung cho do, den minh chuyen xanh nhung khung van con 1 den do khac (huong khac / mui ten / bi cat
            // khoi man) to hon -> cu bao DO. Luat: khi trong khung co nhieu mau, MAU MOI XUAT HIEN (sau mau kia >= 1,5 s) thang;
            // cung luc xuat hien thi theo den to + gan giua nhat (r.col).
            var w: Char = r.col
            if (present.size > 1) {
                val nw = present.maxByOrNull { streak[it]!![0] }!!
                val older = present.filter { it != nw }.minOf { streak[it]!![0] }
                if (streak[nw]!![0] - older >= 1500) w = nw
            }
            want = w
            if (w != col) {
                if (cand == w) candN++ else { cand = w; candN = 1 }
                if (candN >= 2 || col == null) { prevCol = col; col = w; changedAt = now; sec = -1; pendV = -1 } else return
            }
            val v = (if (w == r.col) r.sec else null) ?: r.lamps.firstOrNull { it.col == w && it.sec != null }?.sec ?: return
            val pred = cur(now)
            when {
                // 1 chu so ma chua co du doan (hoac du doan con >= 12 s) de la doc thieu 1 chu so -> phai khop 2 lan moi nhan
                v < 10 && (pred == null || pred >= 12) && !(pendV >= 0 && abs(pendV - ((now - pendAt) / 1000).toInt() - v) <= 1) -> { pendV = v; pendAt = now }
                pred == null || abs(pred - v) <= 2 -> { sec = v; secAt = now; pendV = -1 }
                pendV >= 0 && abs(pendV - ((now - pendAt) / 1000).toInt() - v) <= 1 -> { sec = v; secAt = now; pendV = -1 }
                else -> { pendV = v; pendAt = now }
            }
        }
        /** So giay hien tai (dem lui theo dong ho), null = khong co / da het. */
        fun cur(now: Long): Int? { if (sec < 0) return null; val v = sec - ((now - secAt) / 1000).toInt(); return if (v >= 0) v else null }
        fun clear() { col = null; sec = -1; lamps = emptyList(); cand = null; candN = 0; streak.clear(); goSeen.clear(); go = "" }
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
