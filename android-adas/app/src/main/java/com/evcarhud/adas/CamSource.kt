package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.media.Image
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import kotlin.math.abs
import kotlin.math.atan
import kotlin.math.max

/**
 * Camera sau qua Camera2 (khong dung thu vien ngoai cho nhe).
 * Luon co luong anh YUV 1280x720 cho mo hinh; khung xem truoc (preview) chi gan khi man hinh app dang mo,
 * chay nen thi bo preview de do ton dien/nong.
 */
/** Nguon khung YUV cho mo hinh: camera dien thoai hoac cam guong Wi-Fi. */
interface FrameSource {
    val size: Size
    val focalPx: Double
    val focalSrc: String
    val flip: Boolean
    val isOpen: Boolean
    fun open()
    fun close()
    fun setRate(hz: Int)
    fun setPreview(s: Surface?)
    fun release()
}

/** Boc android.media.Image (YUV_420_888) thanh YuvFrame; close() se dong Image goc. */
fun Image.toYuvFrame(): YuvFrame {
    val im = this
    val ps = im.planes.map { YuvFrame.Plane(it.buffer, it.rowStride, it.pixelStride) }.toTypedArray()
    return YuvFrame(im.width, im.height, im.timestamp, ps) { try { im.close() } catch (_: Exception) {} }
}

@SuppressLint("MissingPermission")
class CamSource(ctx: Context, private val onFrame: (YuvFrame) -> Unit, private val onErr: (String) -> Unit) : FrameSource {
    private val mgr = ctx.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private val thread = HandlerThread("cam").apply { start() }
    private val h = Handler(thread.looper)

    val id: String
    override val size: Size
    /** Tieu cu (px) o kich thuoc anh YUV. */
    override val focalPx: Double
    /** Nguon tieu cu (de hien trong Cai dat). */
    override val focalSrc: String
    /** Anh tho tu cam bien bi nguoc 180 do so voi man hinh ngang (cam bien xoay 270). */
    override val flip: Boolean
    private val fpsRanges: Array<Range<Int>>

    private var dev: CameraDevice? = null
    private var session: CameraCaptureSession? = null
    private var reader: ImageReader? = null
    private var preview: Surface? = null
    private var wantOpen = false
    private var fps: Range<Int>? = null

