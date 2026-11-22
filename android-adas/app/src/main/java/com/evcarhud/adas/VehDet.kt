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
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min

/**
 * Nhan xe/nguoi TOAN KHUNG cho man canh 3D (chi chay khi man 3D dang bat).
 * Dung lai mo hinh YOLOX-Nano COCO co san (xe-coco.onnx, Apache-2.0), dau vao dong h x w:
 * cat 1 dai ngang quanh duong chan troi (ca be ngang anh) -> 640 x 224.
 * Ket qua: toa do chuan hoa 0..1 theo anh hien thi, giong SignDet.Obj.
 */
class VehDet(ctx: Context, private val onObjs: (List<SignDet.Obj>) -> Unit) {
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
    fun offer(im: YuvFrame, flip: Boolean, topFrac: Float, periodMs: Long) {
        val now = SystemClock.elapsedRealtime()
        if (busy || sess == null || now - lastAt < periodMs) return
        try {
            val p = im.planes
            yA = cp(p[0].buffer, yA); uA = cp(p[1].buffer, uA); vA = cp(p[2].buffer, vA)
            yRow = p[0].rowStride; uvRow = p[1].rowStride; uvPix = p[1].pixelStride; w = im.width; hh = im.height; this.flip = flip
            top = topFrac
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
        return (0xff shl 24) or (r shl 16) or (g shl 8) or b
    }

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
        onObjs(keep.take(16).map { SignDet.Obj(it[5].toInt(), it[4], it[0], it[1], it[2], it[3]) })
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
        private const val CONF = 0.35f
        /** COCO: nguoi 0, xe dap 1, o to 2, xe may 3, xe buyt 5, xe tai 7 */
        private val CLS = intArrayOf(0, 1, 2, 3, 5, 7)
    }
}
