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
import android.media.Image
import android.os.BatteryManager
import android.os.Binder
import android.os.Build
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
    private var cam: CamSource? = null
    @Volatile private var model: OpModel? = null
    private var hud: HudLink? = null
    private var hudMsg = "Chưa nối HUD"
    private var hudOk = false

    var onSnap: ((Snap) -> Unit)? = null
    /** Khung xem truoc tu ve (640x360) tu chinh luong anh AI: nhieu may khong cho mo 2 luong camera cung luc. */
    var onPreview: ((Bitmap) -> Unit)? = null
    private val prevBmp = Array(2) { Bitmap.createBitmap(PW, PH, Bitmap.Config.ARGB_8888) }
    private val prevPx = IntArray(PW * PH)
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
    private var ldwSide = '-'
    @Volatile private var lastMs = 0

    // ---- tu hieu chinh ----
    // goc cui (pitch) + nghieng (roll): cam bien trong luc, loc cham ~10 s (bo qua luc tang/giam toc manh)
    // goc lech trai/phai (yaw): huong chuyen dong mo hinh thay (pose) khi chay thang > 20 km/h, giong calibrationd openpilot
    private var calPitch = 0.0
    private var calYaw = 0.0
    private var calRoll = 0.0
    private var gP = Double.NaN
    private var gR = 0.0
    private var geomAt = 0L
    private val yawObs = FloatArray(50)
    private var yawN = 0
    private var yawGood = 0
    @Volatile private var lastFrameAt = 0L
    private var camOpenAt = 0L

    override fun onBind(i: Intent?): IBinder = LocalBinder()

    override fun onCreate() {
        super.onCreate()
        prefs = Prefs(this)
        goForeground("Đang khởi động…")
        try {
            cam = CamSource(this, ::onFrame) { e -> main.post { loadMsg = e; publishIdle() } }
        } catch (e: Exception) { loadMsg = "Không có camera: ${e.message}" }
        calPitch = prefs.pitch.toDouble(); calYaw = prefs.yaw.toDouble(); calRoll = prefs.roll.toDouble()
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
        hud = HudLink(this) { m, ok -> hudMsg = m; hudOk = ok; publishIdle() }.also {
            it.onMac = { a -> prefs.hudMac = a }
            if (hasPerm(Manifest.permission.BLUETOOTH_CONNECT)) it.start(prefs.hudMac) else hudMsg = "Chưa cho quyền Bluetooth"
        }
        main.post(tick)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) { stopSelf(); return START_NOT_STICKY }
        return START_STICKY
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        hud?.stop()
        cam?.release()
        try { (getSystemService(SENSOR_SERVICE) as SensorManager).unregisterListener(gravL) } catch (_: Exception) {}
        try { (getSystemService(LOCATION_SERVICE) as LocationManager).removeUpdates(locL) } catch (_: Exception) {}
        ai.post { model?.close(); model = null; aiThread.quitSafely() }
        super.onDestroy()
    }

    // ---------------- giao dien goi ----------------
    fun setPreview(s: Surface?) { cam?.setPreview(s) }
    fun camInfo(): CamSource? = cam
    fun currentGeom(): Op.Cam? = geom

    /** Doi hieu chinh / chieu cao / bo tang toc. */
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
    fun setManualVp(vpX: Float, vpY: Float) {
        val c = cam ?: return
        val w = c.size.width; val h = c.size.height; val f = c.focalPx
        val vx = vpX * w - w / 2.0
        val vy = vpY * h - h / 2.0
        calYaw = atan(vx / f)
        calPitch = -atan2(vy, sqrt(f * f + vx * vx))
        calRoll = 0.0
        prefs.autoCalib = false
        saveCalib(); rebuildGeom()
    }

    fun setAuto(on: Boolean) {
        prefs.autoCalib = on
        if (on) { yawGood = 0; prefs.calibrated = false; gP = Double.NaN }
    }

    private fun saveCalib() { prefs.pitch = calPitch.toFloat(); prefs.yaw = calYaw.toFloat(); prefs.roll = calRoll.toFloat() }

    private fun rebuildGeom() {
        val c = cam ?: return
        val g = Op.Cam(c.size.width, c.size.height, c.focalPx, calPitch, calYaw, calRoll)
        geomAt = SystemClock.elapsedRealtime()
        ai.post { geom = g; warpKey = ""; ringCount = 0 }
    }

    /** Thong tin hieu chinh cho Cai dat (do). */
    fun calibInfo(): String {
        val c = cam ?: return "Chưa có camera"
        val hf = Math.toDegrees(2 * atan(c.size.width / 2 / c.focalPx))
        fun d(r: Double) = "%.1f°".format(Math.toDegrees(r))
        return "Camera ${c.size.width}x${c.size.height}, góc ngang ${"%.0f".format(hf)}° (${c.focalSrc})\nCúi ${d(calPitch)} · lệch ${d(calYaw)} · nghiêng ${d(calRoll)}" +
            (if (prefs.autoCalib) (if (prefs.calibrated) " · tự hiệu chỉnh xong" else " · đang tự hiệu chỉnh") else " · chỉnh tay") +
            "\n" + aiInfo()
    }

    /** Thong tin bo xu ly AI + ket qua tu kiem NPU. */
    fun aiInfo(): String {
        val m = model ?: return "AI chưa nạp"
        val w = warpMed
        val outPct = if (w != null) 100 * w.y.count { it < 0 } / w.y.size else -1
        val o = lastOut
        return "AI ${m.backend}, ${lastMs} ms" + (if (m.selfTest.isNotEmpty()) " · ${m.selfTest}" else "") +
            "\nẢnh vào: ${if (outPct >= 0) "$outPct% ngoài khung" else "chưa có"} · cam ${yRowS}/${uvRowS}/${uvPixS}" +
            (if (o != null) " · khung sống: xe ${(o.leadProb * 100).roundToInt()}% vạch ${(o.laneProb[1] * 100).roundToInt()}/${(o.laneProb[2] * 100).roundToInt()}% pose ${"%.1f".format(o.poseVx)}" else "")
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
            if (!prefs.autoCalib) return
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
            yawGood = if (abs(med) < 0.012) yawGood + 1 else 0
            if (yawGood >= 2 && !prefs.calibrated) prefs.calibrated = true
            saveCalib()
            if (abs(med) > 0.002) rebuildGeom()
        }
    }

    // ---------------- camera -> AI ----------------
    private fun onFrame(im: Image) {
        val now = SystemClock.elapsedRealtime()
        lastFrameAt = now
        val cb = onPreview
        if (uiVisible && cb != null && now - lastPrev >= (if (hot >= 1) 200 else 100)) { lastPrev = now; try { drawPreview(im, cb) } catch (e: Exception) { Log.w(TAG, "prev", e) } }
        else if (now - lastPrev >= 1000) { lastPrev = now; try { bright = meanY(im) } catch (_: Exception) {} }
        if (now - hoodAt >= 300) { hoodAt = now; try { hoodSample(im) } catch (_: Exception) {} }
        val hz = targetHz
        if (hz <= 0 || busy || model == null || now - lastAccept < (1000 / hz) * 0.85) { im.close(); return }
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
    private fun drawPreview(im: Image, cb: (Bitmap) -> Unit) {
        val p = im.planes
        val yb = p[0].buffer; val ub = p[1].buffer; val vb = p[2].buffer
        val yRow = p[0].rowStride; val yPix = p[0].pixelStride
        val uvRow = p[1].rowStride; val uvPix = p[1].pixelStride
        val w = im.width; val h = im.height
        val flip = cam?.flip == true
        val sx = w / PW.toFloat(); val sy = h / PH.toFloat()
        val ny = yb.limit(); val nu = minOf(ub.limit(), vb.limit())
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
                val r = (Y + 1.402f * V).toInt().coerceIn(0, 255)
                val g = (Y - 0.344f * U - 0.714f * V).toInt().coerceIn(0, 255)
                val b = (Y + 1.772f * U).toInt().coerceIn(0, 255)
                prevPx[row + x] = (0xff shl 24) or (r shl 16) or (g shl 8) or b
            }
        }
        bright = (sum / (PW * PH)).toInt()
        val bm = prevBmp[prevI]; prevI = 1 - prevI
        bm.setPixels(prevPx, 0, PW, 0, 0, PW, PH)
        main.post { cb(bm) }
    }

    // ---- tu tim mep taplo: hang anh dung yen khi xe chay (taplo) vs hang thay doi (duong) ----
    private var hoodAt = 0L
    private val hoodPrev = IntArray(HOOD_R * HOOD_C)
    private val hoodAct = FloatArray(HOOD_R)
    private var hoodN = 0

    private fun hoodSample(im: Image) {
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

    private fun meanY(im: Image): Int {
        val p = im.planes[0]; val b = p.buffer; val n = b.limit()
        var s = 0L; var c = 0
        var i = 0
        while (i < n) { s += b.get(i).toInt() and 0xff; c++; i += 97 }
        return if (c > 0) (s / c).toInt() else 0
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
        val key = "$yRow/$uvRow/$uvPix/${g.hashCode()}"
        if (key != warpKey) {
            warpMed = Op.Warp(g, false, yRow, uvRow, uvPix, c.flip)
            warpBig = Op.Warp(g, true, yRow, uvRow, uvPix, c.flip)
            warpKey = key; ringCount = 0
        }
        val t0 = SystemClock.elapsedRealtime()
        val slot = ringI
        warpMed!!.pack(yArr, uArr, vArr, ringMed[slot], 0)
        warpBig!!.pack(yArr, uArr, vArr, ringBig[slot], 0)
        ringT[slot] = ts
        ringI = (ringI + 1) % ringN
        if (ringCount < ringN) ringCount++
        // khung cu cach ~0,2 s (mo hinh hoc tren 2 khung cach 0,2 s)
        var best = slot; var bestErr = Long.MAX_VALUE
        for (k in 1 until ringCount) {
            val j = (slot - k + ringN) % ringN
            val err = abs((ts - ringT[j]) - 200)
            if (err < bestErr) { bestErr = err; best = j }
        }
        if (bestErr > 120) best = slot
        System.arraycopy(ringMed[best], 0, img, 0, Op.PACK); System.arraycopy(ringMed[slot], 0, img, Op.PACK, Op.PACK)
        System.arraycopy(ringBig[best], 0, big, 0, Op.PACK); System.arraycopy(ringBig[slot], 0, big, Op.PACK, Op.PACK)

        val o = Op.Out()
        Op.parseVision(m.runVision(img, big), o)
        lastOut = o; yRowS = yRow; uvRowS = uvRow; uvPixS = uvPix

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
        val ldw = if (wall < ldwHoldUntil) ldwSide else '-'
        var leadM: Float? = null; var ttc: Float? = null; var level = 0
        if (o.leadProb > 0.5f && o.leadX > 0) {
            leadM = o.leadX
            val vrel = o.leadV - vms
            if (vrel < -0.5f) ttc = o.leadX / -vrel
            if (v > 5 && o.leadX < Op.safeDist(v)) level = 1
            if (v > 10 && ttc != null && ttc < 2.5f) level = 2
            if (v > 10 && ttc != null && ttc < 1.5f) level = 3
        }
        val near = leadM != null && (level > 0 || (ttc != null && ttc < 4f))
        calibYaw(o, v)
        main.post { onResult(o, g, v, ldw, off, leadM, ttc, level, near) }
    }

    private var lastNear = false

    private fun onResult(o: Op.Out, g: Op.Cam, v: Float, ldw: Char, off: Float?, leadM: Float?, ttc: Float?, level: Int, near: Boolean) {
        lastNear = near
        val offCm = off?.let { (it * 100).roundToInt() }
        val s = Snap(o, g, prefs.camH, v, ldw, offCm, leadM, ttc, level, if (targetHz == 0) 0 else measHz, lastMs, model?.backend ?: "-",
            batt, thermal, stateCode(), stateMsg(), hudMsg, hudOk, lat, lon, bearing, gpsAt > 0)
        s.hood = prefs.hood
        onSnap?.invoke(s)
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

    private fun stateMsg() = when {
        loadMsg.isNotEmpty() && camNoImage() -> loadMsg
        loadMsg.isNotEmpty() && !loadMsg.startsWith("Camera") -> loadMsg
        camNoImage() -> "Camera chưa lên hình (app khác đang dùng camera?)"
        bright in 0..12 -> "Ảnh camera quá tối hoặc bị che"
        hot >= 3 -> "Máy nóng: đã dừng AI, chạy lại khi mát"
        parked -> "Xe đỗ: tạm dừng AI cho mát máy"
        hot == 2 -> "Máy nóng: giảm còn 4 lần/giây"
        prefs.autoCalib && !prefs.calibrated && targetHz > 0 -> "Đang tự hiệu chỉnh camera (chạy thẳng trên 20 km/h)…"
        else -> ""
    }

    private fun camNoImage(): Boolean {
        val c = cam ?: return false
        val now = SystemClock.elapsedRealtime()
        return c.isOpen && now - camOpenAt > 4000 && now - lastFrameAt > 3000
    }

    /** Khong co ket qua AI (dung/nong/dang nap): van cap nhat giao dien + giu HUD biet trang thai. */
    private fun publishIdle() {
        onSnap?.invoke(Snap(null, geom, prefs.camH, if (gpsAt > 0) maxOf(0f, gpsKmh) else 0f, '-', null, null, null, 0,
            if (targetHz == 0) 0 else measHz, lastMs, model?.backend ?: "-", batt, thermal, stateCode(), stateMsg(), hudMsg, hudOk, lat, lon, bearing, gpsAt > 0)
            .also { it.hood = prefs.hood })
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
        hz = when (hot) { 1 -> minOf(hz, 6); 2 -> minOf(hz, 4); 3 -> 0; else -> hz }
        if (parked || model == null) hz = 0
        targetHz = hz
        val c = cam ?: return
        // Khong can camera: AI dung va khong ai xem man hinh -> tat han camera
        if (hz == 0 && !uiVisible) { if (c.isOpen) c.close() }
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
            if (now - lastNotif > 10_000) { lastNotif = now; goForeground(notifText()) }
            main.postDelayed(this, 1000)
        }
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
            .setContentTitle("EVCarHUD Làn")
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
        private const val CH = "adas"
        private const val NID = 7
        const val ACTION_STOP = "stop"
        const val PW = 640
        const val HOOD_R = 72
        const val HOOD_C = 48
        const val PH = 360

        fun start(ctx: Context) {
            val i = Intent(ctx, AdasService::class.java)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
    }
}
