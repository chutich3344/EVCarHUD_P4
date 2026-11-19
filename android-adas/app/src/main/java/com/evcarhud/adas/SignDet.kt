package com.evcarhud.adas

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import android.content.Context
import android.graphics.Bitmap
import android.media.Image
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import java.nio.ByteBuffer
import java.nio.FloatBuffer
import kotlin.math.max
import kotlin.math.min

/**
 * Doc bien bao giao thong VN: YOLO11n 56 loai (assets/bien-vn.onnx, MIT, github Namvipcf/vietnamese-traffic-sign-yolo11)
 * chay 2 lan/giay tren luong rieng, vung anh 85% ben phai x 70% tren (bien thuong o day), dau vao 768x352.
 * Bien toc do (lop 17) doc so bang NumRead tren anh do phan giai goc. Kem anh cat bien + bien phu (nen trang ngay duoi).
 */
class SignDet(ctx: Context, private val onObjs: (List<Obj>) -> Unit = {}, private val onHits: (List<Hit>) -> Unit) {
    class Hit(val cls: Int, val conf: Float, val value: Int?, val score: Float, val crop: Bitmap, val aux: Bitmap?)
    /** Xe/nguoi phia truoc (YOLOX-nano COCO, Apache-2.0); toa do chuan hoa 0..1 theo anh hien thi. */
    class Obj(val cls: Int, val conf: Float, val x1: Float, val y1: Float, val x2: Float, val y2: Float)

    private val th = HandlerThread("sign").apply { start() }
    private val h = Handler(th.looper)
    private val env = OrtEnvironment.getEnvironment()
    private var sess: OrtSession? = null
    private var vsess: OrtSession? = null
    private val vin = FloatBuffer.allocate(3 * VS * VS)
    @Volatile private var busy = false
    /** false: lan nay bo nhan loai xe (may nong). */
    @Volatile var objsOn = true
    private var lastAt = 0L
    @Volatile var lastMs = 0L
    @Volatile var info = "đang nạp"
    private var yA = ByteArray(0); private var uA = ByteArray(0); private var vA = ByteArray(0)
    private var yRow = 0; private var uvRow = 0; private var uvPix = 1; private var w = 0; private var hh = 0; private var flip = false
    private val inp = FloatBuffer.allocate(3 * IH * IW)

