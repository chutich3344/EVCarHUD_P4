package com.evcarhud.adas

import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Hang so va phep tinh lay tu openpilot v0.10.3 (giay phep MIT, (c) comma.ai):
 * khung anh mo hinh, nan hinh camera ve khung mo hinh, dong goi YUV, doc dau ra.
 * Da kiem tra cung cach lam tren video dashcam (run_op.py) truoc khi dua vao app.
 */
object Op {
    const val N = 33
    val X_IDXS = FloatArray(N) { (192.0 * (it / 32.0) * (it / 32.0)).toFloat() }

    const val MW = 512
    const val MH = 256
    private const val MED_FL = 910.0
    private const val MED_CY = 47.6
    private const val BIG_FL = 455.0
    private const val BIG_CY = 0.5 * (256 + MED_CY)

    /** 1 khung dong goi: 6 kenh x 128 x 256 (Y chia 4 + U + V) theo loadyuv.cl. */
    const val PACK = 6 * 128 * 256

    // Vi tri cac phan trong dau ra driving_vision (1576 so)
    const val V_OUT = 1576
    private const val DESIRE = 55
    private const val POSE = 87
    private const val LANE = 117
    private const val LANE_PROB = 645
    private const val EDGE = 653
    private const val LEAD = 917
    private const val LEAD_PROB = 1061
    const val HIDDEN = 1064
    const val HID_N = 512

    // driving_policy: plan 0..990 (khong MHP: nua dau la trung binh, 33 x 15)
    const val P_OUT = 1000
    const val FEAT_N = 25

    fun sigmoid(x: Float) = (1.0 / (1.0 + exp(-x.toDouble().coerceIn(-50.0, 50.0)))).toFloat()

    /** Ma tran 3x3 dang mang 9 so (theo hang). */
    fun mul(a: DoubleArray, b: DoubleArray): DoubleArray {
        val r = DoubleArray(9)
        for (i in 0..2) for (j in 0..2) {
            var s = 0.0
            for (k in 0..2) s += a[i * 3 + k] * b[k * 3 + j]
            r[i * 3 + j] = s
        }
        return r
    }

    fun tr(a: DoubleArray) = DoubleArray(9) { a[(it % 3) * 3 + it / 3] }

    fun rotFromEuler(r: Double, p: Double, y: Double): DoubleArray {
        val rx = doubleArrayOf(1.0, 0.0, 0.0, 0.0, cos(r), -sin(r), 0.0, sin(r), cos(r))
        val ry = doubleArrayOf(cos(p), 0.0, sin(p), 0.0, 1.0, 0.0, -sin(p), 0.0, cos(p))
        val rz = doubleArrayOf(cos(y), -sin(y), 0.0, sin(y), cos(y), 0.0, 0.0, 0.0, 1.0)
        return mul(mul(rz, ry), rx)
    }

    /** Nguoc cua rotFromEuler: (roll, pitch, yaw). */
    fun eulerFromRot(m: DoubleArray) = doubleArrayOf(
        kotlin.math.atan2(m[7], m[8]), -kotlin.math.asin(m[6].coerceIn(-1.0, 1.0)), kotlin.math.atan2(m[3], m[0]))

    /** device (x truoc, y phai, z xuong) -> view (x phai, y xuong, z truoc). */
    val VFD = doubleArrayOf(0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0)

    private fun kInv(fl: Double, cy: Double) =
        doubleArrayOf(1 / fl, 0.0, -MW / 2.0 / fl, 0.0, 1 / fl, -cy / fl, 0.0, 0.0, 1.0)

    /**
     * Camera dien thoai (pinhole) da xoay dung chieu, kich thuoc w x h, tieu cu f px.
     * Hieu chinh: pitch/yaw cua huong "truoc mat xe" so voi camera.
     */
    class Cam(val w: Int, val h: Int, val f: Double, pitch: Double, yaw: Double, roll: Double = 0.0) {
        val cx = w / 2.0
        val cy = h / 2.0
        /** view-cua-camera <- calib (device) : dung de chieu diem 3D len anh. */
        val viewFromCalib: DoubleArray = mul(VFD, rotFromEuler(roll, pitch, yaw))

