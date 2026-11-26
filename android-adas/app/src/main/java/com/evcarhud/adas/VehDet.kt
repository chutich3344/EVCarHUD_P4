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

/**
 * Nhan xe/nguoi TOAN KHUNG cho man canh 3D (chi chay khi man 3D dang bat).
 * Dung lai mo hinh YOLOX-Nano COCO co san (xe-coco.onnx, Apache-2.0), dau vao dong h x w:
 * cat 1 dai ngang quanh duong chan troi (ca be ngang anh) -> 640 x 224.
 * Ket qua: toa do chuan hoa 0..1 theo anh hien thi, giong SignDet.Obj.
 */
class VehDet(ctx: Context, private val onObjs: (List<SignDet.Obj>, IntArray?, Long) -> Unit) {
    private val th = HandlerThread("veh3d").apply { start() }
    private val h = Handler(th.looper)
    private val env = OrtEnvironment.getEnvironment()
    private var sess: OrtSession? = null
    private val inp = FloatBuffer.allocate(3 * IH * IW)
    @Volatile private var busy = false
    private var lastAt = 0L
    @Volatile var lastMs = 0L
    private var yA = ByteArray(0); private var uA = ByteArray(0); private var vA = ByteArray(0)
    private var yRow = 0; private var uvRow = 0; private var uvPix = 1; private var w = 0; private var hh = 0; private var flip = false
    private var top = 0.3f
    // doc vach: hinh hoc camera + vach openpilot luc chup khung
    private var geo: Op.Cam? = null
    private val lnY = Array(4) { FloatArray(Op.N) }; private val lnZ = Array(4) { FloatArray(Op.N) }; private var lnOk = false
    private val emR = FloatArray(4); private val emY = FloatArray(4); private val emD = FloatArray(4); private var lastL1 = 0f

    init {
        h.post {
            try {
                val o = OrtSession.SessionOptions().apply {
                    setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                    try { setIntraOpNumThreads(1); addXnnpack(mapOf("intra_op_num_threads" to "2")) } catch (e: Throwable) { setIntraOpNumThreads(2) }
                }
                sess = env.createSession(ctx.assets.open("xe-coco.onnx").use { it.readBytes() }, o)
            } catch (e: Throwable) { Log.e(TAG, "load", e) }
        }
    }

    /** Goi tu onFrame (truoc khi dong anh). topFrac: mep tren dai quet (ti le chieu cao, thuong ngay tren diem tu). */
    fun offer(im: YuvFrame, flip: Boolean, topFrac: Float, periodMs: Long, g: Op.Cam? = null, o: Op.Out? = null) {
        val now = SystemClock.elapsedRealtime()
        if (busy || sess == null || now - lastAt < periodMs) return
        try {
            val p = im.planes
            yA = cp(p[0].buffer, yA); uA = cp(p[1].buffer, uA); vA = cp(p[2].buffer, vA)
            yRow = p[0].rowStride; uvRow = p[1].rowStride; uvPix = p[1].pixelStride; w = im.width; hh = im.height; this.flip = flip
            top = topFrac
            geo = g; lnOk = g != null && o != null
            if (o != null) for (i in 0..3) { System.arraycopy(o.laneY[i], 0, lnY[i], 0, Op.N); System.arraycopy(o.laneZ[i], 0, lnZ[i], 0, Op.N) }
        } catch (e: Exception) { return }
        lastAt = now; busy = true; frameAt = now   // 24/11: moc thoi gian KHUNG ANH (khong phai luc AI xong) -> 3D dat xe dung thoi diem
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
        return (0xff shl 24) or (r shl 16) or (g shl 8) or b
    }