    init {
        h.post {
            try {
                NumRead.load(ctx.assets.open("digits.dat").use { it.readBytes() })
                val o = OrtSession.SessionOptions().apply { setIntraOpNumThreads(2); setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT) }
                sess = env.createSession(ctx.assets.open("bien-vn.onnx").use { it.readBytes() }, o)
                try { vsess = env.createSession(ctx.assets.open("xe-coco.onnx").use { it.readBytes() }, o) } catch (e: Throwable) { Log.e(TAG, "xe", e) }
                info = "sẵn sàng"
            } catch (e: Throwable) { info = "lỗi nạp: ${e.message}"; Log.e(TAG, "load", e) }
        }
    }

    /** Goi tu onFrame (truoc khi dong Image). Chep anh roi chay tren luong rieng; toi da 1 lan / periodMs. */
    fun offer(im: Image, flip: Boolean, periodMs: Long = 500) {
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

    /** Diem anh (x, y) theo huong hien thi -> RGB tu YUV goc. */
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
        // vung quet: 15%..100% ngang, cao theo ti le dau vao, tu 6% chieu cao
        val cx0 = (0.15f * w).toInt(); val cw = w - cx0
        val ch = min(hh.toFloat(), cw * IH.toFloat() / IW); val cy0 = min(0.06f * hh, hh - ch)
        val sc = cw.toFloat() / IW
        inp.clear()
        val plane = IH * IW
        val arr = inp.array()
        for (iy in 0 until IH) {
            val sy = (cy0 + (iy + 0.5f) * sc).toInt()
            for (ix in 0 until IW) {
                val c = if (sy < hh) rgb(cx0 + ((ix + 0.5f) * sc).toInt(), sy) else 0
                val o = iy * IW + ix
                arr[o] = ((c shr 16) and 255) / 255f; arr[plane + o] = ((c shr 8) and 255) / 255f; arr[2 * plane + o] = (c and 255) / 255f
            }
        }
        val out = OnnxTensor.createTensor(env, inp, longArrayOf(1, 3, IH.toLong(), IW.toLong())).use { t ->
            s.run(mapOf("images" to t)).use { r -> (r.get(0) as OnnxTensor).floatBuffer.let { fb -> FloatArray(fb.remaining()).also { fb.get(it) } } }
        }
        // out [1, 4+56, A]: cx, cy, w, h (pixel dau vao), diem tung lop
        val na = out.size / (4 + NC)
        val cand = ArrayList<FloatArray>()   // x1,y1,x2,y2 (anh goc), conf, cls
        for (i in 0 until na) {
            var best = 0; var bs = 0f
            for (c in 0 until NC) { val v = out[(4 + c) * na + i]; if (v > bs) { bs = v; best = c } }
            if (bs < CONF) continue
            val bx = out[i]; val by = out[na + i]; val bw = out[2 * na + i]; val bh = out[3 * na + i]
            cand += floatArrayOf(cx0 + (bx - bw / 2) * sc, cy0 + (by - bh / 2) * sc, cx0 + (bx + bw / 2) * sc, cy0 + (by + bh / 2) * sc, bs, best.toFloat())
        }
        cand.sortByDescending { it[4] }
        val keep = ArrayList<FloatArray>()
        for (c in cand) if (keep.none { it[5] == c[5] && iou(it, c) > 0.5f }) keep += c
        val hits = ArrayList<Hit>()
        for (k in keep.take(6)) {
            val x1 = k[0].toInt(); val y1 = k[1].toInt(); val x2 = k[2].toInt(); val y2 = k[3].toInt(); val cls = k[5].toInt()
            if (x2 - x1 < 8 || y2 - y1 < 8) continue
            var value: Int? = null; var score = 0f
            if (cls == SPEED) {
                val m = max(2, (x2 - x1) / 16)
                val (v, sco) = NumRead.read(sample(x1 - m, y1 - m, x2 + m, y2 + m, 96, 96))
                if (v != null) { value = v; score = sco }
            }
            val mg = max(3, (x2 - x1) / 10)
            val crop = bitmap(x1 - mg, y1 - mg, x2 + mg, y2 + mg)
            hits += Hit(cls, k[4], value, score, crop, aux(x1, y1, x2, y2))
        }
        val nObj = if (!objsOn) 0 else try { objs() } catch (e: Throwable) { Log.w(TAG, "xe", e); 0 }
        lastMs = SystemClock.elapsedRealtime() - t0
        info = "${lastMs} ms · ${hits.size} biển · $nObj xe"
        if (hits.isNotEmpty()) onHits(hits)
    }

    /** Nhan loai xe: vung giua 70% ngang tu 20% chieu cao, dua vao 416x416 (BGR 0..255, dem 114 ben duoi). */
    private fun objs(): Int {
        val s = vsess ?: return 0
        val x0 = 0.15f * w; val cw = 0.7f * w; val y0 = 0.2f * hh; val sc = cw / VS
        val arr = vin.array(); val pl = VS * VS
        for (iy in 0 until VS) {
            val sy = (y0 + (iy + 0.5f) * sc).toInt()
            for (ix in 0 until VS) {
                val o = iy * VS + ix
                if (sy >= hh) { arr[o] = 114f; arr[pl + o] = 114f; arr[2 * pl + o] = 114f; continue }
                val c = rgb((x0 + (ix + 0.5f) * sc).toInt(), sy)
                arr[o] = (c and 255).toFloat(); arr[pl + o] = ((c shr 8) and 255).toFloat(); arr[2 * pl + o] = ((c shr 16) and 255).toFloat()
            }
        }
        vin.clear()
        val out = OnnxTensor.createTensor(env, vin, longArrayOf(1, 3, VS.toLong(), VS.toLong())).use { t ->
            s.run(mapOf("images" to t)).use { r -> (r.get(0) as OnnxTensor).floatBuffer.let { fb -> FloatArray(fb.remaining()).also { fb.get(it) } } }
        }
        // out [1, 3549, 85]: dx, dy, log w, log h, obj, 80 lop; luoi stride 8/16/32
        val cand = ArrayList<FloatArray>()
        var a = 0
        for (st in intArrayOf(8, 16, 32)) { val n = VS / st
            for (gy in 0 until n) for (gx in 0 until n) {
                val b = a * 85; a++
                val obj = out[b + 4]; if (obj < VCONF) continue
                var best = -1; var bs = 0f
                for (c in VCLS) { val v = obj * out[b + 5 + c]; if (v > bs) { bs = v; best = c } }
                if (best < 0 || bs < VCONF) continue
                val cx = (out[b] + gx) * st; val cy = (out[b + 1] + gy) * st
                val bw = kotlin.math.exp(out[b + 2]) * st; val bh = kotlin.math.exp(out[b + 3]) * st
                cand += floatArrayOf((x0 + (cx - bw / 2) * sc) / w, (y0 + (cy - bh / 2) * sc) / hh, (x0 + (cx + bw / 2) * sc) / w, (y0 + (cy + bh / 2) * sc) / hh, bs, best.toFloat())
            }
        }
        cand.sortByDescending { it[4] }
        val keep = ArrayList<FloatArray>()
        for (c in cand) if (keep.none { iou(it, c) > 0.5f }) keep += c
        onObjs(keep.take(12).map { Obj(it[5].toInt(), it[4], it[0], it[1], it[2], it[3]) })
        return keep.size
    }

    /** Bien phu: vung ngay duoi bien chinh, nen trang it mau chiem phan lon. */
    private fun aux(x1: Int, y1: Int, x2: Int, y2: Int): Bitmap? {
        val bw = x2 - x1; val bh = y2 - y1
        val ax1 = x1 - bw / 4; val ax2 = x2 + bw / 4; val ay1 = y2 + 2; val ay2 = y2 + (bh * 0.9f).toInt()
        if (ay2 >= hh || ay2 - ay1 < 8) return null
        val px = sample(ax1, ay1, ax2, ay2, 32, 16)
        var white = 0; var color = 0
        for (c in px) { val r = (c shr 16) and 255; val g = (c shr 8) and 255; val b = c and 255; val mx = max(r, max(g, b)); val mn = min(r, min(g, b))
            if (mx > 150 && (mx - mn) * 255 < 60 * mx) white++
            if (mx > 90 && (mx - mn) * 255 > 120 * mx) color++ }   // do / xanh dam: la bien khac treo ben duoi, khong phai bien phu chu
        return if (white > 0.45f * px.size && color < 0.06f * px.size) bitmap(ax1, ay1, ax2, ay2) else null
    }

    /** Lay mau song tuyen vung [x1,x2)x[y1,y2) ve ow x oh. */
    private fun sample(x1: Int, y1: Int, x2: Int, y2: Int, ow: Int, oh: Int): IntArray {
        val o = IntArray(ow * oh); val sx = (x2 - x1).toFloat() / ow; val sy = (y2 - y1).toFloat() / oh
        for (j in 0 until oh) for (i in 0 until ow) o[j * ow + i] = rgb(x1 + ((i + 0.5f) * sx).toInt(), y1 + ((j + 0.5f) * sy).toInt())
        return o
    }

    private fun bitmap(x1: Int, y1: Int, x2: Int, y2: Int): Bitmap {
        val bw = x2 - x1; val bh = y2 - y1; val k = min(1f, 128f / max(bw, bh))
        val ow = max(1, (bw * k).toInt()); val oh = max(1, (bh * k).toInt())
        return Bitmap.createBitmap(sample(x1, y1, x2, y2, ow, oh), ow, oh, Bitmap.Config.ARGB_8888)
    }

    private fun iou(a: FloatArray, b: FloatArray): Float {
        val ix = max(0f, min(a[2], b[2]) - max(a[0], b[0])); val iy = max(0f, min(a[3], b[3]) - max(a[1], b[1]))
        val i = ix * iy; val u = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - i
        return if (u > 0) i / u else 0f
    }

    fun release() { h.post { try { sess?.close(); vsess?.close() } catch (_: Exception) {}; sess = null; vsess = null; th.quitSafely() } }

    companion object {
        private const val TAG = "SignDet"
        private const val IW = 768
        private const val IH = 352
        private const val NC = 56
        private const val CONF = 0.4f
        private const val VS = 416
        private const val VCONF = 0.3f
        private val VCLS = intArrayOf(0, 1, 2, 3, 5, 7)
        /** Ten ngan loai xe (chi so COCO). */
        fun kind(c: Int) = when (c) { 0 -> "người đi bộ"; 1 -> "xe đạp"; 2 -> "xe con"; 3 -> "xe máy"; 5 -> "xe buýt"; 7 -> "xe tải"; else -> "xe" }
        const val SPEED = 17
        /** Lop canh bao/cam dang hien len cot bien (bo qua bien chi dan it gia tri khi lai). */
        val WARN = setOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22, 28, 29, 31, 32, 34, 35, 36, 37, 38, 39,
            40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 55)
        val NAME = arrayOf("Hết lệnh cấm", "Cấm đi ngược chiều", "Cấm ô tô", "Cấm ô tô rẽ phải", "Cấm ô tô rẽ trái", "Cấm mô tô", "Cấm xe tải",
            "Cấm xe tải (tải trọng)", "Cấm xe khách", "Cấm người đi bộ", "Hạn chế trọng lượng", "Hạn chế chiều cao", "Cấm rẽ trái", "Cấm rẽ phải",
            "Cấm quay đầu", "Cấm ô tô quay đầu", "Cấm rẽ trái + quay đầu", "Tốc độ tối đa", "Cấm bóp còi", "Cấm dừng đỗ", "Cấm đỗ xe",
            "Cấm rẽ trái và phải", "Cấm xe điện", "Đi thẳng / rẽ phải", "Hướng rẽ trái", "Hướng rẽ phải", "Đi vòng bên phải", "Đi vòng bên trái",
            "Vòng xuyến", "Đường một chiều", "Chỗ quay xe", "Bắt đầu cao tốc", "Làn xe buýt", "Biển phụ cấm dừng đỗ", "Chỗ ngoặt trái",
            "Chỗ ngoặt phải", "Nhiều chỗ ngoặt", "Nhiều chỗ ngoặt", "Đường hẹp bên trái", "Đường hẹp bên phải", "Ngã tư", "Ngã ba phải",
            "Ngã ba trái", "Giao đường không ưu tiên", "Giao đường không ưu tiên", "Giao đường không ưu tiên", "Giao đường ưu tiên",
            "Giao nhau có đèn", "Giao đường sắt", "Dốc xuống nguy hiểm", "Người đi bộ cắt ngang", "Trẻ em", "Công trường", "Nguy hiểm khác",
            "Đường đôi", "Đi chậm")
    }
}
