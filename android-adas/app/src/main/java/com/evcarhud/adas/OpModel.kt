package com.evcarhud.adas

import ai.onnxruntime.providers.NNAPIFlags
import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import android.content.Context
import android.util.Log
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.ShortBuffer
import java.util.EnumSet
import kotlin.math.abs

/**
 * Chay 2 mo hinh openpilot bang ONNX Runtime:
 *  - driving_vision.onnx: 2 anh (img, big_img) -> lan, mep duong, xe truoc, hidden_state
 *  - driving_policy.onnx: 25 hidden_state (5 giay) -> duong di (plan)
 * accel: "npu" thu NNAPI (chip AI cua dien thoai, mat hon) va tu kiem voi CPU, sai/loi thi ve CPU; "cpu" chi CPU 2 nhan.
 */
class OpModel(ctx: Context, accel: String) : AutoCloseable {
    private val env = OrtEnvironment.getEnvironment()
    private var vision: OrtSession
    private val visionFile: File
    /** Phien CPU phu de doi chieu NPU tren khung that (mo khi can). */
    private var refCpu: OrtSession? = null
    private val policy: OrtSession?
    var backend = "CPU"
        private set

    private val imgBuf = ByteBuffer.allocateDirect(2 * Op.PACK)
    private val bigBuf = ByteBuffer.allocateDirect(2 * Op.PACK)
    private val featBuf: ShortBuffer = ByteBuffer.allocateDirect(Op.FEAT_N * Op.HID_N * 2).order(ByteOrder.nativeOrder()).asShortBuffer()
    private val desBuf: ShortBuffer = ByteBuffer.allocateDirect(Op.FEAT_N * 8 * 2).order(ByteOrder.nativeOrder()).asShortBuffer()
    private val tcBuf: ShortBuffer = ByteBuffer.allocateDirect(4).order(ByteOrder.nativeOrder()).asShortBuffer()
    private val vOut = FloatArray(Op.V_OUT)
    private val pOut = FloatArray(Op.P_OUT)

    /** Ket qua tu kiem NPU (hien trong Cai dat). */
    var selfTest = ""
        private set

    init {
        val vf = asset(ctx, "driving_vision.onnx") ?: throw IllegalStateException("Thiếu mô hình driving_vision.onnx")
        visionFile = vf
        val pf = asset(ctx, "driving_policy.onnx")
        vision = if (accel == "npu") openChecked(ctx, vf) else cpu(vf)
        // mo hinh duong di nho (vai chuc ms tren CPU) va khong co cach tu kiem rieng: luon chay CPU cho chac
        policy = pf?.let { try { cpu(it) } catch (e: Exception) { Log.w(TAG, "policy", e); null } }
        // traffic_convention = [1, 0] (xe di ben phai, nhu Viet Nam)
        tcBuf.put(0, Op.floatToHalf(1f)); tcBuf.put(1, Op.floatToHalf(0f))
        for (i in 0 until Op.FEAT_N * 8) desBuf.put(i, 0)
    }

    private fun cpu(f: File): OrtSession {
        val o = OrtSession.SessionOptions()
        o.setIntraOpNumThreads(2)        // 2 nhan: du nhanh, it nong hon dung het nhan
        o.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
        return env.createSession(f.absolutePath, o)
    }

    private fun nnapi(f: File, fp16: Boolean): OrtSession {
        val o = OrtSession.SessionOptions()
        o.addNnapi(if (fp16) EnumSet.of(NNAPIFlags.USE_FP16) else EnumSet.noneOf(NNAPIFlags::class.java))
        o.setIntraOpNumThreads(2)
        return env.createSession(f.absolutePath, o)
    }

    /**
     * NPU cua moi hang tinh khac nhau, co may cho ket qua sai (mo hinh "mu": lan ve giong het moi canh, khong thay xe).
     * Chay 1 khung mau (cao toc Test07) tren CPU lam chuan, roi thu NPU fp16 -> NPU fp32; lech thi dung CPU.
     */
    private fun openChecked(ctx: Context, vf: File): OrtSession {
        val img = try { ctx.assets.open("selftest_img.u8").use { it.readBytes() } } catch (_: Exception) { null }
        val big = try { ctx.assets.open("selftest_big.u8").use { it.readBytes() } } catch (_: Exception) { null }
        val c = cpu(vf)
        if (img == null || big == null || img.size != 2 * Op.PACK || big.size != 2 * Op.PACK) {
            selfTest = "không có khung mẫu"; backend = "CPU"; return c
        }
        val ref = Op.Out().also { Op.parseVision(run(c, img, big).copyOf(), it) }
        selfTest = "mẫu CPU: xe ${(ref.leadProb * 100).toInt()}% vạch ${(ref.laneProb[1] * 100).toInt()}/${(ref.laneProb[2] * 100).toInt()}% · "
        for (fp16 in booleanArrayOf(true, false)) {
            val name = if (fp16) "NPU" else "NPU32"
            var s: OrtSession? = null
            try {
                s = nnapi(vf, fp16)
                val o = Op.Out().also { Op.parseVision(run(s, img, big).copyOf(), it) }
                val err = diff(ref, o)
                Log.i(TAG, "tu kiem $name lech $err")
                if (err < 0.35f) {
                    c.close(); backend = name
                    selfTest += "$name khớp CPU (lệch ${"%.2f".format(err)})"
                    return s
                }
                selfTest += "$name sai (lệch ${"%.1f".format(err)}) · "
                s.close()
            } catch (e: Exception) {
                Log.w(TAG, "NNAPI $name loi", e)
                selfTest += "$name lỗi · "
                try { s?.close() } catch (_: Exception) {}
            }
        }
        selfTest += "dùng CPU"
        backend = "CPU"
        return c
    }