    @Volatile private var frameAt = 0L
    private fun run() {
        val s = sess ?: return
        val t0 = SystemClock.elapsedRealtime()
        val sc = w.toFloat() / IW                       // ca be ngang anh
        val bandH = IH * sc
        val y0 = (top * hh).coerceIn(0f, max(0f, hh - bandH))
        val arr = inp.array(); val pl = IH * IW
        for (iy in 0 until IH) {
            val sy = (y0 + (iy + 0.5f) * sc).toInt()
            for (ix in 0 until IW) {
                val o = iy * IW + ix
                if (sy >= hh) { arr[o] = 114f; arr[pl + o] = 114f; arr[2 * pl + o] = 114f; continue }
                val c = rgb(((ix + 0.5f) * sc).toInt(), sy)
                // YOLOX: BGR 0..255
                arr[o] = (c and 255).toFloat(); arr[pl + o] = ((c shr 8) and 255).toFloat(); arr[2 * pl + o] = ((c shr 16) and 255).toFloat()
            }
        }
        inp.clear()
        val out = OnnxTensor.createTensor(env, inp, longArrayOf(1, 3, IH.toLong(), IW.toLong())).use { t ->
            s.run(mapOf("images" to t)).use { r -> (r.get(0) as OnnxTensor).floatBuffer.let { fb -> FloatArray(fb.remaining()).also { fb.get(it) } } }
        }
        val cand = ArrayList<FloatArray>()
        var a = 0
        for (st in intArrayOf(8, 16, 32)) {
            val nx = IW / st; val ny = IH / st
            for (gy in 0 until ny) for (gx in 0 until nx) {
                val b = a * 85; a++
                if (b + 85 > out.size) break
                val obj = out[b + 4]; if (obj < CONF) continue
                var best = -1; var bs = 0f
                for (c in CLS) { val v = obj * out[b + 5 + c]; if (v > bs) { bs = v; best = c } }
                if (best < 0 || bs < CONF) continue
                val cx = (out[b] + gx) * st; val cy = (out[b + 1] + gy) * st
                val bw = exp(out[b + 2]) * st; val bh = exp(out[b + 3]) * st
                cand += floatArrayOf((cx - bw / 2) * sc / w, (y0 + (cy - bh / 2) * sc) / hh, (cx + bw / 2) * sc / w, (y0 + (cy + bh / 2) * sc) / hh, bs, best.toFloat())
            }
        }
        cand.sortByDescending { it[4] }
        val keep = ArrayList<FloatArray>()
        // NMS khong phan lop (xe con / xe tai hay bi nhan doi cung 1 xe); bo hop qua to (nap capo, kinh)
        for (c in cand) if (c[2] - c[0] < 0.75f && keep.none { iou(it, c) > 0.5f }) keep += c
        lastMs = SystemClock.elapsedRealtime() - t0
        val lm = try { if (lnOk) marks() else null } catch (e: Exception) { null }
        onObjs(keep.take(16).map { SignDet.Obj(it[5].toInt(), it[4], it[0], it[1], it[2], it[3]) }, lm, frameAt)
    }