        /** Chieu diem (x truoc, y phai, z xuong, goc camera) -> pixel anh. false neu sau camera. */
        fun project(x: Float, y: Float, z: Float, out: FloatArray, o: Int): Boolean {
            val m = viewFromCalib
            val vx = m[0] * x + m[1] * y + m[2] * z
            val vy = m[3] * x + m[4] * y + m[5] * z
            val vz = m[6] * x + m[7] * y + m[8] * z
            if (vz < 0.5) return false
            out[o] = (cx + f * vx / vz).toFloat()
            out[o + 1] = (cy + f * vy / vz).toFloat()
            return true
        }
    }

    /**
     * Bang tra nan hinh: voi moi o trong goi YUV cua mo hinh -> vi tri byte trong mat phang Y / UV cua camera.
     * Dung lang can (nearest): du chinh xac cho mo hinh, nhe CPU hon noi suy.
     */
    class Warp(cam: Cam, big: Boolean, yRow: Int, uvRow: Int, uvPix: Int, flip: Boolean) {
        val y = IntArray(4 * 128 * 256)
        val uv = IntArray(128 * 256)

        init {
            val ki = if (big) kInv(BIG_FL, BIG_CY) else kInv(MED_FL, MED_CY)
            // tia trong khung camera = VFD * dfc * VFD^T * K^-1 * [u v 1]
            val m = mul(mul(cam.viewFromCalib, tr(VFD)), ki)
            fun src(u: Double, v: Double, out: IntArray): Boolean {
                val rx = m[0] * u + m[1] * v + m[2]
                val ry = m[3] * u + m[4] * v + m[5]
                val rz = m[6] * u + m[7] * v + m[8]
                if (rz <= 1e-6) return false
                var sx = (cam.cx + cam.f * rx / rz).roundToInt()
                var sy = (cam.cy + cam.f * ry / rz).roundToInt()
                if (flip) { sx = cam.w - 1 - sx; sy = cam.h - 1 - sy }
                if (sx < 0 || sy < 0 || sx >= cam.w || sy >= cam.h) return false
                out[0] = sx; out[1] = sy
                return true
            }
            val p = IntArray(2)
            for (c in 0..3) {
                val dr = if (c == 1 || c == 3) 1 else 0
                val dc = if (c >= 2) 1 else 0
                for (r in 0 until 128) for (q in 0 until 256) {
                    val i = (c * 128 + r) * 256 + q
                    y[i] = if (src((2 * q + dc).toDouble(), (2 * r + dr).toDouble(), p)) p[1] * yRow + p[0] else -1
                }
            }
            for (r in 0 until 128) for (q in 0 until 256) {
                uv[r * 256 + q] = if (src(2.0 * q + 0.5, 2.0 * r + 0.5, p)) (p[1] / 2) * uvRow + (p[0] / 2) * uvPix else -1
            }
        }

        /** Dong goi 1 khung vao out[off .. off+PACK). */
        fun pack(yp: ByteArray, up: ByteArray, vp: ByteArray, out: ByteArray, off: Int) {
            val ym = y
            val ny = yp.size
            for (i in ym.indices) {
                val o = ym[i]
                out[off + i] = if (o in 0 until ny) yp[o] else 0
            }
            val n = ym.size
            val m = uv.size
            val nu = minOf(up.size, vp.size)
            val gray = 128.toByte()
            for (i in uv.indices) {
                val o = uv[i]
                if (o in 0 until nu) { out[off + n + i] = up[o]; out[off + n + m + i] = vp[o] }
                else { out[off + n + i] = gray; out[off + n + m + i] = gray }
            }
        }
    }

