package com.evcarhud.adas

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Binder
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Parcel

/**
 * Doc du lieu truc tiep tu Waze Mod (WazeMod Data API v1, tim thay trong APK Waze Mod 10 ngay 19/11/2026):
 * dich vu com.waze/.gw.WazeModDataService, action com.wazemod.api.action.BIND, giao dien Binder "com.wazemod.api.IWazeModData":
 *   2 = phien ban API (int), 3 = anh chup (Bundle), 4 = dang ky listener (IBinder, int), 5 = huy listener.
 * Listener "com.wazemod.api.IWazeModListener": 2 = cap nhat (Bundle, toi da 5 lan/giay khi doi), 3 = bi thu hoi quyen.
 * Quyen: nguoi dung bam "Cho phep" o man com.wazemod.api.action.REQUEST_ACCESS (startActivityForResult), Waze Mod nho theo goi + chu ky.
 * Bundle: speed, speed_limit, overspeed, current_street, turn, turn_distance_m, alerts[] (type = ma canh bao HLP, distance_m, value)...
 * Khong co vi tri GPS.
 */
class WazeModLink(private val ctx: Context, private val log: (String) -> Unit, private val onData: (Bundle?) -> Unit) {
    @Volatile var status = "chưa nối"; private set
    @Volatile var ok = false; private set
    private val main = Handler(Looper.getMainLooper())
    private var svc: IBinder? = null
    private var bound = false
    private var stopped = false

    private val listener = object : Binder() {
        override fun onTransact(code: Int, data: Parcel, reply: Parcel?, flags: Int): Boolean {
            when (code) {
                ON_UPDATE -> {
                    data.enforceInterface(LISTENER)
                    val b = if (data.readInt() != 0) Bundle.CREATOR.createFromParcel(data) else null
                    main.post { onData(b) }
                    return true
                }
                ON_REVOKED -> {
                    try { data.enforceInterface(LISTENER) } catch (_: Exception) {}
                    main.post { set("Waze Mod đã thu hồi quyền", false); onData(null) }
                    return true
                }
                IBinder.INTERFACE_TRANSACTION -> { reply?.writeString(LISTENER); return true }
            }
            return super.onTransact(code, data, reply, flags)
        }
    }

    private val conn = object : ServiceConnection {
        override fun onServiceConnected(n: ComponentName?, b: IBinder?) {
            svc = b
            Thread { register() }.start()
        }
        override fun onServiceDisconnected(n: ComponentName?) {
            svc = null; set("Waze Mod đã đóng", false); main.post { onData(null) }
        }
    }

    private fun set(s: String, k: Boolean) {
        if (s != status) log("WAZEMOD $s")
        status = s; ok = k
    }

    fun start() {
        stopped = false
        if (bound) return
        // phan biet: chua cai Waze / Waze cai khong phai ban Mod co cong / he thong (ColorOS...) chan mo lien ket
        val ver = try { ctx.packageManager.getPackageInfo(PKG, 0).versionName ?: "?" } catch (_: Exception) { null }
        val has = try { ctx.packageManager.queryIntentServices(Intent(ACTION_BIND).setPackage(PKG), 0).isNotEmpty() } catch (_: Exception) { false }
        var err = ""
        bound = try {
            ctx.bindService(Intent(ACTION_BIND).setPackage(PKG), conn, Context.BIND_AUTO_CREATE)
        } catch (e: Exception) { err = e.javaClass.simpleName; false }
        if (!bound) set(when {
            ver == null -> "chưa cài Waze"
            !has -> "Waze $ver đang cài không có cổng dữ liệu (cần Waze Mod 10)"
            else -> "máy chặn mở Waze $ver từ app khác${if (err.isNotEmpty()) " ($err)" else ""} · mở Waze trước rồi thử lại"
        }, false)
    }

    /** Sau khi nguoi dung cho phep: dang ky lai. */
    fun retry() {
        if (svc != null) Thread { register() }.start() else { stop(); start() }
    }

    private fun register() {
        val b = svc ?: return
        val d = Parcel.obtain(); val r = Parcel.obtain()
        try {
            d.writeInterfaceToken(DESCRIPTOR)
            d.writeStrongBinder(listener)
            d.writeInt(0)
            b.transact(REGISTER, d, r, 0)
            r.readException()
            set("đã nối, đang nhận dữ liệu", true)
        } catch (e: SecurityException) {
            set("chưa cho phép · bấm để xin quyền", false)
        } catch (e: Exception) {
            set("lỗi ${e.javaClass.simpleName}", false)
        } finally { d.recycle(); r.recycle() }
    }

    fun stop() {
        stopped = true
        val b = svc
        if (b != null) Thread {
            val d = Parcel.obtain()
            try { d.writeInterfaceToken(DESCRIPTOR); d.writeStrongBinder(listener); b.transact(UNREGISTER, d, null, IBinder.FLAG_ONEWAY) } catch (_: Exception) {} finally { d.recycle() }
        }.start()
        if (bound) try { ctx.unbindService(conn) } catch (_: Exception) {}
        bound = false; svc = null; ok = false
    }

    companion object {
        const val PKG = "com.waze"
        const val ACTION_BIND = "com.wazemod.api.action.BIND"
        const val ACTION_REQUEST = "com.wazemod.api.action.REQUEST_ACCESS"
        private const val DESCRIPTOR = "com.wazemod.api.IWazeModData"
        private const val LISTENER = "com.wazemod.api.IWazeModListener"
        private const val REGISTER = 4
        private const val UNREGISTER = 5
        private const val ON_UPDATE = 2
        private const val ON_REVOKED = 3
    }
}