    /**
     * Doc mau + kieu vach (23/11): lay mau anh doc theo 4 vach openpilot tu 6 den 30 m, moi diem quet ngang +-0,5 m.
     * Diem "co son" = sang hon nen 2 ben > max(18, 15%). Ti le co son tren chieu dai: > 0,72 lien, 0,12..0,72 dut, nho hon = khong co.
     * Vang: (R+G)/2 - B chia do sang, tru cua nen (son vang 70mai ~ 195,155,113 -> 0,26; trang ~ 0). Hai dai son tach nhau = vach doi.
     * Lam muot ~1 s. Ma: 0 khong ro, 1 trang dut, 2 trang lien, 3 vang dut, 4 vang lien, 5 vang doi.
     */
    private fun marks(): IntArray {
        val g = geo ?: return IntArray(4)
        val sx = w.toFloat() / g.w; val sy = hh.toFloat() / g.h
        val l1 = interp(lnY[1], 10f)
        if (abs(l1 - lastL1) > 1.6f) { emR.fill(0f); emY.fill(0f); emD.fill(0f) }   // chuyen lan: vach doi so thu tu
        lastL1 = l1
        val pt = FloatArray(2); val yv = FloatArray(21); val ych = FloatArray(21)
        val res = IntArray(4)
        for (i in 0..3) {
            var n = 0; var paint = 0; var dbl = 0; val yel = ArrayList<Float>()
            var d = 6f
            while (d <= 30f) {
                val y = interp(lnY[i], d); val z = interp(lnZ[i], d); var ok = true
                for (a in 0 until 21) {
                    if (!g.project(d, y - 0.5f + a * 0.05f, z, pt, 0)) { ok = false; break }
                    val u = (pt[0] * sx).toInt(); val v = (pt[1] * sy).toInt()
                    if (u < 0 || u >= w || v < 0 || v >= hh * 0.97f) { ok = false; break }
                    val c = rgb(u, v)
                    val r = (c shr 16) and 255; val gg = (c shr 8) and 255; val b = c and 255
                    yv[a] = 0.299f * r + 0.587f * gg + 0.114f * b
                    ych[a] = ((r + gg) / 2f - b) / (yv[a] + 1f)
                }
                d += 0.5f
                if (!ok) continue
                n++
                val bgs = floatArrayOf(yv[0], yv[1], yv[2], yv[18], yv[19], yv[20]).sorted(); val bg = (bgs[2] + bgs[3]) / 2
                val bgc = floatArrayOf(ych[0], ych[1], ych[2], ych[18], ych[19], ych[20]).sorted().let { (it[2] + it[3]) / 2 }
                val thr = bg + maxOf(18f, 0.15f * bg)
                var cnt = 0; var runs = 0; var prev = false; var ymax = -1f
                for (a in 4..16) { val on = yv[a] > thr; if (on) { cnt++; if (ych[a] > ymax) ymax = ych[a] }; if (on && !prev) runs++; prev = on }
                if (cnt > 0) { paint++; yel += ymax - bgc; if (runs >= 2) dbl++ }
            }
            val r = if (n >= 8) paint.toFloat() / n else 0f
            val ye = if (yel.isEmpty()) 0f else yel.sorted()[yel.size / 2]
            val db = if (paint > 0) dbl.toFloat() / paint else 0f
            val k = 0.25f
            emR[i] += k * (r - emR[i]); if (yel.size >= 3) emY[i] += k * (ye - emY[i]); emD[i] += k * (db - emD[i])
            val solid = emR[i] > 0.72f; val dashed = emR[i] in 0.12f..0.72f; val yellow = emY[i] > 0.18f
            res[i] = when {
                n < 8 || (!solid && !dashed) -> 0
                yellow && solid && emD[i] > 0.4f -> 5
                yellow && solid -> 4
                yellow -> 3
                solid -> 2
                else -> 1
            }
        }
        return res
    }

    private fun interp(a: FloatArray, x: Float): Float {
        val xs = Op.X_IDXS
        if (x <= xs[0]) return a[0]
        for (k in 1 until Op.N) if (x <= xs[k]) { val t = (x - xs[k - 1]) / (xs[k] - xs[k - 1]); return a[k - 1] + t * (a[k] - a[k - 1]) }
        return a[Op.N - 1]
    }

    private fun iou(a: FloatArray, b: FloatArray): Float {
        val ix = max(0f, min(a[2], b[2]) - max(a[0], b[0])); val iy = max(0f, min(a[3], b[3]) - max(a[1], b[1]))
        val i = ix * iy; val u = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - i
        return if (u > 0) i / u else 0f
    }

    fun release() { h.post { try { sess?.close() } catch (_: Exception) {}; sess = null; th.quitSafely() } }

    companion object {
        private const val TAG = "VehDet"
        private const val IW = 640
        private const val IH = 224
        private const val CONF = 0.2f   // 24/11 ByteTrack: lay ca hop diem thap (0,2–0,35) de NOI TIEP vet cu; xe moi van can >= 0,35 (trang 3D)
        /** COCO: nguoi 0, xe dap 1, o to 2, xe may 3, xe buyt 5, xe tai 7 */
        private val CLS = intArrayOf(0, 1, 2, 3, 5, 7)
    }
}
