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

/**
 * Chay 2 mo hinh openpilot bang ONNX Runtime:
 *  - driving_vision.onnx: 2 anh (img, big_img) -> lan, mep duong, xe truoc, hidden_state
 *  - driving_policy.onnx: 25 hidden_state (5 giay) -> duong di (plan)
 * accel: "npu" thu NNAPI (chip AI cua dien thoai, mat hon), loi thi tu ve CPU; "cpu" chi CPU 2 nhan.
 */
class OpModel(ctx: Context, accel: String) : AutoCloseable {
    private val env = OrtEnvironment.getEnvironment()
    private val vision: OrtSession
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

    init {
        val vf = asset(ctx, "driving_vision.onnx") ?: throw IllegalStateException("Thiếu mô hình driving_vision.onnx")
        val pf = asset(ctx, "driving_policy.onnx")
        vision = open(vf, accel)
        policy = pf?.let { try { open(it, accel) } catch (e: Exception) { Log.w(TAG, "policy", e); null } }
        // traffic_convention = [1, 0] (xe di ben phai, nhu Viet Nam)
        tcBuf.put(0, Op.floatToHalf(1f)); tcBuf.put(1, Op.floatToHalf(0f))
        for (i in 0 until Op.FEAT_N * 8) desBuf.put(i, 0)
    }

    private fun open(f: File, accel: String): OrtSession {
        if (accel == "npu") {
            try {
                val o = OrtSession.SessionOptions()
                o.addNnapi(EnumSet.of(NNAPIFlags.USE_FP16))
                o.setIntraOpNumThreads(2)
                val s = env.createSession(f.absolutePath, o)
                backend = "NPU"
                return s
            } catch (e: Exception) {
                Log.w(TAG, "NNAPI loi, dung CPU", e)
            }
        }
        val o = OrtSession.SessionOptions()
        o.setIntraOpNumThreads(2)        // 2 nhan: du nhanh, it nong hon dung het nhan
        o.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
        backend = "CPU"
        return env.createSession(f.absolutePath, o)
    }

    val hasPolicy get() = policy != null

    /** img/big: 2 khung dong goi lien nhau (cu truoc, moi sau), moi khung Op.PACK byte. */
    fun runVision(img: ByteArray, big: ByteArray): FloatArray {
        imgBuf.clear(); imgBuf.put(img); imgBuf.flip()
        bigBuf.clear(); bigBuf.put(big); bigBuf.flip()
        val shape = longArrayOf(1, 12, 128, 256)
        OnnxTensor.createTensor(env, imgBuf, shape, OnnxJavaType.UINT8).use { ti ->
            OnnxTensor.createTensor(env, bigBuf, shape, OnnxJavaType.UINT8).use { tb ->
                vision.run(mapOf("img" to ti, "big_img" to tb)).use { r -> read(r.get(0) as OnnxTensor, vOut) }
            }
        }
        return vOut
    }

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
