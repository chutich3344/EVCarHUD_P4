package com.evcarhud.adas

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.graphics.Bitmap
import android.os.BatteryManager
import android.os.Binder
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import kotlin.math.abs
import kotlin.math.atan
import kotlin.math.atan2
import kotlin.math.roundToInt
import kotlin.math.sqrt

/** 1 lan cap nhat gui cho giao dien. */
class Snap(
    val out: Op.Out?,
    val cam: Op.Cam?,
    val camH: Float,
    val vKmh: Float,
    val ldw: Char,
    val offCm: Int?,
    val leadM: Float?,
    val ttc: Float?,
    val level: Int,
    val hz: Int,
    val ms: Int,
    val backend: String,
    val batt: Float,
    val thermal: Int,
    val state: Int,
    val msg: String,
    val hud: String,
    val hudOk: Boolean,
    val lat: Double,
    val lon: Double,
    val bearing: Float,
    val hasFix: Boolean,
) {
    /** Mep tren taplo (ti le chieu cao anh, 1 = khong thay taplo): ve lan/duong di khong de len taplo. */
    var hood = 1f
    /** So lieu chan doan hien tren man (bat trong Chan doan AI). */
    var dbg: String? = null
    /** 1 dong thong so ngan luon hien (AI, xe, vach, hieu chinh, log, camera). */
    var stat = ""
    /** Bien toc do dang ap dung (camera doc), bien vua qua (anh that + bien phu), dang vuot toc do. */
    var limit: Int? = null
    var objs: List<SignDet.Obj>? = null   // xe/nguoi YOLOX (de ghi loai xe truoc)
    var wide: List<SignDet.Obj>? = null   // xe/nguoi TOAN KHUNG (VehDet) khi man canh 3D dang bat
    var wideSeq = 0L   // moc lan nhan (de canh 3D chi cap nhat vet khi co lan nhan MOI, khong lap lai list cu 20 lan/giay)
    var wideAge = 0L   // tuoi lan nhan do (ms)
    var cal = true     // camera da hieu chinh xong VA thi giac tin duoc (khong: vach openpilot chao -> canh 3D lam muot manh, bo khung vo ly)
    var lm: IntArray? = null   // kieu vach 4 vach openpilot (VehDet.marks): 0 khong ro, 1 trang dut, 2 trang lien, 3 vang dut, 4 vang lien, 5 vang doi
    var camType: Char? = null   // camera tu HUD (Waze/GOFA): S/R/A/G
    var camM: Int? = null
    var limitSrc: String? = null   // bien lay tu app dan duong: "Waze" / "GOFA"
    var limitWhy: String? = null   // null = bien that; khac = toc do mac dinh (theo luat / ban do)
    var sign: SignDet.Hit? = null
    var over = false
    /** Den giao thong (R/Y/G, null = khong thay), so giay con lai (dem lui), hop den/bang dem lan quet gan nhat (moi < 0,8 s). */
    var tl: Char? = null
    var tlSec: Int? = null
    var tlLamps: List<TLight.Lamp>? = null
}

/**
 * Dich vu chay nen: camera -> mo hinh openpilot -> canh bao -> goi A1 len HUD.
 * Chong nong (uu tien so 1):
 *  - tan suat AI theo tinh huong: 5 Hz khi cham (<5 km/h), 10 Hz binh thuong, 20 Hz khi >80 km/h hoac xe truoc gan;
 *    dung han khi xe do >30 giay (dong camera neu khong mo man hinh app)
 *  - theo doi nhiet: pin >=42 do / Android bao "am" -> toi da 10 Hz; >=45 / "nong" -> 5 Hz;
 *    >=48 / "rat nong" -> dung AI, bao MAY NONG len HUD, chay lai khi <44 do
 *  - chay nen khong ve camera; camera 720p, khong ghi hinh
 */
class AdasService : Service() {
    inner class LocalBinder : Binder() { val service get() = this@AdasService }

    private val main = Handler(Looper.getMainLooper())
    private val aiThread = HandlerThread("ai").apply { start() }
    private val ai = Handler(aiThread.looper)
    private lateinit var prefs: Prefs
    private var cam: FrameSource? = null
    @Volatile private var model: OpModel? = null
    private var hud: HudLink? = null
    private var hudMsg = "Chưa nối HUD"
    private var hudOk = false

    var onSnap: ((Snap) -> Unit)? = null
    /** Khung xem truoc tu ve (640x360) tu chinh luong anh AI: nhieu may khong cho mo 2 luong camera cung luc. */
    var onPreview: ((Bitmap) -> Unit)? = null
    private val prevBmp = Array(2) { Bitmap.createBitmap(PW, PH, Bitmap.Config.ARGB_8888) }
    private val prevPx = IntArray(PW * PH)
    private val sharpPx = IntArray(PW * PH)      // bo dem lam net preview (unsharp nhe)
    private var prevI = 0
    private var lastPrev = 0L
    @Volatile var bright = -1
        private set
    private var aiCount = 0
    private var aiCountAt = 0L
    @Volatile private var measHz = 0
    var uiVisible = false
        set(v) { field = v; main.post { applyRate() } }

    // ---- trang thai (luong chinh) ----
    @Volatile private var targetHz = 10
    @Volatile private var busy = false
    private var lastAccept = 0L
    @Volatile private var preBusy = false
    private var preAt = 0L
    private var preY = ByteArray(0); private var preU = ByteArray(0); private var preV = ByteArray(0)
    /** Chan doan (23/11): khoang cach 2 khung dua vao mo hinh lan cuoi (0 = 2 khung giong nhau), so lan cap hong / tong, so lan doi hinh hoc. */
    @Volatile private var pairMs = -1L
    private var pairBad = 0; private var pairAll = 0; private var geomN = 0
    private var keepRingOnce = false
    private var gpsKmh = -1f
    private var gpsAt = 0L
    private var lat = 0.0
    private var lon = 0.0
    private var bearing = 0f
    private var stillSince = 0L
    private var parked = false
    private var batt = 0f
    private var thermal = 0
    private var hot = 0            // 0 binh thuong, 1 toi da 6 Hz, 2 toi da 4 Hz, 3 dung
    private var loadMsg = "Đang nạp mô hình…"
    private var lastSend = 0L
    private var lastNotif = 0L

    // ---- luong AI ----
    private var geom: Op.Cam? = null
    private var warpKey = ""
    private var warpMed: Op.Warp? = null
    private var warpBig: Op.Warp? = null
    private var yArr = ByteArray(0)
    private var uArr = ByteArray(0)
    private var vArr = ByteArray(0)
    private val ringN = 8
    private val ringMed = Array(ringN) { ByteArray(Op.PACK) }
    private val ringBig = Array(ringN) { ByteArray(Op.PACK) }
    private val ringT = LongArray(ringN)
    private var ringI = 0
    private var ringCount = 0
    private val img = ByteArray(2 * Op.PACK)
    private val big = ByteArray(2 * Op.PACK)
    private val hidT = LongArray(160)
    private val hid = Array(160) { FloatArray(Op.HID_N) }
    private var hidI = 0
    private var hidCount = 0
    private val feat = FloatArray(Op.FEAT_N * Op.HID_N)
    private var poseKmh = 0f
    private var ldwHoldUntil = 0L
    // ---- doc bien giao thong ----
    private var signDet: SignDet? = null
    // ---- den giao thong + dem giay (TLight.kt) ----
    private var tlight: TLight? = null
    private val tlTrack = TLight.Track()
    private var tlResAt = 0L
    private var tlLogAt = 0L
    private var signDb: SignDb? = null
    private var road: RoadInfo? = null
    private var objs: List<SignDet.Obj>? = null
    private var camDb: CamDb? = null
    private var camType: Char? = null
    private var camDist = -1
    private var camAt = 0L
    private var camLat = 0.0
    private var camLon = 0.0
    private var objsAt = 0L
    // man canh 3D (Scene3D): nhan xe toan khung chi khi man 3D dang hien
    private var vehDet: VehDet? = null
    @Volatile var scene3d = false
    private var wide: List<SignDet.Obj>? = null
    private var wideAt = 0L
    private var laneMk: IntArray? = null; private var laneMkAt = 0L
    private var limit: Int? = null
    private var limitAt = 0L
    private val votes = ArrayList<Triple<Long, Int, Float>>()
    private var waze: WazeModLink? = null
    private var wazeTry = 0L
    private var navLim: Int? = null     // bien toc do Waze/GOFA tu HUD
    private var navLimAt = 0L
    private var sigSent = -1; private var sigAt = 0L
    @Volatile private var spdAt = 0L   // luc vua thay bien toc do -> doc day hon 2,5 s de du phieu chot so
    private var lastSign: SignDet.Hit? = null
    private var lastSignAt = 0L
    private var overBeepAt = 0L
    val beeper = Beeper()
    private var lastLvLog = 0
    private var vrelF = Float.NaN
    private var vrelT = 0L
    private var l3Since = 0L
    private var l3Hold = 0L
    private var l2n = 0
    // Do tin thi giac: sai lech |toc do model (poseVx) - GPS|. Cam goc rong meo (70mai) lam model hong
    // (poseVx ~0 du chay 50 km/h) -> van toc/cu ly xe truoc rac -> chan canh bao phanh oan.
    private var poseErrF = Float.NaN
    private var poseErrT = 0L
    // EMA nhanh/cham cua cu ly xe truoc -> do "xe truoc co THAT SU lai gan khong" (chong phanh ao).
    private var leadXfast = Float.NaN
    private var leadXslow = Float.NaN
    private var ldwSide = '-'
    @Volatile private var lastMs = 0

    // ---- tu hieu chinh ----
    // goc cui (pitch) + nghieng (roll): cam bien trong luc, loc cham ~10 s (bo qua luc tang/giam toc manh)
    // goc lech trai/phai (yaw): huong chuyen dong mo hinh thay (pose) khi chay thang > 20 km/h, giong calibrationd openpilot
    private var calPitch = 0.0
    private var calYaw = 0.0
    private var calRoll = 0.0
    private val CAL70_PITCH = -0.22   // 70mai chuc xuong (diem tu duoi tam anh)
    private var gP = Double.NaN
    private var gR = 0.0
    private var geomAt = 0L
    private val yawObs = FloatArray(50)
    private var yawN = 0
    private var yawGood = 0
    @Volatile private var lastFrameAt = 0L
    // Khi cam rot/noi lai (gap khung), khung dau sau do hay nhay -> chan canh bao PHANH/xe truoc gia
    // trong 1,5 s cho on dinh, tranh "loi phanh" khi lai xe.
    @Volatile private var camSettleUntil = 0L
    private var camOpenAt = 0L

