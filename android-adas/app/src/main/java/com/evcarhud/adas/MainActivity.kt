package com.evcarhud.adas

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Matrix
import android.graphics.RectF
import android.graphics.SurfaceTexture
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import kotlin.math.max

/**
 * Man hinh chinh kieu C: camera toan man + lop ve + o nhiet va ban do keo duoc.
 * Thoat ra (vd. mo Waze/GOFA) thi dich vu van chay nen, chi bo ve camera cho mat may.
 */
class MainActivity : Activity() {
    private lateinit var prefs: Prefs
    private lateinit var root: FrameLayout
    private lateinit var tex: TextureView
    private lateinit var overlay: Overlay
    private lateinit var temp: TempPip
    private lateinit var map: TileMap
    private var svc: AdasService? = null
    private var surface: Surface? = null
    private var started = false

    private val conn = object : ServiceConnection {
        override fun onServiceConnected(n: ComponentName?, b: IBinder?) {
            val s = (b as AdasService.LocalBinder).service
            svc = s
            s.camInfo()?.let { overlay.imgW = it.size.width; overlay.imgH = it.size.height }
            s.onSnap = { sn -> overlay.snap = sn; temp.snap = sn; if (map.visibility == View.VISIBLE) map.update(sn) }
            s.uiVisible = started
            attachPreview()
        }
        override fun onServiceDisconnected(n: ComponentName?) { svc = null }
    }

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        prefs = Prefs(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        root = FrameLayout(this).apply { setBackgroundColor(0xff0b0e13.toInt()) }
        tex = TextureView(this)
        overlay = Overlay(this)
        temp = TempPip(this)
        map = TileMap(this)
        root.addView(tex, FrameLayout.LayoutParams(-1, -1))
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        root.addView(temp, FrameLayout.LayoutParams(1, 1))
        root.addView(map, FrameLayout.LayoutParams(1, 1))
        setContentView(root)
        root.addOnLayoutChangeListener { _, l, t, r, btm, ol, ot, orr, ob ->
            if (r - l != orr - ol || btm - t != ob - ot) sizePips(r - l)
        }
        overlay.onGear = { settings() }
        overlay.onCalibDone = { ok ->
            if (ok) svc?.setManualVp(overlay.vpX, overlay.vpY)
            showPips()
        }
        tex.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) { attachPreview() }
            override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) { transform() }
            override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean {
                svc?.setPreview(null); surface?.release(); surface = null; return true
            }
            override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
        }
        askPerms()
    }

    private fun sizePips(w: Int) {
        val u = w / 100f
        val topMin = { 2.2f * u + 4.6f * u + 1.2f * u + 6f * u }   // duoi hang thong so + so toc do
        temp.layoutParams = FrameLayout.LayoutParams((27 * u).toInt(), (6.2f * u).toInt())
        map.layoutParams = FrameLayout.LayoutParams((26 * u).toInt(), (19.5f * u).toInt())
        val pad = 1.8f * u
        makeDraggable(temp, topMin, pad, prefs.tempCorner) { prefs.tempCorner = it }
        makeDraggable(map, topMin, pad, prefs.mapCorner) { prefs.mapCorner = it }
        showPips()
    }

    private fun showPips() {
        val c = overlay.calib
        temp.visibility = if (prefs.showTemp && !c) View.VISIBLE else View.GONE
        map.visibility = if (prefs.showMap && !c) View.VISIBLE else View.GONE
    }

    private fun attachPreview() {
        val s = svc ?: return
        val st = tex.surfaceTexture ?: return
        if (!started) return
        val sz = s.camInfo()?.size ?: return
        st.setDefaultBufferSize(sz.width, sz.height)
        if (surface == null) surface = Surface(st)
        transform()
        s.setPreview(surface)
    }

    /** Xoay + phong anh camera cho day man hinh ngang, cat giua (cung cach lop ve tinh toa do). */
    private fun transform() {
        val sz = svc?.camInfo()?.size ?: return
        val vw = tex.width.toFloat(); val vh = tex.height.toFloat()
        if (vw == 0f || vh == 0f) return
        @Suppress("DEPRECATION")
        val rot = windowManager.defaultDisplay.rotation
        val m = Matrix()
        val view = RectF(0f, 0f, vw, vh)
        val buf = RectF(0f, 0f, sz.height.toFloat(), sz.width.toFloat())
        val cx = view.centerX(); val cy = view.centerY()
        if (rot == Surface.ROTATION_90 || rot == Surface.ROTATION_270) {
            buf.offset(cx - buf.centerX(), cy - buf.centerY())
            m.setRectToRect(view, buf, Matrix.ScaleToFit.FILL)
            val sc = max(vh / sz.height, vw / sz.width)
            m.postScale(sc, sc, cx, cy)
            m.postRotate(90f * (rot - 2), cx, cy)
        }
        tex.setTransform(m)
    }

    override fun onStart() {
        super.onStart()
        started = true
        svc?.uiVisible = true
        attachPreview()
    }

    override fun onStop() {
        started = false
        // ra nen: bo ve camera, AI van chay trong dich vu
        svc?.setPreview(null)
        svc?.uiVisible = false
        super.onStop()
    }

    override fun onDestroy() {
        svc?.onSnap = null
        if (svc != null) try { unbindService(conn) } catch (_: Exception) {}
        surface?.release()
        super.onDestroy()
    }

    // ---------------- quyen ----------------
    private fun askPerms() {
        val need = mutableListOf(Manifest.permission.CAMERA, Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        if (Build.VERSION.SDK_INT >= 31) { need += Manifest.permission.BLUETOOTH_SCAN; need += Manifest.permission.BLUETOOTH_CONNECT }
        if (Build.VERSION.SDK_INT >= 33) need += Manifest.permission.POST_NOTIFICATIONS
        val missing = need.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) startService() else requestPermissions(missing.toTypedArray(), 1)
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, res: IntArray) {
        super.onRequestPermissionsResult(code, perms, res)
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) startService()
        else AlertDialog.Builder(this).setMessage("App cần quyền Camera để nhìn làn đường.").setPositiveButton("OK") { _, _ -> finish() }.show()
    }

    private fun startService() {
        AdasService.start(this)
        bindService(Intent(this, AdasService::class.java), conn, Context.BIND_AUTO_CREATE)
    }

    // ---------------- cai dat ----------------
    private fun startCalib() {
        // dau + bat dau o diem tu dang dung
        val g = svc?.currentGeom()
        val p = FloatArray(2)
        if (g != null && g.project(1000f, 0f, 0f, p, 0)) { overlay.vpX = p[0] / g.w; overlay.vpY = p[1] / g.h }
        else { overlay.vpX = 0.5f; overlay.vpY = 0.5f }
        overlay.calib = true
        showPips()
    }

    private fun settings() {
        val items = arrayOf(
            "Tự hiệu chỉnh camera: " + (if (prefs.autoCalib) "BẬT" else "TẮT"),
            "Hiệu chỉnh tay (kéo điểm tụ)",
            "Chiều cao camera: ${"%.1f".format(prefs.camH)} m",
            "Bộ xử lý AI: " + (if (prefs.accel == "npu") "NPU (mát hơn)" else "CPU"),
            (if (prefs.showMap) "Ẩn" else "Hiện") + " bản đồ góc",
            (if (prefs.showTemp) "Ẩn" else "Hiện") + " ô nhiệt độ",
            "Tìm lại HUD",
            "Giới thiệu & giấy phép",
            "Thoát hẳn (tắt chạy nền)",
        )
        AlertDialog.Builder(this).setTitle("Cài đặt").setItems(items) { _, i ->
            when (i) {
                0 -> svc?.setAuto(!prefs.autoCalib)
                1 -> startCalib()
                2 -> {
                    val hs = arrayOf(0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.7f, 1.9f)
                    AlertDialog.Builder(this).setTitle("Camera cách mặt đường")
                        .setItems(hs.map { "%.1f m".format(it) }.toTypedArray()) { _, k -> prefs.camH = hs[k]; svc?.reloadSettings(false) }.show()
                }
                3 -> { prefs.accel = if (prefs.accel == "npu") "cpu" else "npu"; svc?.reloadSettings(true) }
                4 -> { prefs.showMap = !prefs.showMap; showPips() }
                5 -> { prefs.showTemp = !prefs.showTemp; showPips() }
                6 -> svc?.findHudAgain()
                7 -> AlertDialog.Builder(this).setTitle("EVCarHUD Làn").setMessage(ABOUT).setPositiveButton("OK", null).show()
                8 -> {
                    startService(Intent(this, AdasService::class.java).setAction(AdasService.ACTION_STOP))
                    finish()
                }
            }
        }.show()
    }

    companion object {
        private const val ABOUT = "Nhận diện làn đường, mép đường, xe phía trước và đường đi dùng mô hình openpilot " +
            "(driving_vision, driving_policy v0.10.3) của comma.ai, giấy phép MIT.\n\n" +
            "Chỉ hỗ trợ cảnh báo, không thay người lái. Bản đồ © OpenStreetMap contributors.\n\n" +
            "Chống nóng máy: AI chạy 5–20 lần/giây theo tốc độ, dừng khi xe đỗ hoặc máy quá nóng. " +
            "Nên gắn điện thoại chỗ cửa gió điều hoà, sạc 18–20 W."
    }
}