    /** Do lech giua 2 ket qua: vi tri 2 vach lan gan (m), xac suat vach, xac suat xe truoc. */
    private fun diff(a: Op.Out, b: Op.Out): Float {
        var e = 0f
        for (i in 1..2) for (k in 0 until 10) e = maxOf(e, abs(a.laneY[i][k] - b.laneY[i][k]))
        for (i in 0..3) e = maxOf(e, 2 * abs(a.laneProb[i] - b.laneProb[i]))
        e = maxOf(e, 2 * abs(a.leadProb - b.leadProb))
        if (e.isNaN()) e = 99f
        return e
    }

    private fun run(s: OrtSession, img: ByteArray, big: ByteArray): FloatArray {
        imgBuf.clear(); imgBuf.put(img); imgBuf.flip()
        bigBuf.clear(); bigBuf.put(big); bigBuf.flip()
        val shape = longArrayOf(1, 12, 128, 256)
        OnnxTensor.createTensor(env, imgBuf, shape, OnnxJavaType.UINT8).use { ti ->
            OnnxTensor.createTensor(env, bigBuf, shape, OnnxJavaType.UINT8).use { tb ->
                s.run(mapOf("img" to ti, "big_img" to tb)).use { r -> read(r.get(0) as OnnxTensor, vOut) }
            }
        }
        return vOut
    }

    val hasPolicy get() = policy != null

    /**
     * Doi chieu: chay lai khung that tren CPU (chi khi dang dung NPU). Khung mau luc mo co the khop
     * ma khung that van sai (vd. xe truoc), nen kiem them dinh ky trong luc chay.
     */
    fun crossCheck(img: ByteArray, big: ByteArray): FloatArray? {
        if (backend == "CPU") return null
        val c = refCpu ?: try { cpu(visionFile).also { refCpu = it } } catch (e: Exception) { Log.w(TAG, "ref cpu", e); return null }
        return run(c, img, big).copyOf()
    }

    /** NPU sai tren khung that: chuyen han sang CPU. */
    fun fallbackCpu(why: String) {
        val c = refCpu ?: try { cpu(visionFile) } catch (e: Exception) { Log.w(TAG, "fallback", e); return }
        refCpu = null
        try { vision.close() } catch (_: Exception) {}
        vision = c; backend = "CPU"
        selfTest += " · $why → chuyển CPU"
    }

    /** img/big: 2 khung dong goi lien nhau (cu truoc, moi sau), moi khung Op.PACK byte. */
    fun runVision(img: ByteArray, big: ByteArray): FloatArray = run(vision, img, big)

    /** feat: 25 x 512 hidden_state (cu -> moi). */
    fun runPolicy(feat: FloatArray): FloatArray? {
        val p = policy ?: return null
        for (i in feat.indices) featBuf.put(i, Op.floatToHalf(feat[i]))
        featBuf.rewind(); desBuf.rewind(); tcBuf.rewind()
        OnnxTensor.createTensor(env, featBuf, longArrayOf(1, Op.FEAT_N.toLong(), 512), OnnxJavaType.FLOAT16).use { tf ->
            OnnxTensor.createTensor(env, desBuf, longArrayOf(1, Op.FEAT_N.toLong(), 8), OnnxJavaType.FLOAT16).use { td ->
                OnnxTensor.createTensor(env, tcBuf, longArrayOf(1, 2), OnnxJavaType.FLOAT16).use { tt ->
                    p.run(mapOf("features_buffer" to tf, "desire_pulse" to td, "traffic_convention" to tt)).use { r ->
                        read(r.get(0) as OnnxTensor, pOut)
                    }
                }
            }
        }
        return pOut
    }

    private fun read(t: OnnxTensor, out: FloatArray) {
        if (t.info.type == OnnxJavaType.FLOAT16) {
            val s = t.shortBuffer
            val n = minOf(out.size, s.remaining())
            for (i in 0 until n) out[i] = Op.halfToFloat(s.get(i).toInt() and 0xffff)
        } else {
            val f = t.floatBuffer
            val n = minOf(out.size, f.remaining())
            f.get(out, 0, n)
        }
    }

    override fun close() {
        try { vision.close() } catch (_: Exception) {}
        try { refCpu?.close() } catch (_: Exception) {}
        try { policy?.close() } catch (_: Exception) {}
    }

    companion object {
        private const val TAG = "OpModel"

        /** Chep mo hinh tu assets ra bo nho app (1 lan) de ONNX Runtime mo theo duong dan. */
        fun asset(ctx: Context, name: String): File? {
            val f = File(ctx.filesDir, name)
            try {
                ctx.assets.openFd(name).use { fd ->
                    if (f.exists() && f.length() == fd.length) return f
                }
            } catch (_: Exception) {
                // khong mo duoc dang fd (bi nen): kiem tra ton tai bang open
            }
            return try {
                val tmp = File(ctx.filesDir, "$name.tmp")
                ctx.assets.open(name).use { i -> tmp.outputStream().use { o -> i.copyTo(o, 1 shl 16) } }
                tmp.renameTo(f)
                f
            } catch (e: Exception) {
                if (f.exists()) f else null
            }
        }
    }
}