    override fun onBind(i: Intent?): IBinder = LocalBinder()

    override fun onCreate() {
        super.onCreate()
        prefs = Prefs(this)
        goForeground("Đang khởi động…")
        makeCam()
        calPitch = prefs.pitch.toDouble(); calYaw = prefs.yaw.toDouble(); calRoll = prefs.roll.toDouble()
        // 70mai gan tren kinh, chuc xuong manh: diem tu nam DUOI tam anh -> pitch ~ -0.22 rad (do bang diem tu
        // 1426,1087 @2880, f=1262). 70mai KHONG tu hieu chinh pitch (chi cam DT) nen phai co mac dinh dung.
        // Neu pitch da luu ~0 (chua hieu chinh) thi nap mac dinh; giu lai neu nguoi dung da keo diem tu that.
        if (prefs.camSrc == "70mai" && kotlin.math.abs(calPitch) < 0.02) { calPitch = CAL70_PITCH; calYaw = 0.0 }
        startGravity()
        rebuildGeom()
        ai.post {
            try {
                model = OpModel(this, prefs.accel)
                loadMsg = ""
            } catch (e: Throwable) {
                Log.e(TAG, "model", e); loadMsg = "Lỗi mô hình: ${e.message}"
            }
            main.post { applyRate() }
        }
        startGps()
        clog = CloudLog(this, try { packageManager.getPackageInfo(packageName, 0).versionName ?: "?" } catch (_: Exception) { "?" }).also {
            it.hudMac = prefs.hudMac
            it.line("bat dau · ${android.os.Build.MODEL} · AI ${prefs.accel} · tu hieu chinh ${prefs.autoCalib}/${prefs.calibrated} · camH ${prefs.camH}")
        }
        signDb = SignDb(this); camDb = CamDb(this); road = RoadInfo { m -> clog?.line(m) }
        waze = WazeModLink(this, { m -> clog?.line(m) }) { b -> onWaze(b) }.also { it.start() }
        signDet = SignDet(this, { ob -> main.post { objs = ob; objsAt = SystemClock.elapsedRealtime() } }) { hits -> main.post { onSigns(hits) } }
        vehDet = VehDet(this) { ob, lm -> main.post { wide = ob; wideAt = SystemClock.elapsedRealtime(); if (lm != null) { laneMk = lm; laneMkAt = wideAt }
            if (targetHz == 0 && scene3d) publishScene() } }   // 23/11: AI nghi (xe dung lau) van day xe YOLO len canh 3D
        tlight = TLight(this) { r -> main.post { onTLight(r) } }
        hud = HudLink(this) { m, ok -> hudMsg = m; hudOk = ok; publishIdle() }.also {
            it.onMac = { a -> prefs.hudMac = a; clog?.hudMac = a }
            it.onCam = { m -> onCam(m) }
            if (hasPerm(Manifest.permission.BLUETOOTH_CONNECT)) it.start(prefs.hudMac) else hudMsg = "Chưa cho quyền Bluetooth"
        }
        Nav.listeners += navL
        main.post(tick)
    }

    // ---------------- dan duong (Route.kt) -> goi D1 len HUD ----------------
    private var navOffLeft = 0
    private var lastStep = ""
    private val navL: () -> Unit = {
        // doi chi dan (ma re / ten duong) thi gui ngay, con lai gui theo nhip 1 giay o tick
        val k = Nav.d1().split('|').let { if (it.size > 7) it[1] + it[3] + it[7] else it.joinToString() }
        if (k != lastStep) { lastStep = k; pushNav() }
    }

    private fun pushNav() {
        if (Nav.active) { navOffLeft = 3; hud?.sendNav(Nav.d1().toByteArray()); return }
        if (navOffLeft > 0) { navOffLeft--; hud?.sendNav("D1|0".toByteArray()) }   // ket thuc: bao HUD vai lan cho chac
    }

