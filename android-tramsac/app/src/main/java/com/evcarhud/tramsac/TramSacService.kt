package com.evcarhud.tramsac

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import java.util.concurrent.Executors
import kotlin.math.roundToInt

/**
 * Dich vu chay nen: lay GPS, tai danh sach tram quanh xe, chon tram gan nhat va gui xuong HUD.
 *  - Tai lai danh sach khi: chua co, xe di xa diem tai lan truoc > 1/3 ban kinh, hoac sau 10 phut; toi thieu 30 giay/lan.
 *  - Tinh lai tram gan nhat moi lan co vi tri moi (~2 giay), gui HUD khi thay doi va nhac lai moi 30 giay.
 */
class TramSacService : Service() {
    companion object {
        const val STOP = "stop"
        const val TEST = "test"
        const val SEND = "send"     // gui ngay tram dang chon (vua chon tram dich)
        private const val CH = "tramsac"
        private const val NID = 1
        private const val FETCH_MIN_MS = 30_000L
        private const val FETCH_MAX_AGE_MS = 10 * 60_000L
        private const val RESEND_MS = 30_000L

        fun start(ctx: Context) {
            val i = Intent(ctx, TramSacService::class.java)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, TramSacService::class.java).setAction(STOP))
        }
    }

    private val main = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor()
    private lateinit var prefs: Prefs
    private lateinit var lm: LocationManager
    private lateinit var ble: HudBle
    private var wake: android.os.PowerManager.WakeLock? = null

    private var stations: List<Station> = emptyList()
    private var fetchLat = 0.0
    private var fetchLon = 0.0
    private var fetchedAt = 0L
    private var fetchTriedAt = 0L
    private var fetching = false
    private var last: Location? = null
    private var heading: Double? = null
    private var picked: Nearby? = null
    private var lastPayload = ""
    private var lastSentAt = 0L
    private var posSent: Location? = null      // vi tri da gui HUD (thoi tiet), gui lai khi di xa ~5 km

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        prefs = Prefs(this)
        lm = getSystemService(LOCATION_SERVICE) as LocationManager
        ble = HudBle(this) { s ->
            AppState.update { hud = s }
            if (ble.ready) { push(force = true); posSent = null; last?.let { sendPos(it) } }   // vua noi: gui lai vi tri cho thoi tiet
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == STOP) {
            stopSelf()
            return START_NOT_STICKY
        }
        goForeground("Đang tìm trạm sạc…")
        if (intent?.action == SEND) {
            if (!AppState.running) onStartCommand(null, flags, startId)
            testUntil = 0
            last?.let { onFix(it) }
            push(force = true)
            push(force = true)
            return START_STICKY
        }
        if (intent?.action == TEST) {
            if (!AppState.running) onStartCommand(null, flags, startId)
            sendTest()
            return START_STICKY
        }
        if (!AppState.running) {
            AppState.update { running = true; gps = "Đang chờ GPS…"; data = "—" }
            // Tat man hinh: may ngu sau thi bo dem 30 giay khong chay, GPS dung yen khong bao -> HUD mat tram.
            wake = (getSystemService(POWER_SERVICE) as android.os.PowerManager)
                .newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "EVCarHUD:tramsac").apply {
                    setReferenceCounted(false); acquire(12 * 3600_000L)
                }
            startGps()
            ble.start(prefs.hudMac)
            main.postDelayed(tick, RESEND_MS)
        } else {
            ble.start(prefs.hudMac)          // vua doi HUD / cai dat: noi lai
            fetchedAt = 0; fetchTriedAt = 0  // tai lai theo nguon moi
            last?.let { onFix(it) }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        try { wake?.release() } catch (_: Exception) {}
        wake = null
        try { lm.removeUpdates(loc) } catch (_: Exception) {}
        ble.send(HudProto.encode(null, 0))  // xoa o tram sac tren HUD (neu con noi), cho ghi xong roi moi ngat
        val b = ble
        Handler(Looper.getMainLooper()).postDelayed({ b.stop() }, 800)
        io.shutdownNow()
        AppState.update { running = false; gps = "Đã dừng"; hud = "Đã dừng"; picked = null; ranked = emptyList() }
        super.onDestroy()
    }

    private val tick = object : Runnable {
        override fun run() {
            push(force = true)
            last?.let { maybeFetch(it) }
            main.postDelayed(this, RESEND_MS)
        }
    }

    // ---------------- GPS ----------------
    @SuppressLint("MissingPermission")
    private fun startGps() {
        try {
            if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER))
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 2000L, 0f, loc, Looper.getMainLooper())
            if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER))
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 10000L, 20f, loc, Looper.getMainLooper())
            (lm.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                ?: lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER))?.let { onFix(it) }
        } catch (e: SecurityException) {
            AppState.update { gps = "Thiếu quyền vị trí" }
        }
    }

    private val loc = object : LocationListener {
        override fun onLocationChanged(l: Location) = onFix(l)
        override fun onProviderEnabled(provider: String) {}
        override fun onProviderDisabled(provider: String) { AppState.update { gps = "GPS đang tắt" } }
        @Deprecated("Deprecated in Java")
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
    }

    private fun onFix(l: Location) {
        val prev = last
        // Mang di dong (sai so lon) khong de len vi tri GPS con moi
        if (l.provider == LocationManager.NETWORK_PROVIDER && prev != null &&
            prev.provider == LocationManager.GPS_PROVIDER && l.time - prev.time < 30_000) return
        last = l
        val kmh = if (l.hasSpeed()) (l.speed * 3.6f).roundToInt() else 0
        // Huong xe: tu GPS khi dang chay (> ~8 km/h); dung yen thi giu huong cu toi da 2 phut
        if (l.hasBearing() && l.hasSpeed() && l.speed > 2.2f) heading = l.bearing.toDouble()
        else if (prev != null && Geo.distM(prev.latitude, prev.longitude, l.latitude, l.longitude) > 25 && kmh > 8)
            heading = Geo.bearing(prev.latitude, prev.longitude, l.latitude, l.longitude)
        val acc = if (l.hasAccuracy()) " ±${l.accuracy.roundToInt()} m" else ""
        AppState.update { gps = "Có vị trí$acc"; lastFix = l.latitude to l.longitude; speedKmh = kmh }
        maybeFetch(l)
        recompute()
        sendPos(l)
    }

    /** Vi tri cho thoi tiet tren HUD: lan dau sau khi noi, roi moi khi lech > 5 km. Ten noi lay tu Geocoder (neu co). */
    private fun sendPos(l: Location) {
        if (!ble.ready || !ble.hasPos) return
        val p = posSent
        if (p != null && Geo.distM(p.latitude, p.longitude, l.latitude, l.longitude) < 5000) return
        posSent = l
        Thread {
            val place = try {
                @Suppress("DEPRECATION")
                android.location.Geocoder(this, java.util.Locale("vi", "VN")).getFromLocation(l.latitude, l.longitude, 1)
                    ?.firstOrNull()?.let { a -> a.locality ?: a.subAdminArea ?: a.adminArea }.orEmpty()
            } catch (_: Exception) { "" }
            main.post { ble.sendPos(HudProto.encodePos(l.latitude, l.longitude, place)) }
        }.start()
    }

    // ---------------- Tai danh sach tram ----------------
    private fun maybeFetch(l: Location) {
        if (fetching) return
        val now = SystemClock.elapsedRealtime()
        val radiusM = prefs.radiusKm * 1000
        val moved = if (fetchedAt == 0L) Double.MAX_VALUE else Geo.distM(fetchLat, fetchLon, l.latitude, l.longitude)
        val need = fetchedAt == 0L || moved > radiusM / 3.0 || now - fetchedAt > FETCH_MAX_AGE_MS
        if (!need || now - fetchTriedAt < FETCH_MIN_MS) return
        fetching = true
        fetchTriedAt = now
        val lat = l.latitude; val lon = l.longitude
        AppState.update { data = "Đang tải danh sách trạm…" }
        io.execute {
            try {
                val list = Sources.fetch(this, prefs, lat, lon, radiusM)
                main.post {
                    fetching = false
                    stations = list; fetchLat = lat; fetchLon = lon; fetchedAt = SystemClock.elapsedRealtime()
                    AppState.update { data = "${list.size} trạm trong ${prefs.radiusKm} km (${srcName()})" }
                    recompute()
                }
            } catch (e: Exception) {
                main.post {
                    fetching = false
                    AppState.update { data = "Lỗi tải (${srcName()}): ${e.message ?: e.javaClass.simpleName}" }
                }
            }
        }
    }

    private fun srcName() = when (prefs.source) {
        Sources.OCM -> "OpenChargeMap"
        Sources.CUSTOM -> "máy chủ riêng"
        Sources.VF -> "VinFast"
        Sources.OSM -> "OpenStreetMap"
        else -> "VinFast + OSM"
    }

    private fun recompute() {
        val l = last ?: return
        val ranked = Geo.rank(l.latitude, l.longitude, heading, stations)
        val tgt = prefs.target
        val p = if (tgt != null) Geo.rank(l.latitude, l.longitude, heading, listOf(tgt)).first() else Geo.pick(ranked)
        picked = p
        AppState.update { this.ranked = ranked.take(200); this.picked = p }
        push(force = false)
        val text = p?.let { "${it.st.name} · ${Geo.fmtDist(it.distM)} · ${Geo.sideText(it.side)}" }
            ?: "Chưa có trạm nào gần"
        goForeground(text)
    }

    private var testUntil = 0L

    /** Gui 1 tram gia de kiem tra HUD khi chua lai xe; giu tren HUD 15 giay. */
    private fun sendTest() {
        testUntil = SystemClock.elapsedRealtime() + 15_000
        val fake = Nearby(Station("Trạm thử EVCarHUD", 0.0, 0.0, "Thử", 60, 4), 850, 35)
        ble.send(HudProto.encode(fake, 1))
        AppState.update { sentAt = System.currentTimeMillis(); data = "Đã gửi trạm thử (giữ 15 giây)" }
    }

    /** Gui tram dang chon xuong HUD khi doi (khoang cach lam tron 10 m) hoac khi force. */
    private fun push(force: Boolean) {
        val n = picked.takeIf { stations.isNotEmpty() || prefs.target != null }
        val rounded = n?.copy(distM = if (n.distM < 1000) n.distM / 10 * 10 else n.distM / 100 * 100)
        val payload = HudProto.encode(rounded, stations.size, prefs.target != null)
        val s = String(payload, Charsets.UTF_8)
        val now = SystemClock.elapsedRealtime()
        if (now < testUntil) return
        if (!force && s == lastPayload) return
        if (!force && now - lastSentAt < 1500) return
        lastPayload = s; lastSentAt = now
        ble.send(payload)
        AppState.update { sentAt = System.currentTimeMillis() }
    }

    // ---------------- Thong bao (bat buoc cho dich vu nen) ----------------
    private fun goForeground(text: String) {
        val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CH) == null)
            nm.createNotificationChannel(NotificationChannel(CH, "Trạm sạc gần", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stopPi = PendingIntent.getService(
            this, 1, Intent(this, TramSacService::class.java).setAction(STOP), PendingIntent.FLAG_IMMUTABLE
        )
        val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CH) else @Suppress("DEPRECATION") Notification.Builder(this)
        val n = b.setSmallIcon(R.drawable.ic_notif)
            .setContentTitle("HUD Trạm sạc")
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Dừng", stopPi).build())
            .build()
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(
                    NID, n,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                )
            } else startForeground(NID, n)
        } catch (e: Exception) {
            // Android 14 khong cho dich vu vi tri tu khoi dong lai khi app o nen: dung, cho nguoi dung mo app
            AppState.update { gps = "Mở app để chạy lại" }
            stopSelf()
        }
    }
}