    init {
        id = mgr.cameraIdList.firstOrNull {
            mgr.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
        } ?: mgr.cameraIdList.first()
        val ch = mgr.getCameraCharacteristics(id)
        val map = ch.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)!!
        val sizes = map.getOutputSizes(ImageFormat.YUV_420_888)
        // 1280x720 neu co; khong thi khung 16:9 gan nhat, khong qua 1920
        size = sizes.firstOrNull { it.width == 1280 && it.height == 720 }
            ?: sizes.filter { abs(it.width * 9 - it.height * 16) < 32 && it.width <= 1920 }.minByOrNull { abs(it.width - 1280) }
            ?: sizes.minByOrNull { abs(it.width - 1280) + abs(it.height - 720) }!!
        val phys = ch.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)
        val arr = ch.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)
        val act = ch.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)
        // Khung 16:9 cat tu vung cam bien: cam bien 4:3 thi giu nguyen chieu ngang
        val cropW = act?.let { if (it.width() * 9 >= it.height() * 16) it.height() * 16.0 / 9 else it.width().toDouble() }
        fun hfov(f: Double) = Math.toDegrees(2 * atan(size.width / 2 / f))
        // 1) thong so noi tai cua hang (chinh xac nhat, theo diem anh vung cam bien)
        val intr = ch.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)
        val fIntr = if (intr != null && intr[0] > 1f && cropW != null) intr[0] * size.width / cropW else 0.0
        // 2) tieu cu mm: may nhieu camera gop (logical) liet ke nhieu tieu cu -> chon cai cho goc ngang gan 75 do nhat
        val fls = ch.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS) ?: floatArrayOf()
        val fMm = if (phys != null && arr != null && cropW != null && fls.isNotEmpty()) {
            val pxPerMm = arr.width / phys.width
            fls.map { it * pxPerMm * size.width / cropW }.minByOrNull { abs(hfov(it) - 75) } ?: 0.0
        } else 0.0
        val fDef = size.width / 2.0 / Math.tan(Math.toRadians(37.5))
        val pick = when {
            fIntr > 0 && hfov(fIntr) in 45.0..110.0 -> fIntr
            fMm > 0 && hfov(fMm) in 45.0..110.0 -> fMm
            else -> fDef
        }
        focalPx = pick
        focalSrc = when (pick) { fIntr -> "nội tại"; fMm -> "tiêu cự ${fls.joinToString("/") { "%.1f".format(it) }} mm"; else -> "mặc định" }
        flip = (ch.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90) == 270
        fpsRanges = ch.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES) ?: arrayOf(Range(15, 30))
        Log.i(TAG, "cam $id ${size} ($focalSrc) f=${"%.0f".format(focalPx)} px (goc ngang ${"%.0f".format(Math.toDegrees(2 * atan(size.width / 2 / focalPx)))} do) flip=$flip")
    }

    override fun open() {
        h.post {
            wantOpen = true
            if (dev != null) return@post
            try {
                mgr.openCamera(id, object : CameraDevice.StateCallback() {
                    override fun onOpened(d: CameraDevice) {
                        if (!wantOpen) { d.close(); return }
                        dev = d; startSession()
                    }
                    override fun onDisconnected(d: CameraDevice) { d.close(); if (dev == d) dev = null; onErr("Camera bị app khác chiếm") }
                    override fun onError(d: CameraDevice, e: Int) { d.close(); if (dev == d) dev = null; onErr("Lỗi camera $e") }
                }, h)
            } catch (e: Exception) { onErr("Không mở được camera: ${e.message}") }
        }
    }

    override fun close() {
        h.post {
            wantOpen = false
            try { session?.close() } catch (_: Exception) {}
            session = null
            try { dev?.close() } catch (_: Exception) {}
            dev = null
            reader?.close(); reader = null
        }
    }

    override val isOpen get() = wantOpen

    /** Gan / bo khung xem truoc. */
    override fun setPreview(s: Surface?) {
        h.post {
            if (preview == s) return@post
            preview = s
            if (dev != null) startSession()
        }
    }

    /** Toc do khung camera: chon khoang thap nhat du cho tan suat AI (it khung = it nong). */
    override fun setRate(hz: Int) {
        h.post {
            val need = max(hz, 15)
            val r = fpsRanges.filter { it.upper >= need }.minWithOrNull(compareBy({ it.upper }, { -it.lower }))
                ?: fpsRanges.maxByOrNull { it.upper }
            if (r == fps) return@post
            fps = r
            repeat()
        }
    }

    private fun startSession() {
        val d = dev ?: return
        try { session?.close() } catch (_: Exception) {}
        session = null
        if (reader == null) {
            reader = ImageReader.newInstance(size.width, size.height, ImageFormat.YUV_420_888, 3).apply {
                setOnImageAvailableListener({ r ->
                    val img = try { r.acquireLatestImage() } catch (e: Exception) { null } ?: return@setOnImageAvailableListener
                    onFrame(img.toYuvFrame())      // ben nhan phai close()
                }, h)
            }
        }
        val outs = listOfNotNull(reader!!.surface, preview)
        try {
            @Suppress("DEPRECATION")
            d.createCaptureSession(outs, object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(s: CameraCaptureSession) {
                    if (dev != d) { s.close(); return }
                    session = s; repeat()
                }
                override fun onConfigureFailed(s: CameraCaptureSession) { onErr("Camera không cấu hình được") }
            }, h)
        } catch (e: Exception) { onErr("Camera lỗi: ${e.message}") }
    }

    private fun repeat() {
        val d = dev ?: return
        val s = session ?: return
        try {
            val b = d.createCaptureRequest(CameraDevice.TEMPLATE_RECORD)
            b.addTarget(reader!!.surface)
            preview?.let { b.addTarget(it) }
            fps?.let { b.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, it) }
            b.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
            // Chong rung dien tu lam lech goc nhin -> tat de hieu chinh dung
            b.set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF)
            s.setRepeatingRequest(b.build(), null, h)
        } catch (e: Exception) { Log.w(TAG, "repeat", e) }
    }

    override fun release() { close(); thread.quitSafely() }

    companion object { private const val TAG = "CamSource" }
}