    fun startNav(d: Dest) = Nav.start(this, d, prefs.hereKey, prefs.routeMode)

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) { stopSelf(); return START_NOT_STICKY }
        return START_STICKY
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        hud?.stop()
        beeper.release()
        cam?.release()
        try { (getSystemService(SENSOR_SERVICE) as SensorManager).unregisterListener(gravL) } catch (_: Exception) {}
        Nav.listeners -= navL
        try { (getSystemService(LOCATION_SERVICE) as LocationManager).removeUpdates(locL) } catch (_: Exception) {}
        ai.post { model?.close(); model = null; aiThread.quitSafely() }
        waze?.stop(); signDet?.release(); vehDet?.release(); tlight?.release(); road?.release(); signDb?.flush(); camDb?.flush()
        clog?.line("dung app"); clog?.close()
        super.onDestroy()
    }

    // ---------------- giao dien goi ----------------
    fun setPreview(s: Surface?) { cam?.setPreview(s) }
    fun camInfo(): FrameSource? = cam
    fun currentGeom(): Op.Cam? = geom

    /** Doi hieu chinh / chieu cao / bo tang toc. */
    private fun makeCam() {
        var me: FrameSource? = null
        // bo bao loi cua nguon cu (vd. cam guong da tat sau khi doi ve camera dien thoai)
        val err: (String) -> Unit = { e -> main.post { if (me == null || cam === me) { loadMsg = e; publishIdle() } } }
        try {
            me = if (prefs.camSrc != "phone") MirrorCam(this, ::onFrame, err, { main.post { rebuildGeom() } }, { clog?.line("cam wifi: $it") }, prefs.camSrc, prefs.camHd)
            else CamSource(this, ::onFrame, err)
            cam = me
        } catch (e: Exception) { loadMsg = "Không có camera: ${e.message}" }
    }

    private var pendingCam: Runnable? = null

    /** Tat DUT cam cu roi moi mo cam moi. Neu cam cu la cam Wi-Fi (MirrorCam) thi cho 2s cho luong+mang cu
     *  dut han (bao "Dang ngat ket noi cu, cho 2s") -> cam moi noi SACH, khong bi 2 cam giành mang. */
    private fun rebuildCamGuarded() {
        val wifiOld = cam is MirrorCam
        cam?.release(); cam = null
        pendingCam?.let { main.removeCallbacks(it) }
        val go = Runnable { pendingCam = null; loadMsg = ""; makeCam(); rebuildGeom(); applyRate() }
        if (wifiOld) {
            loadMsg = "Đang ngắt kết nối cũ, chờ 2s…"; publishIdle()
            pendingCam = go; main.postDelayed(go, 2000)
        } else { loadMsg = ""; go.run() }
    }

    /** Doi nguon camera (dien thoai <-> cam guong Wi-Fi). */
    fun switchCam() {
        // cam guong: goc lap tu 3 clip 17/11 (cui -0,04, lech 0,09); tu hieu chinh yaw chay tiep. Dien thoai: lay lai tu trong luc
        if (prefs.camSrc == "mirror") { calPitch = -0.04; calYaw = 0.09; calRoll = 0.0 } else if (prefs.camSrc == "70mai") { calPitch = CAL70_PITCH; calYaw = 0.0; calRoll = 0.0 } else { calYaw = 0.0; gP = Double.NaN }
        prefs.calibrated = false; yawGood = 0
        saveCalib()
        rebuildCamGuarded()
        clog?.line("nguon camera: ${prefs.camSrc}")
    }

    /** Mo lai camera (vd. doi do phan giai 720p/1080p) KHONG dong lai hieu chinh nhu switchCam. */
    fun restartCam() {
        if (prefs.camSrc == "phone") return
        rebuildCamGuarded()
        clog?.line("mo lai cam: ${prefs.camSrc} ${if (prefs.camHd) "1080p" else "720p"}")
    }

    fun reloadSettings(accelChanged: Boolean) {
        rebuildGeom()
        if (accelChanged) ai.post {
            model?.close(); model = null; loadMsg = "Đang nạp mô hình…"
            try { model = OpModel(this, prefs.accel); loadMsg = "" } catch (e: Throwable) { loadMsg = "Lỗi mô hình: ${e.message}" }
        }
    }

    fun findHudAgain() {
        prefs.hudMac = ""
        if (hasPerm(Manifest.permission.BLUETOOTH_CONNECT)) hud?.start("")
    }

    /** Hieu chinh tay (keo diem tu): tat tu hieu chinh. */
    fun leadHint(x: Float, y: Float, w: Float) { signDet?.leadHint(x, y, w) }

    fun setManualVp(vpX: Float, vpY: Float) {
        val c = cam ?: return
        val w = c.size.width; val h = c.size.height; val f = c.focalPx
        val vx = vpX * w - w / 2.0
        val vy = vpY * h - h / 2.0
        if (c.fisheye) {            // mat ca equidistant: goc = ban kinh pixel / f
            calYaw = vx / f
            calPitch = -vy / f
        } else {
            calYaw = atan(vx / f)
            calPitch = -atan2(vy, sqrt(f * f + vx * vx))
        }
        calRoll = 0.0
        prefs.autoCalib = false
        saveCalib(); rebuildGeom()
    }

    /** Bat/tat tu hieu chinh. BAT lai = HOC LAI TU DAU: ve goc mac dinh cua nguon cam roi hoc lai
     *  (gop nut "Hoc lai tu dau" cu vao day cho gon). */
    fun setAuto(on: Boolean) {
        prefs.autoCalib = on
        if (on) {
            if (prefs.camSrc == "mirror") { calPitch = -0.04; calYaw = 0.09; calRoll = 0.0 } else if (prefs.camSrc == "70mai") { calPitch = CAL70_PITCH; calYaw = 0.0; calRoll = 0.0 } else { calYaw = 0.0; calRoll = 0.0; gP = Double.NaN }
            prefs.calibrated = false; yawGood = 0
            saveCalib(); rebuildGeom()
            clog?.line("bat tu hieu chinh = hoc lai tu dau")
        }
    }

    private fun saveCalib() { prefs.pitch = calPitch.toFloat(); prefs.yaw = calYaw.toFloat(); prefs.roll = calRoll.toFloat() }

    private fun rebuildGeom(keepRing: Boolean = false) {
        val c = cam ?: return
        val g = Op.Cam(c.size.width, c.size.height, c.focalPx, calPitch, calYaw, calRoll, c.fisheye)
        geomAt = SystemClock.elapsedRealtime()
        geomN++
        ai.post { geom = g; warpKey = ""; if (keepRing) keepRingOnce = true else { keepRingOnce = false; ringCount = 0 } }
    }

    /** Thong tin hieu chinh cho Cai dat (do). */
    fun calibInfo(): String {
        val c = cam ?: return "Chưa có camera"
        val hf = if (c.fisheye) Math.toDegrees(c.size.width / c.focalPx) else Math.toDegrees(2 * atan(c.size.width / 2 / c.focalPx))
        fun d(r: Double) = "%.1f°".format(Math.toDegrees(r))
        return "Camera ${c.size.width}x${c.size.height}, góc ngang ${"%.0f".format(hf)}° (${c.focalSrc}${(c as? MirrorCam)?.let { " · " + it.status } ?: ""})\nCúi ${d(calPitch)} · lệch ${d(calYaw)} · nghiêng ${d(calRoll)}" +
            (if (prefs.autoCalib) (if (prefs.calibrated) " · tự hiệu chỉnh xong" else " · đang tự hiệu chỉnh") else " · chỉnh tay") +
            "\n" + aiInfo() + "\n${if (targetHz == 0) 0 else measHz} lần/giây · ${gpsKmh.roundToInt()} km/h · mép taplo ${(prefs.hood * 100).roundToInt()}% · pin ${batt.roundToInt()}°"
    }

    // ---- doi chieu NPU voi CPU tren khung that, 8 s/lan (CPU them ~1 lan chay) ----
    private var xcAt = 0L
    private var xcBad = 0
    private var xcN = 0
    @Volatile private var xcInfo = ""

    private fun crossCheck(m: OpModel, o: Op.Out, wall: Long) {
        if (m.backend == "CPU" || wall - xcAt < 8000) return
        xcAt = wall
        val ref = m.crossCheck(img, big) ?: return
        val r = Op.Out(); Op.parseVision(ref, r)
        var lane = 0f
        for (i in 1..2) for (k in 0 until 10) lane = maxOf(lane, abs(r.laneY[i][k] - o.laneY[i][k]))
        val dLead = abs(r.leadProb - o.leadProb)
        val bad = dLead > 0.3f || lane > 0.5f || (r.leadProb > 0.5f) != (o.leadProb > 0.5f)
        xcN++
        xcBad = if (bad) xcBad + 1 else 0
        xcInfo = "đối chiếu CPU #$xcN: xe ${m.backend} ${(o.leadProb * 100).roundToInt()}% / CPU ${(r.leadProb * 100).roundToInt()}%, vạch lệch ${"%.2f".format(lane)} m${if (bad) " SAI" else " khớp"}"
        // 2 lan lien tiep lech: NPU may nay khong tin duoc, dung CPU luon tu khung nay
        if (xcBad >= 2) {
            m.fallbackCpu("NPU lệch CPU trên khung thật (xe ${(o.leadProb * 100).roundToInt()}% vs ${(r.leadProb * 100).roundToInt()}%)")
            prefs.accel = "cpu"
            clog?.line("CHUYEN CPU: ${m.selfTest}"); clog?.flushSoon()
        }
    }

    /** Thong tin bo xu ly AI + ket qua tu kiem NPU. */
    fun aiInfo(): String {
        val m = model ?: return "AI chưa nạp"
        val w = warpMed
        val outPct = if (w != null) 100 * w.y.count { it < 0 } / w.y.size else -1
        val o = lastOut
        return "Log máy chủ: ${clog?.lastStatus ?: "-"}\nAI ${m.backend}, ${lastMs} ms" + (if (m.selfTest.isNotEmpty()) " · ${m.selfTest}" else "") +
            "\nẢnh vào: ${if (outPct >= 0) "$outPct% ngoài khung" else "chưa có"} · cam ${yRowS}/${uvRowS}/${uvPixS}" +
            (if (xcInfo.isNotEmpty()) "\n$xcInfo" else "") +
            (if (o != null) " · khung sống: xe ${(o.leadProb * 100).roundToInt()}% vạch ${(o.laneProb[1] * 100).roundToInt()}/${(o.laneProb[2] * 100).roundToInt()}% pose ${"%.1f".format(o.poseVx)}" else "")
    }
    /** Ghi 1 khung chan doan (anh camera goc + goi mo hinh + ket qua) vao thu muc Download de gui phan tich. */
    fun requestDump(cb: (String) -> Unit) { dumpReq = cb }
    @Volatile private var dumpReq: ((String) -> Unit)? = null

    private fun writeDump(cb: (String) -> Unit, g: Op.Cam, c: FrameSource, dt: Long, vo: FloatArray, o: Op.Out,
                          yRow: Int, uvRow: Int, uvPix: Int) {
        val msg = try {
            val hdr = "{\"w\":${g.w},\"h\":${g.h},\"f\":${g.f},\"pitch\":$calPitch,\"yaw\":$calYaw,\"roll\":$calRoll,\"flip\":${c.flip}," +
                "\"yRow\":$yRow,\"uvRow\":$uvRow,\"uvPix\":$uvPix,\"ny\":${yArr.size},\"nu\":${uArr.size},\"nv\":${vArr.size},\"dt\":$dt," +
                "\"backend\":\"${model?.backend}\",\"self\":\"${model?.selfTest?.replace("\"", "'")}\",\"lp\":${o.leadProb},\"lx\":${o.leadX}," +
                "\"p1\":${o.laneProb[1]},\"p2\":${o.laneProb[2]},\"model\":\"${Build.MODEL}\",\"v\":$gpsKmh}\n"
            val bb = java.nio.ByteBuffer.allocate(vo.size * 4).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            for (x in vo) bb.putFloat(x)
            val name = "evcarhud-chan-doan-${System.currentTimeMillis() / 1000}.bin"
            val cv = android.content.ContentValues().apply {
                put(android.provider.MediaStore.Downloads.DISPLAY_NAME, name)
                put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
            }
            val uri = contentResolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                ?: throw IllegalStateException("không tạo được file")
            contentResolver.openOutputStream(uri)!!.use { os ->
                os.write(hdr.toByteArray()); os.write(img); os.write(big); os.write(bb.array())
                os.write(yArr); os.write(uArr); os.write(vArr)
            }
            "Đã lưu Download/$name\n(xe ${(o.leadProb * 100).roundToInt()}%, vạch ${(o.laneProb[1] * 100).roundToInt()}/${(o.laneProb[2] * 100).roundToInt()}%)\nGửi file này lên Drive cho em."
        } catch (e: Exception) { "Lỗi ghi file: ${e.message}" }
        main.post { cb(msg) }
    }

    @Volatile private var lastOut: Op.Out? = null
    @Volatile private var yRowS = 0
    @Volatile private var uvRowS = 0
    @Volatile private var uvPixS = 0

    /** Ghep kenh Y cua khung moi nhat thanh anh xam 512x256 (goc hep) tren 512x256 (goc rong). */
    fun modelView(): Bitmap? {
        if (ringCount == 0) return null
        val j = (ringI - 1 + ringN) % ringN
        val px = IntArray(512 * 512)
        for ((half, src) in listOf(ringMed[j], ringBig[j]).withIndex()) {
            for (c in 0..3) {
                val dr = if (c == 1 || c == 3) 1 else 0
                val dc = if (c >= 2) 1 else 0
                for (r in 0 until 128) for (q in 0 until 256) {
                    val v = src[(c * 128 + r) * 256 + q].toInt() and 0xff
                    px[(half * 256 + 2 * r + dr) * 512 + 2 * q + dc] = (0xff shl 24) or (v shl 16) or (v shl 8) or v
                }
            }
        }
        // vach ngang phan cach 2 anh
        for (x in 0 until 512) px[256 * 512 + x] = 0xff47e08a.toInt()
        return Bitmap.createBitmap(px, 512, 512, Bitmap.Config.ARGB_8888)
    }

    // trong luc -> goc cui / nghieng cua camera. Man hinh ngang (dinh may ben trai), camera sau:
    // truoc = -z may, phai = -y may, xuong = -x may. Gia toc ke do huong "len" (a).
    private val gravL = object : SensorEventListener {
        override fun onSensorChanged(e: SensorEvent) {
            if (!prefs.autoCalib || prefs.camSrc != "phone") return   // cam guong: trong luc cua dien thoai khong lien quan
            val ax = e.values[0].toDouble(); val ay = e.values[1].toDouble(); val az = e.values[2].toDouble()
            val n = sqrt(ax * ax + ay * ay + az * az)
            if (n < 8.8 || n > 10.8) return                      // dang tang/giam toc, o ga: bo
            val p = atan2(az, ax)                                 // duong = camera cui xuong
            val r = Math.asin((-ay / n).coerceIn(-1.0, 1.0))
            if (gP.isNaN()) { gP = p; gR = r } else { gP += 0.02 * (p - gP); gR += 0.02 * (r - gR) }
            val now = SystemClock.elapsedRealtime()
            if ((abs(gP - calPitch) > 0.01 || abs(gR - calRoll) > 0.01) && now - geomAt > 5000) {
                calPitch = gP; calRoll = gR; saveCalib(); rebuildGeom()
            }
        }
        override fun onAccuracyChanged(s: Sensor?, a: Int) {}
    }

    private fun startGravity() {
        val sm = getSystemService(SENSOR_SERVICE) as SensorManager
        val s = sm.getDefaultSensor(Sensor.TYPE_GRAVITY) ?: sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) ?: return
        sm.registerListener(gravL, s, 200_000)
    }

    /** Luong AI: huong chuyen dong lech -> cong don, 50 mau (~5 s) thi sua yaw theo trung vi. */
    private fun calibYaw(o: Op.Out, v: Float) {
        if (!prefs.autoCalib || v < 20 || o.poseVx < 5f || abs(o.poseRz) > 0.05f) return
        yawObs[yawN++] = kotlin.math.atan2(o.poseVy, o.poseVx)
        // chua hieu chinh xong: 20 mau (~3 s o 7 lan/giay) la sua; xong roi thi 50 mau cho on dinh
        val need = if (prefs.calibrated) yawObs.size else 20
        if (yawN < need) return
        val med = yawObs.copyOf(yawN).sortedArray()[yawN / 2].toDouble()
        yawN = 0
        main.post {
            // ghep nhu calibrationd: R(moi) = R(dang dung) * R(quan sat), chi lay phan yaw
            val r = Op.mul(Op.rotFromEuler(calRoll, calPitch, calYaw), Op.rotFromEuler(0.0, 0.0, (if (prefs.calibrated) 0.7 else 0.85) * med))
            val e = Op.eulerFromRot(r)
            calYaw = e[2].coerceIn(-0.35, 0.35)
            yawGood = if (abs(med) < 0.02) yawGood + 1 else 0
            // pho dong it doan thang dai: lech < 1,1 do 1 lan la du dung (van tiep tuc tinh chinh sau)
            if (yawGood >= 1 && !prefs.calibrated) prefs.calibrated = true
            saveCalib()
            if (abs(med) > 0.002) rebuildGeom(keepRing = true)   // chinh yaw vai phan 10 do: giu cap khung, khong lam vach nhay
        }
    }

    // ---------------- camera -> AI ----------------
    private fun onFrame(im: YuvFrame) {
        val now = SystemClock.elapsedRealtime()
        // chi coi la "rot that su" khi gap >1.5s (giat nhe cam Wi-Fi binh thuong <1.5s van bao duoc);
        // cho on dinh ngan 0.8s -> khong tat canh bao va cham oan khi cam chi giat vat.
        if (lastFrameAt != 0L && now - lastFrameAt > 1500) camSettleUntil = now + 800
        lastFrameAt = now
        val cb = onPreview
        if (uiVisible && cb != null && now - lastPrev >= (if (hot >= 1) 200 else 100)) { lastPrev = now; try { drawPreview(im, cb) } catch (e: Exception) { Log.w(TAG, "prev", e) } }
        else if (now - lastPrev >= 1000) { lastPrev = now; try { bright = meanY(im) } catch (_: Exception) {} }
        if (now - hoodAt >= 300) { hoodAt = now; try { hoodSample(im) } catch (_: Exception) {} }
        // doc bien: 2 lan/giay khi xe chay, 1 lan/giay khi may am, dung khi nong
        if (prefs.signs && hot < 3 && (if (now - gpsAt < 3000 && gpsKmh >= 0) gpsKmh else poseKmh) > 3) signDet?.let { sd ->
            // pin nong: doc bien thua hon, bo nhan loai xe (19/11: pin 43 do khi chay pho)
            sd.objsOn = true   // nhan loai xe gio chi tren vung quanh xe truoc (192x192), du nhe de luon bat
            sd.offer(im, cam?.flip == true, (if (prefs.perf == 0) 2L else 1L) * when (hot) { 0 -> if (now - spdAt < 2500) 200 else 500; 1 -> if (now - spdAt < 2500) 500 else 1000; else -> 2000 })
        }
        // man 3D: nhan xe toan khung 4 lan/giay (may am 2, nong thi dung)
        if (scene3d && uiVisible && hot < 2) vehDet?.let { vd ->
            val g = geom; val p = FloatArray(2)
            val top = if (g != null && g.project(1000f, 0f, 0f, p, 0)) ((p[1] - 0.06f * g.w) / g.h) else 0.3f
            vd.offer(im, cam?.flip == true, top, if (hot == 0) 250L else 500L, g, lastOut)
        }
        // den giao thong: chi khi xe cham/dung (gan giao lo), thua; nong thi thua hon, rat nong thi tat
        if (prefs.tlight && hot < 3) tlight?.let { tl ->
            val v = if (now - gpsAt < 3000 && gpsKmh >= 0) gpsKmh else poseKmh
            if (v < TL_MAX_KMH) tl.offer(im, cam?.flip == true, (if (v < 3) 700L else 1000L) * when (hot) { 0 -> 1; 1 -> 2; else -> 3 } * (if (prefs.perf == 0) 2 else 1))
        }
        val hz = targetHz
        val per = if (hz > 0) 1000L / hz else 0L
        // 23/11 (lane 3D lech/nhay): mo hinh openpilot hoc tren CAP 2 khung cach 0,2 s. Ring chi chua khung da chay AI, nen
        // khi AI <= 3 lan/giay (khung cach >= 333 ms) khong bao gio co khung ~0,2 s -> dua 2 khung GIONG NHAU vao mo hinh:
        // pose ~0 km/h, vach lech 0,3–0,4 m (thu offline Test06). Sua: o muc <= 4 lan/giay, nan truoc 1 khung (KHONG chay mo hinh)
        // khoang 0,2 s truoc lan AI ke tiep.
        if (hz in 1..4 && model != null && !busy && !preBusy && preAt <= lastAccept && now - lastAccept >= per * 0.85 - 200) {
            preBusy = true; preAt = now
            try {
                val p = im.planes
                val yR = p[0].rowStride; val uvR = p[1].rowStride; val uvP = p[1].pixelStride
                preY = copy(p[0].buffer, preY); preU = copy(p[1].buffer, preU); preV = copy(p[2].buffer, preV)
                val ts = im.timestamp / 1_000_000L
                im.close()
                ai.post { try { packRing(ts, yR, uvR, uvP, preY, preU, preV) } catch (e: Throwable) { Log.e(TAG, "pre", e) } finally { preBusy = false } }
            } catch (e: Exception) { im.close(); preBusy = false }
            return
        }
        if (hz <= 0 || busy || model == null || now - lastAccept < per * 0.85 || (hz <= 4 && preAt > lastAccept && (preBusy || now - preAt < 185))) { im.close(); return }
        lastAccept = now
        busy = true
        val yRow: Int; val uvRow: Int; val uvPix: Int
        try {
            val p = im.planes
            yRow = p[0].rowStride; uvRow = p[1].rowStride; uvPix = p[1].pixelStride
            yArr = copy(p[0].buffer, yArr); uArr = copy(p[1].buffer, uArr); vArr = copy(p[2].buffer, vArr)
        } catch (e: Exception) { im.close(); busy = false; return }
        val ts = im.timestamp / 1_000_000L
        im.close()
        ai.post { try { infer(ts, now, yRow, uvRow, uvPix) } catch (e: Throwable) { Log.e(TAG, "infer", e) } finally { busy = false } }
    }

    /** YUV -> anh mau 640x360 (lay 1/2 diem moi chieu), tinh luon do sang trung binh. */
    private fun drawPreview(im: YuvFrame, cb: (Bitmap) -> Unit) {
        val p = im.planes
        val yb = p[0].buffer; val ub = p[1].buffer; val vb = p[2].buffer
        val yRow = p[0].rowStride; val yPix = p[0].pixelStride
        val uvRow = p[1].rowStride; val uvPix = p[1].pixelStride
        val w = im.width; val h = im.height
        val flip = cam?.flip == true
        val sx = w / PW.toFloat(); val sy = h / PH.toFloat()
        val ny = yb.limit(); val nu = minOf(ub.limit(), vb.limit())
        // Cam Wi-Fi hay bi nhat mau / toi: can mau tu dong gian dai Y (limited 16-235 -> full 0-255)
        // + tang bao hoa mau. Chi ap cho cam Wi-Fi va chi anh HIEN (preview), KHONG dung cho AI.
        val wifi = prefs.camSrc != "phone"
        val colOn = wifi && prefs.camColor
        val sat = if (colOn) 1.30f else 1f
        var sum = 0L
        for (y in 0 until PH) {
            val iy0 = (y * sy).toInt().coerceAtMost(h - 1)
            val iy = if (flip) h - 1 - iy0 else iy0
            val yo = iy * yRow; val uo = (iy / 2) * uvRow
            val row = y * PW
            for (x in 0 until PW) {
                val ix0 = (x * sx).toInt().coerceAtMost(w - 1)
                val ix = if (flip) w - 1 - ix0 else ix0
                val yi = yo + ix * yPix
                val Y = if (yi < ny) yb.get(yi).toInt() and 0xff else 0
                val ui = uo + (ix / 2) * uvPix
                val U = (if (ui < nu) ub.get(ui).toInt() and 0xff else 128) - 128
                val V = (if (ui < nu) vb.get(ui).toInt() and 0xff else 128) - 128
                sum += Y
                val Yf = if (colOn) (((Y - 16) * 1.164f).coerceIn(0f, 255f)) else Y.toFloat()
                val Uf = U * sat; val Vf = V * sat
                val r = (Yf + 1.402f * Vf).toInt().coerceIn(0, 255)
                val g = (Yf - 0.344f * Uf - 0.714f * Vf).toInt().coerceIn(0, 255)
                val b = (Yf + 1.772f * Uf).toInt().coerceIn(0, 255)
                prevPx[row + x] = (0xff shl 24) or (r shl 16) or (g shl 8) or b
            }
        }
        bright = (sum / (PW * PH)).toInt()
        // Lam net: unsharp nhe (net vien xe/vat phia truoc, bot mo xung quanh). Chi cam Wi-Fi, chi preview.
        val out = if (wifi && prefs.camSharpen) sharpen(prevPx) else prevPx
        val bm = prevBmp[prevI]; prevI = 1 - prevI
        bm.setPixels(out, 0, PW, 0, 0, PW, PH)
        main.post { cb(bm) }
    }

    /** Unsharp nhe tren preview PWxPH: moi diem = tam*(1+4a) - a*(tren+duoi+trai+phai), tung kenh.
     *  Preview nho (640x360) + chay <=10 lan/giay nen nhe may. Ket qua vao sharpPx. */
    private fun sharpen(src: IntArray): IntArray {
        val a = 0.35f
        for (y in 0 until PH) {
            val row = y * PW
            for (x in 0 until PW) {
                val i = row + x
                if (x == 0 || y == 0 || x == PW - 1 || y == PH - 1) { sharpPx[i] = src[i]; continue }
                val c = src[i]; val up = src[i - PW]; val dn = src[i + PW]; val lf = src[i - 1]; val rt = src[i + 1]
                val cr = (c shr 16) and 0xff; val cg = (c shr 8) and 0xff; val cb = c and 0xff
                val sr = ((cr * (1 + 4 * a)) - a * (((up shr 16) and 0xff) + ((dn shr 16) and 0xff) + ((lf shr 16) and 0xff) + ((rt shr 16) and 0xff))).toInt().coerceIn(0, 255)
                val sg = ((cg * (1 + 4 * a)) - a * (((up shr 8) and 0xff) + ((dn shr 8) and 0xff) + ((lf shr 8) and 0xff) + ((rt shr 8) and 0xff))).toInt().coerceIn(0, 255)
                val sb = ((cb * (1 + 4 * a)) - a * ((up and 0xff) + (dn and 0xff) + (lf and 0xff) + (rt and 0xff))).toInt().coerceIn(0, 255)
                sharpPx[i] = (0xff shl 24) or (sr shl 16) or (sg shl 8) or sb
            }
        }
        return sharpPx
    }

    // ---- tu tim mep taplo: hang anh dung yen khi xe chay (taplo) vs hang thay doi (duong) ----
    private var hoodAt = 0L
    private val hoodPrev = IntArray(HOOD_R * HOOD_C)
    private val hoodAct = FloatArray(HOOD_R)
    private var hoodN = 0

    private fun hoodSample(im: YuvFrame) {
        val v = if (SystemClock.elapsedRealtime() - gpsAt < 3000) gpsKmh else poseKmh
        if (v < 15f) return                                   // dung yen: ca duong cung khong doi, bo
        val p = im.planes[0]; val b = p.buffer; val row = p.rowStride; val n = b.limit()
        val w = im.width; val h = im.height; val flip = cam?.flip == true
        for (r in 0 until HOOD_R) {
            val y0 = ((r + 0.5f) * h / HOOD_R).toInt()
            val y = if (flip) h - 1 - y0 else y0
            var d = 0
            for (c in 0 until HOOD_C) {
                val x0 = ((c + 0.5f) * w / HOOD_C).toInt()
                val x = if (flip) w - 1 - x0 else x0
                val i = y * row + x
                val px = if (i < n) b.get(i).toInt() and 0xff else 0
                val k = r * HOOD_C + c
                d += abs(px - hoodPrev[k]); hoodPrev[k] = px
            }
            val a = d / HOOD_C.toFloat()
            if (hoodN > 0) hoodAct[r] = if (hoodN < 20) hoodAct[r] + (a - hoodAct[r]) / hoodN else hoodAct[r] + 0.03f * (a - hoodAct[r])
        }
        hoodN++
        if (hoodN < 60 || hoodN % 20 != 0) return
        // muc thay doi cua mat duong: trung vi vung 35-60% chieu cao
        val ref = hoodAct.copyOfRange((HOOD_R * 0.35).toInt(), (HOOD_R * 0.6).toInt()).sortedArray().let { it[it.size / 2] }
        if (ref < 2f) return
        var top = HOOD_R
        while (top > HOOD_R / 2 && hoodAct[top - 1] < 0.5f * ref) top--
        // tru 2 hang cho phan chuyen tiep (mep taplo cong, kinh phan chieu); clip 17/11 ra ~0,71
        val frac = if (top >= HOOD_R - 1) 1f else (top - 2) / HOOD_R.toFloat()
        if (abs(frac - prefs.hood) > 0.02f) prefs.hood = frac
    }

    private fun meanY(im: YuvFrame): Int {
        val p = im.planes[0]; val b = p.buffer; val n = b.limit()
        var s = 0L; var c = 0
        var i = 0
        while (i < n) { s += b.get(i).toInt() and 0xff; c++; i += 97 }
        return if (c > 0) (s / c).toInt() else 0
    }

    /** Nan 1 khung vao ring (luong AI). Doi hinh hoc nho (tu hieu chinh yaw) thi GIU ring: xoa ring = 2 khung giong nhau = vach nhay. */
    private fun packRing(ts: Long, yRow: Int, uvRow: Int, uvPix: Int, y: ByteArray, u: ByteArray, v: ByteArray): Int? {
        val g = geom ?: return null
        val c = cam ?: return null
        val key = "$yRow/$uvRow/$uvPix/${g.hashCode()}"
        if (key != warpKey) {
            warpMed = Op.Warp(g, false, yRow, uvRow, uvPix, c.flip)
            warpBig = Op.Warp(g, true, yRow, uvRow, uvPix, c.flip)
            warpKey = key; if (!keepRingOnce) ringCount = 0
            keepRingOnce = false
        }
        val slot = ringI
        warpMed!!.pack(y, u, v, ringMed[slot], 0)
        warpBig!!.pack(y, u, v, ringBig[slot], 0)
        ringT[slot] = ts
        ringI = (ringI + 1) % ringN
        if (ringCount < ringN) ringCount++
        return slot
    }

    private fun copy(b: java.nio.ByteBuffer, old: ByteArray): ByteArray {
        b.rewind()
        val a = if (old.size == b.remaining()) old else ByteArray(b.remaining())
        b.get(a)
        return a
    }

    private fun infer(ts: Long, wall: Long, yRow: Int, uvRow: Int, uvPix: Int) {
        val m = model ?: return
        val g = geom ?: return
        val c = cam ?: return
        val t0 = SystemClock.elapsedRealtime()
        val slot = packRing(ts, yRow, uvRow, uvPix, yArr, uArr, vArr) ?: return
        // khung cu cach ~0,2 s (mo hinh hoc tren 2 khung cach 0,2 s)
        var best = slot; var bestErr = Long.MAX_VALUE
        for (k in 1 until ringCount) {
            val j = (slot - k + ringN) % ringN
            val err = abs((ts - ringT[j]) - 200)
            if (err < bestErr) { bestErr = err; best = j }
        }
        if (bestErr > 120) best = slot
        pairMs = ts - ringT[best]; pairAll++; if (best == slot) pairBad++
        System.arraycopy(ringMed[best], 0, img, 0, Op.PACK); System.arraycopy(ringMed[slot], 0, img, Op.PACK, Op.PACK)
        System.arraycopy(ringBig[best], 0, big, 0, Op.PACK); System.arraycopy(ringBig[slot], 0, big, Op.PACK, Op.PACK)

        val o = Op.Out()
        val vo = m.runVision(img, big)
        Op.parseVision(vo, o)
        lastOut = o; yRowS = yRow; uvRowS = uvRow; uvPixS = uvPix
        dumpReq?.let { cb -> dumpReq = null; writeDump(cb, g, c, ts - ringT[best], vo, o, yRow, uvRow, uvPix) }
        crossCheck(m, o, wall)

        // hang doi hidden_state cho mo hinh duong di: 25 moc cach 0,2 s
        System.arraycopy(o.hidden, 0, hid[hidI], 0, Op.HID_N); hidT[hidI] = ts
        hidI = (hidI + 1) % hid.size
        if (hidCount < hid.size) hidCount++
        if (m.hasPolicy) {
            var span = 0L
            for (k in 0 until Op.FEAT_N) {
                val want = ts - (Op.FEAT_N - 1 - k) * 200L
                var pick = -1
                for (q in 0 until hidCount) {
                    val j = (hidI - 1 - q + hid.size) % hid.size
                    if (hidT[j] <= want + 60) { pick = j; break }
                }
                if (pick >= 0 && ts - hidT[pick] <= (Op.FEAT_N - 1 - k) * 200L + 150) {
                    System.arraycopy(hid[pick], 0, feat, k * Op.HID_N, Op.HID_N)
                    span = maxOf(span, ts - hidT[pick])
                } else java.util.Arrays.fill(feat, k * Op.HID_N, (k + 1) * Op.HID_N, 0f)
            }
            // duong di chi de ve tren man hinh (HUD khong dung): chay nen thi bo cho mat may
            if (span >= 1500 && uiVisible) m.runPolicy(feat)?.let { Op.parsePlan(it, o) }
        }
        lastMs = (SystemClock.elapsedRealtime() - t0).toInt()
        aiCount++
        if (wall - aiCountAt >= 2000) { measHz = (aiCount * 1000 / (wall - aiCountAt)).toInt(); aiCount = 0; aiCountAt = wall }

        // ---- canh bao ----
        poseKmh = 0.8f * poseKmh + 0.2f * o.poseVx * 3.6f
        val gpsFresh = wall - gpsAt < 3000 && gpsKmh >= 0
        val v = if (gpsFresh) gpsKmh else maxOf(0f, poseKmh)
        val vms = v / 3.6f
        // Do tin THI GIAC: toc do model (poseVx) phai bam GPS khi dang chay. Cam goc rong meo (70mai) lam
        // model doan sai ca van toc lan cu ly -> KHONG tin de bao phanh. Cam guong: poseVx ~ GPS -> tin.
        if (gpsFresh && gpsKmh > 12) {
            val e = kotlin.math.abs(o.poseVx - vms)
            poseErrF = if (poseErrF.isNaN() || wall - poseErrT > 2500) e else poseErrF + 0.08f * (e - poseErrF)
            poseErrT = wall
        }
        val visReliable = poseErrF.isNaN() || poseErrF < 2.5f   // sai < 2,5 m/s (~9 km/h) = tin
        val pL = o.laneProb[1]; val pR = o.laneProb[2]
        val off = if (pL > 0.5f && pR > 0.5f) -(o.laneY[1][0] + o.laneY[2][0]) / 2 else null
        // lech lan kieu openpilot (ldw.py): >= 50 km/h, vach gan + mo hinh doan dang troi sang lan ben
        val lClose = pL > 0.5f && o.laneY[1][0] > -(1.08f + 0.04f)
        val rClose = pR > 0.5f && o.laneY[2][0] < (1.08f - 0.04f)
        val side = when {
            v < 50 -> '-'
            o.desireL > 0.1f && lClose -> 'L'
            o.desireR > 0.1f && rClose -> 'R'
            else -> '-'
        }
        if (side != '-') { ldwSide = side; ldwHoldUntil = wall + 1200 }
        val ldw = if (wall < ldwHoldUntil && prefs.ldwOn) ldwSide else '-'
        var leadM: Float? = null; var ttc: Float? = null; var level = 0
        if (o.leadProb > 0.5f && o.leadX > 0) {
            leadM = o.leadX
            // cu ly xe truoc THAT SU co lai gan khong (dao ham leadX qua EMA nhanh/cham): model co the bao sai
            // cu ly (bao gan 5-9m dung yen) -> neu khoang cach khong thuc su co lai thi KHONG phu do PHANH oan.
            if (wall - vrelT > 1500 || leadXfast.isNaN()) { leadXfast = o.leadX; leadXslow = o.leadX }
            else { leadXfast += 0.5f * (o.leadX - leadXfast); leadXslow += 0.12f * (o.leadX - leadXslow) }
            val closingMeas = leadXslow - leadXfast   // >0 = cu ly dang co lai that su
            // toc do tuong doi: lead v cua mo hinh tru toc do xe theo chinh mo hinh (cung thang do, khong tre nhu GPS), lam muot
            val ego = if (o.poseVx > 0.5f) o.poseVx else vms
            val raw = o.leadV - ego
            vrelF = if (wall - vrelT > 1500 || vrelF.isNaN()) raw else vrelF + 0.3f * (raw - vrelF)
            vrelT = wall
            val vrel = vrelF
            if (vrel < -1f) ttc = o.leadX / -vrel
            if (v > 5 && o.leadX < Op.safeDist(v)) level = 1
            // do nhay (cai dat): som / vua / muon
            val t2 = when (prefs.fcwSens) { 0 -> 3.0f; 2 -> 2.0f; else -> 2.5f }
            val t3 = when (prefs.fcwSens) { 0 -> 1.8f; 2 -> 1.2f; else -> 1.5f }
            if (v > 15 && ttc != null && ttc < t2 && vrel < -2f) level = 2
            if (v > 15 && ttc != null && ttc < t3 && vrel < -3f && o.leadX < 2f * Op.safeDist(v)) level = 3
            // CHOT CHAN phanh ao:
            //  (a) thi giac khong tin (cam goc rong meo lam model doan sai van toc/cu ly) -> khong canh bao/phanh.
            //  (b) PHANH (muc 3) chi khi cu ly DO DUOC dang that su co lai; chi tin van toc model thi ha xuong muc 2.
            if (level >= 2 && !visReliable) level = minOf(level, 1)
            if (level >= 3 && closingMeas < 0.5f) level = 2
        } else { vrelF = Float.NaN; leadXfast = Float.NaN; leadXslow = Float.NaN }
        // PHANH chi bat khi muc 3 keo dai >= 0,4 s; da bat thi giu 2 s (khong chop tat lien tuc)
        if (level >= 3) { if (l3Since == 0L) l3Since = wall } else l3Since = 0L
        if (level >= 3 && wall - l3Since < 400) level = 2
        if (level >= 3) l3Hold = wall + 2000 else if (wall < l3Hold && leadM != null) level = 3
        // muc 2 can 2 lan lien tiep
        if (level == 2) { l2n++; if (l2n < 2) level = 1 } else if (level < 2) l2n = 0
        // cam vua rot/noi lai: khung dau hay nhay -> chan canh bao gia (khong keu PHANH oan)
        if (wall < camSettleUntil) { level = 0; ttc = null; vrelF = Float.NaN; l3Since = 0L; l3Hold = 0L; l2n = 0 }
        val near = leadM != null && (level > 0 || (ttc != null && ttc < 4f))
        calibYaw(o, v)
        main.post { onResult(o, g, v, ldw, off, leadM, ttc, level, near) }
    }

    private var lastNear = false

    /** Dong thong so ngan tren man (nhu video demo) de chup man hinh khi chay thu. */
    private fun statLine(o: Op.Out?): String {
        val m = model
        val ai = if (m == null) "AI đang nạp" else if (o == null || targetHz == 0) "AI ${m.backend} dừng" else "AI ${m.backend} ${lastMs}ms ${measHz}/s"
        val det = if (o != null) " · xe ${(o.leadProb * 100).roundToInt()}% · vạch ${(o.laneProb[1] * 100).roundToInt()}/${(o.laneProb[2] * 100).roundToInt()}%" else ""
        val cal = if (!prefs.autoCalib) "chỉnh tay" else if (prefs.calibrated) "HC xong" else "đang HC"
        val src = when (prefs.camSrc) { "mirror" -> "cam gương"; "70mai" -> "cam 70mai"; else -> "cam ĐT" }
        val lg = clog?.let { "log ${it.short}" } ?: ""
        val sg = if (prefs.signs) " · biển ${signDet?.info ?: "-"}" else ""
        val tg = if (prefs.tlight) " · đèn ${tlight?.info ?: "-"}" else ""
        val pr = if (o != null && pairMs >= 0) " · cặp ${pairMs}ms" else ""
        return "$ai$det$pr · $cal · $src · $lg$sg$tg"
    }
    private var clog: CloudLog? = null
    private var pairLogAt = 0L
    private var logAt = 0L
    private var lastXc = ""

    /** 1 dong/giay: du de biet NPU/CPU, xe truoc, vach, hieu chinh, nhiet khi nguoi dung dang lai. */
    private fun logTick(o: Op.Out?, v: Float, leadM: Float?) {
        val l = clog ?: return
        val now = SystemClock.elapsedRealtime()
        if (now - logAt < 1000) return
        logAt = now
        val m = model
        val ai = if (o == null) "AI dung(${stateCode()})" else
            "AI ${m?.backend} ${lastMs}ms ${if (targetHz == 0) 0 else measHz}/s xe ${(o.leadProb * 100).roundToInt()}%" +
            (if (leadM != null) " ${leadM.roundToInt()}m" else " --") + " vach ${(o.laneProb[1] * 100).roundToInt()}/${(o.laneProb[2] * 100).roundToInt()}" +
            " pose ${"%.1f".format(o.poseVx * 3.6f)}km/h"
        l.line("${v.roundToInt()}km/h · $ai · yaw ${"%.3f".format(calYaw)} pitch ${"%.3f".format(calPitch)} ${if (prefs.calibrated) "HC xong" else "HC dang"}" +
            " · pin ${batt.roundToInt()} nong $hot · taplo ${(prefs.hood * 100).roundToInt()}% · HUD ${if (hudOk) "noi" else "chua"} · gps ${if (gpsAt > 0) "co" else "khong"}")
        if (xcInfo != lastXc) { lastXc = xcInfo; l.line(xcInfo) }
        // chan doan lane 3D (23/11): cap khung vao mo hinh + so lan doi hinh hoc, moi 10 s
        if (now - pairLogAt >= 10_000) { pairLogAt = now
            l.line("cap khung ${pairMs}ms · hong $pairBad/$pairAll · doi hinh hoc $geomN · feed3d ${Scene3D.info}"); pairBad = 0; pairAll = 0; geomN = 0 }
    }

    private fun onResult(o: Op.Out, g: Op.Cam, v: Float, ldw: Char, off: Float?, leadM: Float?, ttc: Float?, level: Int, near: Boolean) {
        lastNear = near
        val offCm = off?.let { (it * 100).roundToInt() }
        val s = Snap(o, g, prefs.camH, v, ldw, offCm, leadM, ttc, level, if (targetHz == 0) 0 else measHz, lastMs, model?.backend ?: "-",
            batt, thermal, stateCode(), stateMsg(), hudMsg, hudOk, lat, lon, bearing, gpsAt > 0)
        s.hood = prefs.hood
        s.stat = if (prefs.showDbg) statLine(o) else ""   // dong test chi hien khi bat "Hien so lieu" (Ky thuat)
        signSnap(s, v)
        if (prefs.showDbg) s.dbg = calibInfo()
        onSnap?.invoke(s)
        logTick(o, v, leadM)
        beeper.setVolume(prefs.vol)
        beeper.update(level, ldw, prefs.beep)
        if (level >= 2 && level != lastLvLog) clog?.line("CANH BAO muc $level xe ${leadM?.roundToInt()} m ttc ${ttc?.let { "%.1f".format(it) }} v ${v.roundToInt()}")
        lastLvLog = level
        val now = SystemClock.elapsedRealtime()
        if (now - lastSend >= 95) {
            lastSend = now
            // A1|lan|lech cm|pL%|pR%|xe truoc m|dv km/h|TTC x10|muc|v km/h|trang thai
            val dv = if (leadM != null) ((o.leadV - v / 3.6f) * 3.6f).roundToInt() else 0
            val pkt = "A1|$ldw|${offCm ?: 0}|${(o.laneProb[1] * 100).roundToInt()}|${(o.laneProb[2] * 100).roundToInt()}|" +
                "${leadM?.roundToInt() ?: 0}|$dv|${ttc?.let { (it * 10).roundToInt().coerceAtMost(99) } ?: 0}|$level|${v.roundToInt()}|${stateCode()}"
            hud?.send(pkt.toByteArray())
        }
        applyRate()
    }

    private fun stateCode() = when {
        hot >= 3 -> 3
        parked -> 1
        hot > 0 -> 2
        model == null -> 4
        else -> 0
    }

    private fun stateMsg(): String {
        // "Cam: FFMPEG[...]" la dong test luong cam -> chi hien khi bat "Hien so lieu" (Ky thuat). Giu lai
        // cac thong bao that (Chua noi Wi-Fi, Dang nap mo hinh, Camera chua len hinh, Dang tu hieu chinh...).
        val lm = if (!prefs.showDbg && loadMsg.startsWith("Cam: ")) "" else loadMsg
        return when {
        lm.isNotEmpty() && camNoImage() -> lm
        lm.isNotEmpty() && !lm.startsWith("Camera") -> lm
        camNoImage() -> "Camera chưa lên hình (app khác đang dùng camera?)"
        bright in 0..12 -> "Ảnh camera quá tối hoặc bị che"
        hot >= 3 -> "Máy nóng: đã dừng AI, chạy lại khi mát"
        parked -> "Xe đỗ: tạm dừng AI cho mát máy"
        hot == 2 -> "Máy nóng: giảm còn 3 lần/giây"
        prefs.autoCalib && !prefs.calibrated && targetHz > 0 -> "Đang tự hiệu chỉnh camera (chạy thẳng trên 20 km/h)…"
        else -> ""
        }
    }

    private fun camNoImage(): Boolean {
        val c = cam ?: return false
        val now = SystemClock.elapsedRealtime()
        return c.isOpen && now - camOpenAt > 4000 && now - lastFrameAt > 3000
    }

    /** Khong co ket qua AI (dung/nong/dang nap): van cap nhat giao dien + giu HUD biet trang thai. */
    /** Ket qua doc bien (luong chinh): chot bien toc do (1 lan doc chac >= 0,55 hoac 2 lan cung so trong 3 s), giu bien canh bao 8 s, ghi CSDL. */
    private fun onSigns(hits: List<SignDet.Hit>) {
        val now = SystemClock.elapsedRealtime()
        for (h in hits) {
            if (h.cls == SignDet.SPEED) {
                spdAt = now
                val v = h.value ?: continue
                if (h.score >= 0.5f) votes += Triple(now, v, h.score)
            } else if (h.cls in SignDet.WARN && (lastSign == null || now - lastSignAt > 1500 || h.conf >= lastSign!!.conf)) {
                if (lastSign?.cls != h.cls || now - lastSignAt > 8000) clog?.line("BIEN ${SignDet.NAME[h.cls]} ${(h.conf * 100).roundToInt()}%${if (h.aux != null) " + bien phu" else ""}")
                lastSign = h; lastSignAt = now
            }
            if (h.cls != SignDet.SPEED || h.value != null) signDb?.add(h.cls, h.value, lat, lon, bearing, h.crop, h.aux)
        }
        votes.removeAll { now - it.first > 3000 }
        // chon 1 so duy nhat: nhieu phieu nhat, hoa thi diem cao nhat (truoc day lap qua tung phieu -> nhay 80/60)
        val best = votes.groupBy { it.second }.map { (v, l) -> Triple(v, l.size, l.maxOf { it.third }) }
            .filter { it.third >= 0.55f || it.second >= 2 }
            .maxWithOrNull(compareBy<Triple<Int, Int, Float>> { it.second }.thenBy { it.third })
        if (best != null) {
            if (limit != best.first) clog?.line("BIEN TOC DO ${best.first} km/h" + (navLim?.let { if (it != best.first) " · LECH ${if (waze?.ok == true) "Waze" else "GOFA"} $it -> theo camera" else " · trung ${if (waze?.ok == true) "Waze" else "GOFA"}" } ?: ""))
            limit = best.first; limitAt = now
        }
        if ((signDb?.size ?: 0) % 5 == 0) signDb?.flush()
    }

    /** Camera HUD chuyen tu Waze/GOFA: "C1|loai|met" ("C1|" = het). Ghi CSDL camera theo vi tri xe. */
    private fun onCam(m: String) {
        if (!m.startsWith("C1|")) return
        val f = m.substring(3).split('|')
        val t = f.getOrNull(0)?.firstOrNull()
        // truong 4 (firmware 1.0.57+): bien toc do Waze/GOFA (bong bong toc do), 0 = khong co
        // Waze Mod noi thang thi bo qua ban HUD chuyen (tranh 2 nguon gianh nhau); GOFA van di qua HUD
        val direct = waze?.ok == true
        if (!direct) f.getOrNull(2)?.toIntOrNull()?.let { k -> setNavLim(k, "HUD") }
        if (direct) return
        val d = f.getOrNull(1)?.toIntOrNull() ?: -1
        setCam(t, d)
    }

    private fun setNavLim(k: Int, src: String) {
        val nl = if (k in 5..150) k else null
        if (nl != navLim) { clog?.line("BIEN $src ${nl ?: "-"}"); navLim = nl; navLimAt = SystemClock.elapsedRealtime() }
    }

    private fun setCam(t: Char?, d: Int) {
        if (t == null) { if (camType != null) camDb?.flush(); camType = null; return }
        if (camType != t || camDist < 0 || d > camDist + 50) clog?.line("CAMERA ${CamDb.name(t)} ${if (d >= 0) "$d m" else ""}")
        camType = t; camDist = d; camAt = SystemClock.elapsedRealtime(); camLat = lat; camLon = lon
        if (gpsAt > 0 && d >= 0) camDb?.add(t, lat, lon, bearing, d)
    }

    /** Du lieu Waze Mod (Data API): bien toc do (bong bong) + camera gan nhat. null = mat ket noi. */
    private fun onWaze(b: Bundle?) {
        WazeNav.update(b) { d -> if (gpsAt > 0) road?.ahead(lat, lon, bearing, d) else null }
        if (b == null) { setNavLim(0, "WAZE"); setCam(null, -1); return }
        setNavLim(b.getInt("speed_limit", 0), "WAZE")
        @Suppress("DEPRECATION") val al = b.getParcelableArray("alerts")
        var t: Char? = null; var d = -1
        if (al != null) for (x in al) {
            val a = x as? Bundle ?: continue
            val c = a.getInt("type", 0)
            if (c == 2 || c == 3 || c in 40..46) { t = when (c) { 2 -> 'S'; 3 -> 'R'; 41 -> 'A'; else -> 'G' }; d = a.getInt("distance_m", -1); break }
        }
        setCam(t, d)
    }

    fun logLine(m: String) { clog?.line(m) }
    fun wazeStatus() = waze?.status ?: "chưa chạy"
    fun wazeRetry() { waze?.retry() }
    fun hudConnected() = hudOk
    fun hudStatus() = hudMsg + (if (prefs.hudMac.isNotEmpty()) " · ${prefs.hudMac}" else "")
    fun heatInfo() = "pin ${batt.roundToInt()}° · " + when (hot) { 0 -> "bình thường"; 1 -> "ấm, giảm nhẹ"; 2 -> "nóng, giảm còn 3 lần/giây"; else -> "quá nóng, đã dừng AI" }

    // ---------------- den giao thong ----------------
    private fun onTLight(r: TLight.Res) {
        val now = SystemClock.elapsedRealtime()
        tlResAt = now
        val before = tlTrack.col
        tlTrack.update(r, now)
        val col = tlTrack.col
        // log tung lan quet (toi da 1 dong / 1,5 s) khi thay den: mau, tin cay %, tam x,y %, rong x cao % -> doi chieu loi tu xa
        if (r.lamps.isNotEmpty() && now - tlLogAt > 1500) {
            tlLogAt = now
            fun p(x: Float) = (x * 100).roundToInt()
            clog?.line("DENQ hien ${col ?: '-'} chon ${tlTrack.want ?: '-'} quet ${r.col ?: '-'}${r.sec?.let { "/$it" } ?: ""} · " +
                r.lamps.joinToString(" · ") { l -> "${l.col}${p(l.conf)} @${p((l.x1 + l.x2) / 2)},${p((l.y1 + l.y2) / 2)} ${p(l.x2 - l.x1)}x${p(l.y2 - l.y1)}${l.sec?.let { " so $it" } ?: ""}" })
        }
        if (col != before) {
            clog?.line("DEN ${before ?: '-'} -> ${col ?: '-'}${tlTrack.cur(now)?.let { " con $it s" } ?: ""}")
            val v = if (now - gpsAt < 3000 && gpsKmh >= 0) gpsKmh else poseKmh
            // dang dung cho den do/vang -> chuyen xanh: nhac nhe
            if (col == 'G' && (before == 'R' || before == 'Y') && v < 3 && prefs.beep) beeper.green()
            applyRate()
        }
        if (col != null) pushTl(now)                                   // doi mau / doi so: bao HUD ngay (khong cho nhip 1 giay)
    }
    /** Dang dung cho den do/vang: AI openpilot nghi bot. */
    private fun tlRest(v: Float): Boolean {
        val c = tlTrack.col ?: return false
        return prefs.tlight && (c == 'R' || c == 'Y') && v < 3 && SystemClock.elapsedRealtime() - tlTrack.seenAt < 3000
    }
    /** Giu camera mo khi dung xe lau (xe do > 30 s) ma vua thay den trong 2 phut: van doc den / dem giay cho toi luc xanh. */
    private fun tlWait() = prefs.tlight && tlTrack.seenAt > 0 && SystemClock.elapsedRealtime() - tlTrack.seenAt < 120_000

    /** Gan vao Snap: bien toc do (giu 15 phut neu khong gap bien moi), bien vua qua 8 s, keu khi vuot > 3 km/h. */
    private fun signSnap(s: Snap, v: Float) {
        val now = SystemClock.elapsedRealtime()
        if (limit != null && now - limitAt > 15 * 60_000L) limit = null
        s.limit = limit
        if (now - objsAt < 1500) s.objs = objs
        if (scene3d && now - wideAt < 700) { s.wide = wide; s.wideSeq = wideAt; s.wideAge = now - wideAt }   // 23/11: 1,2 s -> 0,7 s (xe da qua van con hien)
        s.cal = (!prefs.autoCalib || prefs.calibrated) && (poseErrF.isNaN() || poseErrF < 2.5f)   // + thi giac khong tin (70mai meo: pose lech GPS)
        if (scene3d && now - laneMkAt < 1500) s.lm = laneMk
        val ct = camType
        if (ct != null && prefs.camAlert && now - camAt < 30_000) {
            // dem lui theo quang duong da di tu luc HUD bao (duong thang, du dung cho vai tram met)
            val gone = if (camDist >= 0 && gpsAt > 0) run { val dy = (lat - camLat) * 110_540.0; val dx = (lon - camLon) * 111_320.0 * kotlin.math.cos(Math.toRadians(lat)); kotlin.math.sqrt(dx * dx + dy * dy).toInt() } else 0
            val left = if (camDist >= 0) camDist - gone else -1
            if (camDist < 0 || left > -30) { s.camType = ct; s.camM = if (left >= 0) left else if (camDist < 0) null else 0 }
        }
        // uu tien: bien moi nhat giua bien camera doc va bien Waze/GOFA (HUD chuyen sang); khong co thi toc do mac dinh theo luat
        val nl = if (prefs.navLimit && (hudOk || waze?.ok == true)) navLim else null
        // luat 19/11: camera doc bien that la uu tien nhat. Doc xong so voi Waze/GOFA: trung so -> giu nguyen (van ghi nguon app);
        // khac so -> lay camera. Sau do Waze/GOFA doi so (sang duong khac) thi theo Waze/GOFA cho toi khi camera gap bien moi.
        if (nl != null && (limit == null || limit == nl || navLimAt > limitAt)) { s.limit = nl; s.limitSrc = if (waze?.ok == true) "Waze" else "GOFA" }
        if (s.limit == null && prefs.defLimit && gpsAt > 0) road?.cur?.let { d -> if (d.kmh != null) { s.limit = d.kmh; s.limitWhy = d.why } }
        if (lastSign != null && now - lastSignAt <= 8000) s.sign = lastSign
        if (prefs.tlight && tlTrack.col != null) { s.tl = tlTrack.col; s.tlSec = tlTrack.cur(now); if (now - tlResAt < 800) s.tlLamps = tlTrack.lamps }
        val lim = s.limit
        s.over = lim != null && v > lim + 3
        // chi keu khi vuot bien that; toc do mac dinh chi hien chu (tranh keu sai tren duong ngoai do thi 80-90)
        val ok = prefs.overKmh
        if (ok >= 0 && lim != null && v > lim + ok + 0.5f && s.limitWhy == null && prefs.beep && now - overBeepAt > 10_000) { overBeepAt = now; beeper.test() }
    }

    private fun publishIdle() {
        logTick(null, if (gpsAt > 0) maxOf(0f, gpsKmh) else 0f, null)
        publishScene()
    }

    /** Snap khong co ket qua openpilot (AI dang nghi): thong so + xe YOLO cho man 3D / lop ve. */
    private fun publishScene() {
        onSnap?.invoke(Snap(null, geom, prefs.camH, if (gpsAt > 0) maxOf(0f, gpsKmh) else 0f, '-', null, null, null, 0,
            if (targetHz == 0) 0 else measHz, lastMs, model?.backend ?: "-", batt, thermal, stateCode(), stateMsg(), hudMsg, hudOk, lat, lon, bearing, gpsAt > 0)
            .also { it.hood = prefs.hood; it.stat = if (prefs.showDbg) statLine(null) else ""; signSnap(it, it.vKmh); if (prefs.showDbg) it.dbg = calibInfo() })
    }

    // ---------------- tan suat + nhiet ----------------
    private fun applyRate() {
        val v = if (SystemClock.elapsedRealtime() - gpsAt < 3000 && gpsKmh >= 0) gpsKmh else poseKmh
        // 17/11: giam tai (pin 44-45 do khi chay pho): toi da 10 lan/giay, binh thuong 5 (cap khung cach dung 0,2 s nhu mo hinh hoc)
        var hz = when {
            v > 80 || lastNear -> 10
            v < 5 -> 4
            else -> 5
        }
        // che do (cai dat): tiet kiem toi da 3 lan/giay (5 khi > 80 km/h / xe gan); toi da bo gioi han muc nong 1
        if (prefs.perf == 0) hz = if (v > 80 || lastNear) 5 else 3
        hz = when (hot) { 1 -> if (prefs.perf == 2) hz else if (v > 80) minOf(hz, 6) else minOf(hz, 4); 2 -> minOf(hz, 3); 3 -> 0; else -> hz }
        // dung den do / vang (xe < 3 km/h, den moi thay < 3 s): AI openpilot nghi bot, nhuong may cho doc den + dem giay
        if (tlRest(v)) hz = minOf(hz, 2)
        if (parked || model == null) hz = 0
        targetHz = hz
        val c = cam ?: return
        // Khong can camera: AI dung va khong ai xem man hinh -> tat han camera
        if (hz == 0 && !uiVisible && !tlWait()) { if (c.isOpen) c.close() }
        else { if (!c.isOpen) { c.open(); camOpenAt = SystemClock.elapsedRealtime() }; c.setRate(if (hz == 0) 15 else hz) }
    }

    private val tick = object : Runnable {
        override fun run() {
            readThermal()
            // xe do: GPS < 2 km/h lien tuc 30 giay
            val now = SystemClock.elapsedRealtime()
            val fresh = now - gpsAt < 5000
            if (fresh && gpsKmh < 2f) { if (stillSince == 0L) stillSince = now; if (now - stillSince > 30_000) parked = true }
            else stillSince = 0L
            if (parked && fresh && gpsKmh > 5f) parked = false
            if (!fresh) parked = false
            applyRate()
            if (targetHz == 0) {
                publishIdle()
                hud?.send("A1|-|0|0|0|0|0|0|0|${if (fresh) gpsKmh.roundToInt() else 0}|${stateCode()}".toByteArray())
            }
            pushNav()
            pushSign(now)
            pushTl(now)
            if (now - wazeTry > 30_000 && waze?.ok != true) { wazeTry = now; waze?.start() }
            if (now - lastNotif > 10_000) { lastNotif = now; goForeground(notifText()) }
            main.postDelayed(this, 1000)
        }
    }

    /** Bien toc do doc tu camera -> HUD ("S1|km/h", 0 = het): khi doi so hoac 5 s 1 lan (HUD tu bo sau 20 s). */
    private fun pushSign(now: Long) {
        val h = hud ?: return
        if (!hudOk) { sigSent = -1; return }
        val v = if (prefs.hudSign) (limit?.takeIf { now - limitAt <= 15 * 60_000L } ?: 0) else 0
        if (v == sigSent && (v == 0 || now - sigAt < 5000)) return
        sigSent = v; sigAt = now
        h.sendSign("S1|$v".toByteArray())
    }

    private var tlSent = ""
    private var tlSentAt = 0L
    private var tlOffLeft = 0

    /** Den giao thong -> HUD ("T1|R|25", giay "-" = khong doc duoc, "T1|-" = het): 1 lan/giay khi con thay den, ngay khi doi mau / doi so.
     *  HUD tu bo sau 3 s khong co goi; het den thi bao "T1|-" 2 lan cho chac. */
    private fun pushTl(now: Long) {
        val h = hud ?: return
        if (!hudOk) { tlSent = ""; return }
        val c = tlTrack.col
        val live = prefs.tlight && c != null && now - tlTrack.seenAt < 3000
        val msg = if (live) "T1|$c|${tlTrack.cur(now) ?: "-"}" else "T1|-"
        if (!live) {
            if (tlSent != "T1|-") tlOffLeft = 2
            if (tlOffLeft <= 0) { tlSent = msg; return }
            tlOffLeft--
        } else if (msg == tlSent && now - tlSentAt < 1000) return
        tlSent = msg; tlSentAt = now
        h.sendTl(msg.toByteArray())
    }

    private var thermalAt = 0L

    private fun readThermal() {
        val now = SystemClock.elapsedRealtime()
        if (now - thermalAt < 5000) return
        thermalAt = now
        val bi = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        batt = (bi?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0) / 10f
        thermal = if (Build.VERSION.SDK_INT >= 29) (getSystemService(POWER_SERVICE) as PowerManager).currentThermalStatus else 0
        hot = when {
            batt >= 47 || thermal >= PowerManager.THERMAL_STATUS_SEVERE -> 3
            hot == 3 && (batt >= 44 || thermal >= PowerManager.THERMAL_STATUS_MODERATE) -> 3
            batt >= 43 || thermal == PowerManager.THERMAL_STATUS_MODERATE -> 2
            batt >= 40 || thermal == PowerManager.THERMAL_STATUS_LIGHT -> 1
            else -> 0
        }
    }

    // ---------------- GPS ----------------
    private val locL = LocationListener { l: Location ->
        gpsAt = SystemClock.elapsedRealtime()
        gpsKmh = if (l.hasSpeed()) l.speed * 3.6f else gpsKmh
        lat = l.latitude; lon = l.longitude
        if (l.hasBearing() && l.speed > 1.5f) bearing = l.bearing
        road?.onFix(lat, lon, bearing, gpsKmh)
        Nav.onFix(l.latitude, l.longitude, l.speed, if (l.hasBearing()) l.bearing else null, if (l.hasAccuracy()) l.accuracy else 20f)
    }

    @SuppressLint("MissingPermission")
    private fun startGps() {
        if (!hasPerm(Manifest.permission.ACCESS_FINE_LOCATION)) return
        try {
            (getSystemService(LOCATION_SERVICE) as LocationManager)
                .requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, locL, Looper.getMainLooper())
        } catch (e: Exception) { Log.w(TAG, "gps", e) }
    }

    // ---------------- thong bao ----------------
    private fun notifText(): String {
        val s = stateMsg()
        return (if (s.isNotEmpty()) s else "Đang theo dõi làn · ${targetHz} lần/giây") + " · " + hudMsg
    }

    private fun hasPerm(p: String) = checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED

    private fun goForeground(text: String) {
        val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CH) == null)
            nm.createNotificationChannel(NotificationChannel(CH, "Theo dõi làn", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, AdasService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CH)
            .setSmallIcon(R.drawable.ic_notif)
            .setContentTitle("EVCarHUD LANE")
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Dừng", stop).build())
            .build()
        var type = 0
        if (Build.VERSION.SDK_INT >= 29) {
            if (hasPerm(Manifest.permission.CAMERA)) type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
            if (hasPerm(Manifest.permission.ACCESS_FINE_LOCATION)) type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            if (Build.VERSION.SDK_INT < 31 || hasPerm(Manifest.permission.BLUETOOTH_CONNECT))
                type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        }
        try {
            if (Build.VERSION.SDK_INT >= 29 && type != 0) startForeground(NID, n, type) else startForeground(NID, n)
        } catch (e: Exception) {
            Log.w(TAG, "fg", e)
            try { startForeground(NID, n) } catch (_: Exception) {}
        }
    }

    companion object {
        private const val TAG = "AdasService"
        /** Den giao thong chi quet khi xe duoi toc do nay (gan giao lo / cho den). */
        private const val TL_MAX_KMH = 35f
        private const val CH = "adas"
        private const val NID = 7
        const val ACTION_STOP = "stop"
        const val PW = 960            // anh xem (preview) ve 960x540 thay vi 640x360 -> net hon khi phong len man rong (bot nhoe)
        const val HOOD_R = 72
        const val HOOD_C = 48
        const val PH = 540

        fun start(ctx: Context) {
            val i = Intent(ctx, AdasService::class.java)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
    }
}
