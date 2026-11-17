package com.evcarhud.adas

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout

/**
 * Man hinh chinh kieu C: camera toan man + lop ve + o nhiet va ban do keo duoc.
 * Thoat ra (vd. mo Waze/GOFA) thi dich vu van chay nen, chi bo ve camera cho mat may.
 */
class MainActivity : Activity() {
    private lateinit var prefs: Prefs
    private lateinit var root: FrameLayout
    private lateinit var overlay: Overlay
    private lateinit var temp: TempPip
    private lateinit var map: TileMap
    private var svc: AdasService? = null
    private var started = false

    private val conn = object : ServiceConnection {
        override fun onServiceConnected(n: ComponentName?, b: IBinder?) {
            val s = (b as AdasService.LocalBinder).service
            svc = s
            s.camInfo()?.let { overlay.imgW = it.size.width; overlay.imgH = it.size.height }
            s.onSnap = { sn -> overlay.snap = sn; temp.snap = sn; if (map.visibility == View.VISIBLE) map.update(sn) }
            s.onPreview = { bm -> overlay.frame = bm }
            s.uiVisible = started
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
        overlay = Overlay(this)
        temp = TempPip(this)
        map = TileMap(this).apply { source = prefs.mapSrc }
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
        askPerms()
        handleNav(intent)
    }

    override fun onNewIntent(i: Intent) {
        super.onNewIntent(i)
        setIntent(i)
        handleNav(i)
    }

    /** Diem dich tu app EVCarHUD Charge / link chia se HERE WeGo, Google Maps -> dan duong tren ban do PiP. */
    private fun handleNav(i: Intent?) {
        val q = ShareLink.quick(i)
        if (q != null) { goNav(q); return }
        if (i == null || !ShareLink.isShare(i)) return
        toast("Đang đọc link địa điểm…")
        Thread {
            val d = try { ShareLink.resolve(this, i) } catch (e: Exception) { null }
            runOnUiThread { if (d != null) goNav(d) else toast("Link này không có toạ độ, thử chia sẻ lại từ HERE WeGo / Google Maps") }
        }.start()
    }

    private fun goNav(d: Dest) {
        Nav.start(this, d, prefs.hereKey)
        if (!prefs.showMap) { prefs.showMap = true; showPips() }
        toast("Dẫn đường tới ${d.name}")
    }

    private fun toast(t: String) = android.widget.Toast.makeText(this, t, android.widget.Toast.LENGTH_SHORT).show()

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

    override fun onStart() {
        super.onStart()
        started = true
        svc?.uiVisible = true
    }

    override fun onStop() {
        started = false
        // ra nen: bo ve camera, AI van chay trong dich vu
        svc?.uiVisible = false
        overlay.frame = null
        super.onStop()
    }

    override fun onDestroy() {
        svc?.onSnap = null
        svc?.onPreview = null
        if (svc != null) try { unbindService(conn) } catch (_: Exception) {}
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
        val navItems = arrayOf(
            if (Nav.active) "Kết thúc dẫn đường tới ${Nav.dest?.name}" else "Dẫn đường: mở trạm từ app EVCarHUD Charge hoặc chia sẻ link HERE WeGo / Google Maps",
            "Khoá HERE (tính đường): " + (if (prefs.hereKey.isEmpty()) "chưa có, đang dùng OSRM" else "đã lưu …" + prefs.hereKey.takeLast(4)),
        )
        val items = navItems + arrayOf(
            "Tự hiệu chỉnh camera: " + (if (prefs.autoCalib) "BẬT" else "TẮT"),
            "Hiệu chỉnh tay (kéo điểm tụ)",
            "Chiều cao camera: ${"%.1f".format(prefs.camH)} m",
            "Bộ xử lý AI: " + (if (prefs.accel == "npu") "NPU (mát hơn)" else "CPU"),
            (if (prefs.showMap) "Ẩn" else "Hiện") + " bản đồ góc",
            "Nguồn bản đồ: " + mapName(prefs.mapSrc),
            (if (prefs.showTemp) "Ẩn" else "Hiện") + " ô nhiệt độ",
            "Tìm lại HUD",
            "Xem ảnh mô hình AI đang thấy",
            "Dò cam gương Wi-Fi",
            "Chẩn đoán AI (xem số liệu + ghi 1 khung)",
            "Giới thiệu & giấy phép",
            "Thoát hẳn (tắt chạy nền)",
        )
        AlertDialog.Builder(this).setTitle("Cài đặt\n" + (svc?.calibInfo() ?: "")).setItems(items) { _, k ->
            if (k == 0) { if (Nav.active) Nav.stop(); return@setItems }
            if (k == 1) { hereKeyDialog(); return@setItems }
            when (k - navItems.size) {
                0 -> svc?.setAuto(!prefs.autoCalib)
                1 -> startCalib()
                2 -> {
                    val hs = arrayOf(0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.7f, 1.9f)
                    AlertDialog.Builder(this).setTitle("Camera cách mặt đường")
                        .setItems(hs.map { "%.1f m".format(it) }.toTypedArray()) { _, k -> prefs.camH = hs[k]; svc?.reloadSettings(false) }.show()
                }
                3 -> { prefs.accel = if (prefs.accel == "npu") "cpu" else "npu"; svc?.reloadSettings(true) }
                4 -> { prefs.showMap = !prefs.showMap; showPips() }
                5 -> {
                    val ks = arrayOf("google", "osm")
                    AlertDialog.Builder(this).setTitle("Nguồn bản đồ").setItems(ks.map { mapName(it) }.toTypedArray()) { _, k ->
                        prefs.mapSrc = ks[k]; map.source = ks[k]
                    }.show()
                }
                6 -> { prefs.showTemp = !prefs.showTemp; showPips() }
                7 -> svc?.findHudAgain()
                8 -> showModelView()
                9 -> probeCam()
                10 -> diag()
                11 -> AlertDialog.Builder(this).setTitle("EVCarHUD Làn").setMessage(ABOUT).setPositiveButton("OK", null).show()
                12 -> {
                    startService(Intent(this, AdasService::class.java).setAction(AdasService.ACTION_STOP))
                    finish()
                }
            }
        }.show()
    }

    private fun hereKeyDialog() {
        val e = android.widget.EditText(this).apply {
            setText(prefs.hereKey); hint = "Dán API key từ platform.here.com"; setSingleLine()
        }
        AlertDialog.Builder(this).setTitle("Khoá HERE Routing")
            .setMessage("Dùng để tính đường (chính xác hơn ở Việt Nam). Để trống thì dùng OSRM miễn phí. Khoá chỉ lưu trên máy này.")
            .setView(e)
            .setPositiveButton("Lưu") { _, _ -> prefs.hereKey = e.text.toString(); toast(if (prefs.hereKey.isEmpty()) "Dùng OSRM" else "Đã lưu khoá HERE") }
            .setNegativeButton("Huỷ", null).show()
    }

    /** Anh xam ma mo hinh nhan (tren: goc hep nhin xa, duoi: goc rong) de kiem tra hieu chinh / anh dau vao. */
    private fun showModelView() {
        val bm = svc?.modelView()
        if (bm == null) {
            AlertDialog.Builder(this).setMessage("Chưa có khung nào (AI đang dừng hoặc chưa chạy).").setPositiveButton("OK", null).show()
            return
        }
        val iv = android.widget.ImageView(this).apply {
            setImageBitmap(bm); adjustViewBounds = true; scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(android.graphics.Color.BLACK)
        }
        AlertDialog.Builder(this).setTitle("Ảnh mô hình thấy · " + (svc?.aiInfo() ?: ""))
            .setView(iv).setPositiveButton("Đóng", null).show()
    }

    /** So lieu chan doan day du (khong bi cat nhu tieu de) + nut ghi 1 khung de gui phan tich. */
    private fun diag() {
        val s = svc ?: return
        AlertDialog.Builder(this).setTitle("Chẩn đoán AI").setMessage(s.calibInfo())
            .setPositiveButton("Ghi 1 khung") { _, _ ->
                android.widget.Toast.makeText(this, "Đang ghi khung tiếp theo…", android.widget.Toast.LENGTH_SHORT).show()
                s.requestDump { m -> AlertDialog.Builder(this).setMessage(m).setPositiveButton("OK", null).show() }
            }.setNeutralButton(if (prefs.showDbg) "Ẩn khỏi màn" else "Hiện trên màn") { _, _ -> prefs.showDbg = !prefs.showDbg }
            .setNegativeButton("Đóng", null).show()
    }

    /** Do cam guong Wi-Fi, hien ket qua tung dong + nut sao chep. */
    private fun probeCam() {
        val tv = android.widget.TextView(this).apply { setTextIsSelectable(true); textSize = 12f; setPadding(32, 16, 32, 16); text = "Đang dò… (khoảng 30 giây)\n" }
        val sv = android.widget.ScrollView(this).apply { addView(tv) }
        val sb = StringBuilder()
        AlertDialog.Builder(this).setTitle("Dò cam gương Wi-Fi").setView(sv)
            .setPositiveButton("Sao chép") { _, _ ->
                (getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager)
                    .setPrimaryClip(android.content.ClipData.newPlainText("do-cam", sb.toString()))
            }.setNegativeButton("Đóng", null).show()
        Thread {
            Probe.run(this) { line -> runOnUiThread { sb.append(line).append('\n'); tv.text = sb.toString() } }
        }.start()
    }

    private fun mapName(k: String) = when (k) { "osm" -> "OpenStreetMap"; else -> "Google (thử nghiệm, nền tối)" }

    companion object {
        private const val ABOUT = "Nhận diện làn đường, mép đường, xe phía trước và đường đi dùng mô hình openpilot " +
            "(driving_vision, driving_policy v0.10.3) của comma.ai, giấy phép MIT.\n\n" +
            "Chỉ hỗ trợ cảnh báo, không thay người lái. Bản đồ © OpenStreetMap contributors.\n\n" +
            "Chống nóng máy: AI chạy 5–20 lần/giây theo tốc độ, dừng khi xe đỗ hoặc máy quá nóng. " +
            "Nên gắn điện thoại chỗ cửa gió điều hoà, sạc 18–20 W."
    }
}
