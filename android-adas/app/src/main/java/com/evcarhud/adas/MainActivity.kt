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
import android.graphics.RectF
import android.os.Bundle
import android.os.IBinder
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import com.evcarhud.adas.SettingsView.Item.Action as A
import com.evcarhud.adas.SettingsView.Item.Header as H
import com.evcarhud.adas.SettingsView.Item.Seg as S
import com.evcarhud.adas.SettingsView.Item.Toggle as T

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
    private var scene: Scene3D? = null          // man canh 3D (tao khi bat lan dau)
    private lateinit var btn3d: android.widget.TextView
    private var started = false

    private val conn = object : ServiceConnection {
        override fun onServiceConnected(n: ComponentName?, b: IBinder?) {
            val s = (b as AdasService.LocalBinder).service
            svc = s
            s.camInfo()?.let { overlay.imgW = it.size.width; overlay.imgH = it.size.height }
            s.onSnap = { sn -> overlay.snap = sn; temp.snap = sn; if (map.visibility == View.VISIBLE) map.update(sn); scene?.update(sn) }
            s.onPreview = { bm -> overlay.frame = bm }
            s.uiVisible = started
            s.scene3d = scene?.visibility == View.VISIBLE
        }
        override fun onServiceDisconnected(n: ComponentName?) { svc = null }
    }

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        prefs = Prefs(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        // hien phien ban moi lan mo app de biet da cai dung ban (nguoi dung hoi 20/11)
        android.widget.Toast.makeText(this, "App LANE ${appVer()}", android.widget.Toast.LENGTH_LONG).show()
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        root = FrameLayout(this).apply { setBackgroundColor(0xff0b0e13.toInt()) }
        overlay = Overlay(this).also { it.overHood = prefs.overHood; it.signScale = SIGN_SCALE[prefs.signSize.coerceIn(0, 3)] }
        temp = TempPip(this)
        map = TileMap(this).apply { source = prefs.mapSrc; autoDayNight = prefs.mapAuto }
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        root.addView(temp, FrameLayout.LayoutParams(1, 1))
        root.addView(map, FrameLayout.LayoutParams(1, 1))
        btn3d = android.widget.TextView(this).apply {
            text = "3D"; textSize = 15f; setTextColor(android.graphics.Color.WHITE); gravity = android.view.Gravity.CENTER
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 999f; setColor(0xcc141b25.toInt()); setStroke(3, 0x66ffffff) }
            setOnClickListener { toggle3d() }
        }
        root.addView(btn3d, FrameLayout.LayoutParams(1, 1))
        setContentView(root)
        root.addOnLayoutChangeListener { _, l, t, r, btm, ol, ot, orr, ob ->
            if (r - l != orr - ol || btm - t != ob - ot) sizePips(r - l)
        }
        overlay.onGear = { settings() }
        overlay.split = prefs.splitScreen
        overlay.onSplit = { prefs.splitScreen = !prefs.splitScreen; overlay.split = prefs.splitScreen; sizePips(root.width); toast(if (prefs.splitScreen) "Chia đôi màn: trái camera, phải bản đồ" else "Toàn màn camera") }
        overlay.onLead = { x, y, w -> svc?.leadHint(x, y, w) }
        overlay.avoid = { if (map.visibility == View.VISIBLE && map.width > 1) RectF(map.x, map.y, map.x + map.width, map.y + map.height) else null }
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
            val d = try { ShareLink.resolve(this, i, Nav.here(), prefs.hereKey) { m -> svc?.logLine(m) } } catch (e: WazeDrive) {
                runOnUiThread { toast("Đây là link theo dõi chuyến đi của Waze, không có toạ độ điểm đến. Cứ dẫn đường trong Waze, app Làn tự lấy hướng rẽ qua Waze Mod") }
                return@Thread
            } catch (e: Exception) { svc?.logLine("LINK loi ${e.javaClass.simpleName} ${e.message}"); null }
            runOnUiThread { if (d != null) goNav(d) else toast("Không tìm được địa điểm trong link này, thử chia sẻ lại từ HERE WeGo / Google Maps") }
        }.start()
    }

    private fun goNav(d: Dest) {
        Nav.start(this, d, prefs.hereKey)
        if (!prefs.showMap) { prefs.showMap = true; showPips() }
        toast("Dẫn đường tới ${d.name}")
    }

    private fun toast(t: String) = android.widget.Toast.makeText(this, t, android.widget.Toast.LENGTH_SHORT).show()

    private fun sizePips(w: Int) {
        if (w <= 0) return
        place3dBtn(w)
        if (prefs.scene3d == 1 && !overlay.calib) { applyScene(w); return }
        if (prefs.splitScreen) { applySplit(w); return }
        val u = w / 100f
        // thoat che do chia doi: tra camera ve toan man, ban do ve cua so goc
        overlay.layoutParams = FrameLayout.LayoutParams(-1, -1).also { overlay.x = 0f; overlay.y = 0f }
        val topMin = { 2.2f * u + 4.6f * u + 1.2f * u + 6f * u }   // duoi hang thong so + so toc do
        temp.layoutParams = FrameLayout.LayoutParams((27 * u).toInt(), (6.2f * u).toInt())
        map.layoutParams = FrameLayout.LayoutParams((26 * u).toInt(), (19.5f * u).toInt())
        val pad = 1.8f * u
        makeDraggable(temp, topMin, pad, prefs.tempCorner) { prefs.tempCorner = it }
        makeDraggable(map, topMin, pad, prefs.mapCorner) { prefs.mapCorner = it }
        showPips()
    }

    /** Chia doi man: trai = camera + lop ve ADAS, phai = ban do (khong keo duoc, o nhiet an). */
    private fun applySplit(w: Int) {
        val half = w / 2
        overlay.setOnTouchListener(null)            // dam bao overlay nhan cham nut chip o nua trai
        overlay.layoutParams = FrameLayout.LayoutParams(half, -1).also { overlay.x = 0f; overlay.y = 0f }
        map.setOnTouchListener(null)                // ban do co dinh nua phai, khong keo
        map.layoutParams = FrameLayout.LayoutParams(w - half, -1).also { map.x = half.toFloat(); map.y = 0f }
        map.requestLayout(); overlay.requestLayout()
        val c = overlay.calib
        temp.visibility = View.GONE
        map.visibility = if (c) View.GONE else View.VISIBLE
        // canh 3D thay ban do: nam dung cho nua phai
        if (prefs.scene3d == 2 && !c) { map.visibility = View.GONE; showScene(half.toFloat(), 0f, w - half, -1) } else hideScene()
        overlay.invalidate()
    }

    // ---------------- canh 3D kieu Tesla ----------------
    private fun ensureScene(): Scene3D = scene ?: Scene3D(this).also { sc ->
        scene = sc
        sc.view = if (prefs.scene3dHigh) "high" else "chase"
        root.addView(sc, root.indexOfChild(overlay) + 1, FrameLayout.LayoutParams(1, 1))
    }

    private fun showScene(x: Float, y: Float, w: Int, h: Int) {
        val sc = ensureScene()
        sc.layoutParams = FrameLayout.LayoutParams(w, h); sc.x = x; sc.y = y
        if (sc.visibility != View.VISIBLE) { sc.visibility = View.VISIBLE; sc.resume() }
        svc?.scene3d = true
        btn3d.bringToFront()
    }

    private fun hideScene() {
        val sc = scene ?: return
        if (sc.visibility == View.VISIBLE) { sc.visibility = View.GONE; sc.pause() }
        svc?.scene3d = false
    }

    /** Toan man 3D: camera thu nho thanh cua so goc tren-phai (cham van vao nut cai dat / chia doi duoc). */
    private fun applyScene(w: Int) {
        val u = w / 100f
        showScene(0f, 0f, -1, -1)
        val pw = (34 * u).toInt(); val ph = pw * 9 / 16
        overlay.setOnTouchListener(null)
        overlay.layoutParams = FrameLayout.LayoutParams(pw, ph); overlay.x = w - pw - 1.6f * u; overlay.y = 1.6f * u
        overlay.bringToFront(); btn3d.bringToFront()
        temp.visibility = View.GONE; map.visibility = View.GONE
        overlay.requestLayout(); overlay.invalidate()
    }

    private fun place3dBtn(w: Int) {
        val u = w / 100f; val s = (5.2f * u).toInt()
        btn3d.layoutParams = FrameLayout.LayoutParams(s, s)
        btn3d.x = 1.8f * u; btn3d.y = root.height * 0.52f
        btn3d.background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 999f
            setColor(if (prefs.scene3d != 0) 0xee1e9e5a.toInt() else 0xcc141b25.toInt()); setStroke(3, 0x66ffffff) }
        btn3d.bringToFront()
    }

    /** Nut 3D tren man: tat <-> kieu bat gan nhat (toan man hoac thay ban do). */
    private fun toggle3d() {
        if (prefs.scene3d != 0) { prefs.scene3d = 0; toast("Tắt cảnh 3D") }
        else { prefs.scene3d = prefs.scene3dLast.coerceIn(1, 2); toast(if (prefs.scene3d == 1) "Cảnh 3D toàn màn · camera ở góc" else "Cảnh 3D thay bản đồ") }
        sizePips(root.width)
    }

    private fun showPips() {
        if (prefs.scene3d == 1 && !overlay.calib) { applyScene(root.width); return }
        // dang chinh tay diem tu trong che do 3D toan man: tra camera ve toan man de keo
        if (prefs.scene3d == 1 && overlay.calib) { hideScene(); if (!prefs.splitScreen && overlay.layoutParams.width != -1) { sizePips(root.width); return } }
        if (prefs.splitScreen) { applySplit(root.width); return }
        val c = overlay.calib || prefs.hidePip      // hidePip = tat nhanh het cua so noi
        temp.visibility = if (prefs.showTemp && !c) View.VISIBLE else View.GONE
        map.visibility = if (prefs.showMap && !c) View.VISIBLE else View.GONE
        // canh 3D thay ban do (khong chia doi): nam dung o cua so ban do
        if (prefs.scene3d == 2 && !overlay.calib && map.width > 1) { map.visibility = View.GONE; showScene(map.x, map.y, map.width, map.height) }
        else if (prefs.scene3d != 1) hideScene()
    }

    override fun onStart() {
        super.onStart()
        started = true
        svc?.uiVisible = true
        if (scene?.visibility == View.VISIBLE) scene?.resume()
    }

    override fun onStop() {
        started = false
        // ra nen: bo ve camera, AI van chay trong dich vu
        svc?.uiVisible = false
        overlay.frame = null
        scene?.pause()
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

    private fun menu(title: String, items: List<Pair<String, () -> Unit>>) {
        AlertDialog.Builder(this).setTitle(title).setItems(items.map { it.first }.toTypedArray()) { _, k -> items[k].second() }.show()
    }

    private var setView: SettingsView? = null

    /** Man cai dat kinh mo 2 cot (19/11): danh muc trai, noi dung phai. */
    private fun settings() {
        if (setView != null) return
        val v = SettingsView(this, settingCats()) { closeSettings() }
        setView = v
        root.addView(v, FrameLayout.LayoutParams(-1, -1))
        if (Build.VERSION.SDK_INT >= 31) {
            val blur = android.graphics.RenderEffect.createBlurEffect(28f, 28f, android.graphics.Shader.TileMode.CLAMP)
            overlay.setRenderEffect(blur); temp.setRenderEffect(blur); map.setRenderEffect(blur)
        }
    }

    private fun closeSettings() {
        val v = setView ?: return
        root.removeView(v); setView = null
        if (Build.VERSION.SDK_INT >= 31) { overlay.setRenderEffect(null); temp.setRenderEffect(null); map.setRenderEffect(null) }
    }

    private fun refreshSettings() = setView?.refresh()

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (setView != null) closeSettings() else @Suppress("DEPRECATION") super.onBackPressed()
    }

    private fun onOff(b: Boolean) = if (b) "bật" else "tắt"

    private fun settingCats(): List<SettingsView.Cat> {
        return listOf(
            SettingsView.Cat("canh", "Cảnh báo", { if (prefs.beep) "ting · ${if (prefs.signs) "biển" else "tắt biển"}" else "im lặng" }) { listOf(
                T("Tiếng ting cảnh báo", "lệch làn, xe trước quá gần, vượt tốc độ", { prefs.beep }, { b -> prefs.beep = b; if (b) svc?.beeper?.test() }, false),
                S("Âm lượng", null, listOf("Nhỏ", "Vừa", "To"), { prefs.vol }, { k -> prefs.vol = k; svc?.beeper?.setVolume(k); svc?.beeper?.test() }, true),
                T("Cảnh báo lệch làn", "trên 50 km/h · tắt khi đi phố đông", { prefs.ldwOn }, { b -> prefs.ldwOn = b }, true),
                S("Độ nhạy xe trước", "sớm = báo sớm hơn, dễ kêu hơn", listOf("Sớm", "Vừa", "Muộn"), { prefs.fcwSens }, { k -> prefs.fcwSens = k }, true),
                H("Biển & tốc độ"),
                T("Đọc biển giao thông", "camera đọc biển khi xe chạy", { prefs.signs }, { b -> prefs.signs = b }, false),
                S("Kêu khi vượt tốc độ", "chỉ theo biển thật / Waze / GOFA", listOf("Tắt", "+0", "+5", "+10"),
                    { when (prefs.overKmh) { -1 -> 0; 0 -> 1; 10 -> 3; else -> 2 } }, { k -> prefs.overKmh = intArrayOf(-1, 0, 5, 10)[k] }, true),
                T("Biển tốc độ từ Waze / GOFA", "bong bóng tốc độ của app dẫn đường", { prefs.navLimit }, { b -> prefs.navLimit = b }, true),
                T("Tốc độ mặc định theo luật", "50 / 60 khi chưa thấy biển (bản đồ OSM)", { prefs.defLimit }, { b -> prefs.defLimit = b }, false),
                T("Camera phạt nguội", "lấy từ Waze / GOFA", { prefs.camAlert }, { b -> prefs.camAlert = b }, false),
            ) },
            SettingsView.Cat("cam", "Camera", { (when (prefs.camSrc) { "mirror" -> "cam gương"; "70mai" -> "70mai"; else -> "ĐT" }) + " · " + (if (!prefs.autoCalib) "chỉnh tay" else if (prefs.calibrated) "đã hiệu chỉnh" else "đang học") }) { listOfNotNull(
                S("Nguồn camera", null, listOf("Điện thoại", "Cam gương", "70mai"), { when (prefs.camSrc) { "mirror" -> 1; "70mai" -> 2; else -> 0 } }, { k ->
                    val m = when (k) { 1 -> "mirror"; 2 -> "70mai"; else -> "phone" }
                    if (m != prefs.camSrc) { prefs.camSrc = m; svc?.switchCam(); toast(when (k) { 1 -> "Dùng cam gương: điện thoại phải đang nối Wi-Fi của cam"; 2 -> "Dùng cam 70mai: nối Wi-Fi 70mai_… của cam"; else -> "Dùng camera điện thoại" }) }
                }, false),
                // Cac muc sau chi cho cam Wi-Fi (cam guong / 70mai)
                if (prefs.camSrc != "phone") H("Camera Wi-Fi — hiệu năng & hình ảnh") else null,
                if (prefs.camSrc != "phone") S("Độ phân giải", "720p mát, mượt, đỡ trễ · AI tự thu nhỏ ảnh nên 720p không giảm độ chính xác", listOf("720p", "1080p"), { if (prefs.camHd) 1 else 0 }, { k -> val hd = k == 1; if (hd != prefs.camHd) { prefs.camHd = hd; svc?.restartCam(); toast(if (hd) "1080p: nét hơn, nóng & trễ hơn" else "720p: mát, mượt, đỡ trễ") } }, true) else null,
                if (prefs.camSrc != "phone") T("Làm nét vật cản", "tăng nét viền xe/vật phía trước, giảm mờ xung quanh", { prefs.camSharpen }, { b -> prefs.camSharpen = b }, true) else null,
                if (prefs.camSrc != "phone") T("Cân màu tự động", "chống ám/bệt màu, màu trung thực hơn", { prefs.camColor }, { b -> prefs.camColor = b }, true) else null,
                H("Hiệu chỉnh"),
                T("Tự hiệu chỉnh", svc?.calibInfo()?.lineSequence()?.firstOrNull() ?: "bật lại = học lại từ đầu", { prefs.autoCalib }, { b -> svc?.setAuto(b); if (b) toast("Học lại từ đầu: chạy thẳng 1–2 phút trên đường có vạch để app tự học"); refreshSettings() }, false),
                A("Chỉnh tay (kéo điểm tụ)", null, null) { closeSettings(); startCalib() },
                A("Chiều cao camera", "từ camera xuống mặt đường", "%.1f m".format(prefs.camH)) {
                    val hs = arrayOf(0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.7f, 1.9f)
                    AlertDialog.Builder(this).setTitle("Camera cách mặt đường")
                        .setItems(hs.map { "%.1f m".format(it) }.toTypedArray()) { _, k -> prefs.camH = hs[k]; svc?.reloadSettings(false); refreshSettings() }.show()
                },
            ) },
            SettingsView.Cat("hien", "Hiển thị", { if (prefs.hidePip) "ẩn PiP" else listOfNotNull(if (prefs.showMap) "bản đồ" else null, if (prefs.showTemp) "nhiệt" else null).joinToString(" · ").ifEmpty { "gọn" } }) { listOf(
                T("Chia đôi màn (camera | bản đồ)", "trái camera + ADAS, phải bản đồ · bật nhanh bằng nút ◫ trên màn", { prefs.splitScreen }, { b -> prefs.splitScreen = b; overlay.split = b; sizePips(root.width) }, true),
                S("Cảnh lái 3D (kiểu Tesla)", "vẽ lại lane, đường đi, xe xung quanh từ AI · bật nhanh bằng nút 3D trên màn", listOf("Tắt", "Toàn màn", "Thay bản đồ"), { prefs.scene3d.coerceIn(0, 2) }, { k -> prefs.scene3d = k; if (k != 0) prefs.scene3dLast = k; sizePips(root.width) }, true),
                S("Góc nhìn cảnh 3D", null, listOf("Sau xe", "Trên cao"), { if (prefs.scene3dHigh) 1 else 0 }, { k -> prefs.scene3dHigh = k == 1; scene?.view = if (k == 1) "high" else "chase" }, true),
                T("Ẩn cửa sổ nổi (PiP)", "ẩn nhanh bản đồ + ô nhiệt để xem camera gọn", { prefs.hidePip }, { b -> prefs.hidePip = b; showPips() }, false),
                T("Bản đồ góc", "kéo để đổi góc", { prefs.showMap }, { b -> prefs.showMap = b; showPips() }, false),
                S("Nguồn bản đồ", null, listOf("Google tối", "OpenStreetMap"), { if (prefs.mapSrc == "osm") 1 else 0 }, { k -> prefs.mapSrc = if (k == 1) "osm" else "google"; map.source = prefs.mapSrc }, false),
                T("Bản đồ Google tự sáng/tối (ngày/đêm)", "ngày hiện bản đồ sáng, đêm đổi sang nền tối theo giờ mặt trời · chỉ với nguồn Google", { prefs.mapAuto }, { b -> prefs.mapAuto = b; map.autoDayNight = b }, true),
                S("Cỡ số khoảng cách + biển", "biển tốc độ, biển báo, camera", listOf("Nhỏ", "Vừa", "To", "Rất to"), { prefs.signSize }, { k -> prefs.signSize = k; overlay.signScale = SIGN_SCALE[k]; overlay.invalidate() }, true),
                T("Ô nhiệt độ máy", null, { prefs.showTemp }, { b -> prefs.showTemp = b; showPips() }, false),
                T("Vạch làn kéo xuống đáy màn", "tắt = cắt ở mép taplo", { prefs.overHood }, { b -> prefs.overHood = b; overlay.overHood = b; overlay.invalidate() }, false),
            ) },
            SettingsView.Cat("hud", "HUD & kết nối", { if (svc?.hudConnected() == true) "đã nối" else "chưa nối" }) { listOf(
                A("Trạng thái HUD", svc?.hudStatus() ?: "dịch vụ chưa chạy", null) { },
                A("Tìm lại HUD", "quên HUD cũ, dò lại", null) { svc?.findHudAgain(); toast("Đang tìm HUD…"); refreshSettings() },
                T("Gửi biển tốc độ lên HUD", "biển camera đọc được (cần firmware 1.0.57)", { prefs.hudSign }, { b -> prefs.hudSign = b }, true),
                H("Waze Mod"),
                A("Nối thẳng Waze Mod", svc?.wazeStatus() ?: "", null) { wazeAccess() },
            ) },
            SettingsView.Cat("dan", "Dẫn đường", { if (Nav.active) "đang dẫn" else if (prefs.hereKey.isEmpty()) "OSRM" else "HERE" }) { listOfNotNull(
                if (Nav.active) A("Kết thúc dẫn đường", Nav.dest?.name, null) { Nav.stop(); refreshSettings() } else null,
                A("Cách dùng", "mở trạm từ app EVCarHUD Charge, hoặc chia sẻ link HERE WeGo / Google Maps vào app này", null) { },
                A("Khoá HERE (tính đường)", if (prefs.hereKey.isEmpty()) "chưa có, đang dùng OSRM" else "đã lưu …" + prefs.hereKey.takeLast(4), null) { hereKeyDialog() },
            ) },
            SettingsView.Cat("nang", "Hiệu năng & nhiệt", { arrayOf("tiết kiệm", "cân bằng", "tối đa")[prefs.perf.coerceIn(0, 2)] }) { listOf(
                S("Chế độ", "tiết kiệm: AI 3 lần/giây, đọc biển thưa hơn · tối đa: chỉ giảm khi pin ≥ 43°", listOf("Tiết kiệm", "Cân bằng", "Tối đa"), { prefs.perf }, { k -> prefs.perf = k }, true),
                S("Bộ xử lý AI", "Tự động: ưu tiên NPU, máy tự đối chiếu CPU, NPU sai thì tự về CPU", listOf("Tự động (NPU)", "Ép CPU"), { if (prefs.accel == "npu") 0 else 1 }, { k -> val a = if (k == 0) "npu" else "cpu"; if (a != prefs.accel) { prefs.accel = a; svc?.reloadSettings(true) } }, false),
                A("Nhiệt độ hiện tại", svc?.heatInfo() ?: "", null) { },
            ) },
            SettingsView.Cat("kt", "Kỹ thuật", { "chẩn đoán" }) { listOf(
                A("Số liệu AI + ghi 1 khung", null, null) { diag() },
                A("Xem ảnh mô hình AI đang thấy", null, null) { showModelView() },
                T("Hiện dòng test/log trên màn", "dòng trạng thái AI + dòng \"Cam: FFMPEG…\" + số liệu hiệu chỉnh · tắt thì ẩn hết, vẫn giữ cảnh báo thật", { prefs.showDbg }, { b -> prefs.showDbg = b }, false),
            ) },
            SettingsView.Cat("tt", "Thông tin", { appVer() }) { listOf(
                A("Phiên bản app", null, appVer()) { },
                A("Mã HUD", null, prefs.hudMac.ifEmpty { "chưa có" }) { },
                A("Giới thiệu & giấy phép", null, null) { AlertDialog.Builder(this).setTitle("EVCarHUD LANE").setMessage(ABOUT).setPositiveButton("OK", null).show() },
                A("Thoát hẳn (tắt chạy nền)", null, null) {
                    startService(Intent(this, AdasService::class.java).setAction(AdasService.ACTION_STOP))
                    finish()
                },
            ) },
        )
    }

    private fun appVer() = try { packageManager.getPackageInfo(packageName, 0).versionName ?: "-" } catch (_: Exception) { "-" }

    /** Xin quyen doc du lieu Waze Mod (bang Cho phep / Tu choi cua Waze Mod). */
    private fun wazeAccess() {
        try { startActivityForResult(Intent(WazeModLink.ACTION_REQUEST).setPackage(WazeModLink.PKG), 7) }
        catch (e: Exception) { svc?.logLine("WAZEMOD xin quyen loi ${e.javaClass.simpleName} ${e.message}"); toast(svc?.wazeStatus() ?: "Không mở được Waze Mod (${e.javaClass.simpleName})") }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(req: Int, res: Int, data: Intent?) {
        @Suppress("DEPRECATION") super.onActivityResult(req, res, data)
        if (req == 7) { svc?.logLine("WAZEMOD xin quyen ket qua $res"); toast(if (res == RESULT_OK) "Đã nối Waze Mod" else "Waze Mod chưa cho phép (bị đóng ngay hoặc bấm Từ chối)"); svc?.wazeRetry(); refreshSettings() }
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
            Probe.run(this) { line -> svc?.logLine("DO CAM $line"); runOnUiThread { sb.append(line).append('\n'); tv.text = sb.toString() } }
        }.start()
    }

    private fun mapName(k: String) = when (k) { "osm" -> "OpenStreetMap"; else -> "Google (thử nghiệm, nền tối)" }

    companion object {
        private const val ABOUT = "Nhận diện làn đường, mép đường, xe phía trước và đường đi dùng mô hình openpilot " +
            "(driving_vision, driving_policy v0.10.3) của comma.ai, giấy phép MIT.\n\n" +
            "Đọc biển giao thông: mô hình YOLO11n 56 loại biển Việt Nam (github.com/Namvipcf/vietnamese-traffic-sign-yolo11, giấy phép MIT).\nHình biển mẫu: gói @road-signs/vn (github.com/karlnorling/road-signs, giấy phép MIT).\nNhận loại xe: YOLOX-Nano (Megvii, giấy phép Apache-2.0). Cảnh 3D: Three.js (giấy phép MIT). Font Be Vietnam Pro (SIL OFL 1.1).\n\n" +
            "Chỉ hỗ trợ cảnh báo, không thay người lái. Bản đồ © OpenStreetMap contributors.\n\n" +
            "Chống nóng máy: AI chạy 5–20 lần/giây theo tốc độ, dừng khi xe đỗ hoặc máy quá nóng. " +
            "Nên gắn điện thoại chỗ cửa gió điều hoà, sạc 18–20 W."
    }
}