    /** Ket qua 1 lan chay, da doc ra so. Toa do: x truoc, y phai, z xuong (m), goc camera. */
    class Out {
        val laneY = Array(4) { FloatArray(N) }
        val laneZ = Array(4) { FloatArray(N) }
        val laneProb = FloatArray(4)
        val edgeY = Array(2) { FloatArray(N) }
        val edgeZ = Array(2) { FloatArray(N) }
        var leadProb = 0f
        var leadX = 0f
        var leadY = 0f
        var leadV = 0f
        var poseVx = 0f
        var poseVy = 0f
        var poseVz = 0f
        var poseRz = 0f
        var desireL = 0f
        var desireR = 0f
        val hidden = FloatArray(HID_N)
        var planOk = false
        val planX = FloatArray(N)
        val planY = FloatArray(N)
        val planZ = FloatArray(N)
    }

    fun parseVision(o: FloatArray, r: Out) {
        for (i in 0..3) {
            for (k in 0 until N) {
                r.laneY[i][k] = o[LANE + i * N * 2 + k * 2]
                r.laneZ[i][k] = o[LANE + i * N * 2 + k * 2 + 1]
            }
            r.laneProb[i] = sigmoid(o[LANE_PROB + i * 2 + 1])
        }
        for (i in 0..1) for (k in 0 until N) {
            r.edgeY[i][k] = o[EDGE + i * N * 2 + k * 2]
            r.edgeZ[i][k] = o[EDGE + i * N * 2 + k * 2 + 1]
        }
        r.leadProb = sigmoid(o[LEAD_PROB])
        r.leadX = o[LEAD]; r.leadY = o[LEAD + 1]; r.leadV = o[LEAD + 2]
        r.poseVx = o[POSE]; r.poseVy = o[POSE + 1]; r.poseVz = o[POSE + 2]; r.poseRz = o[POSE + 5]
        // desire_pred: 4 moc x 8 desire, softmax moc dau
        var mx = -1e9f
        for (j in 0..7) mx = max(mx, o[DESIRE + j])
        var s = 0f
        val e = FloatArray(8)
        for (j in 0..7) { e[j] = exp((o[DESIRE + j] - mx).toDouble()).toFloat(); s += e[j] }
        r.desireL = e[3] / s
        r.desireR = e[4] / s
        System.arraycopy(o, HIDDEN, r.hidden, 0, HID_N)
    }

    fun parsePlan(p: FloatArray, r: Out) {
        for (k in 0 until N) {
            r.planX[k] = p[k * 15]; r.planY[k] = p[k * 15 + 1]; r.planZ[k] = p[k * 15 + 2]
        }
        r.planOk = true
    }

    /** Khoang cach an toan: Thong tu 38 tu 60 km/h; duoi 60 dung 1 giay (toi thieu 8 m). */
    fun safeDist(vKmh: Float): Float = when {
        vKmh < 60 -> max(8f, vKmh / 3.6f)
        vKmh < 80 -> 55f
        vKmh < 100 -> 70f
        else -> 100f
    }

    // ---- float16 <-> float32 (mo hinh openpilot dung fp16) ----
    fun halfToFloat(h: Int): Float {
        val s = (h shr 15) and 1
        val e = (h shr 10) and 0x1f
        val m = h and 0x3ff
        val bits = when {
            e == 0 -> if (m == 0) s shl 31 else {
                var mm = m; var ee = -14
                while (mm and 0x400 == 0) { mm = mm shl 1; ee-- }
                (s shl 31) or ((ee + 127) shl 23) or ((mm and 0x3ff) shl 13)
            }
            e == 31 -> (s shl 31) or (0xff shl 23) or (m shl 13)
            else -> (s shl 31) or ((e - 15 + 127) shl 23) or (m shl 13)
        }
        return java.lang.Float.intBitsToFloat(bits)
    }

    fun floatToHalf(f: Float): Short {
        val b = java.lang.Float.floatToIntBits(f)
        val s = (b ushr 16) and 0x8000
        val v = (b and 0x7fffffff)
        if (v >= 0x47800000) return (s or 0x7c00).toShort()          // qua lon -> vo cung
        if (v < 0x38800000) {                                         // so nho -> denormal / 0
            if (v < 0x33000000) return s.toShort()
            val e = v ushr 23
            val m = (v and 0x7fffff) or 0x800000
            return (s or (m ushr (126 - e))).toShort()
        }
        return (s or (((v - 0x38000000) + 0x1000) ushr 13)).toShort()
    }
}
