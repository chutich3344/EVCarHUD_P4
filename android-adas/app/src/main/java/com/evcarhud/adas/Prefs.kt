package com.evcarhud.adas

import android.content.Context

/** Cai dat luu tren may. */
class Prefs(ctx: Context) {
    private val p = ctx.getSharedPreferences("adas", Context.MODE_PRIVATE)

    var calibrated: Boolean
        get() = p.getBoolean("calib", false)
        set(v) = p.edit().putBoolean("calib", v).apply()
    /** Diem tu (chỗ 2 vach lan gap nhau) theo ti le anh 0..1. */
    var vpX: Float
        get() = p.getFloat("vpx", 0.5f)
        set(v) = p.edit().putFloat("vpx", v).apply()
    var vpY: Float
        get() = p.getFloat("vpy", 0.5f)
        set(v) = p.edit().putFloat("vpy", v).apply()
    /** Chieu cao camera so voi mat duong (m). */
    var camH: Float
        get() = p.getFloat("camh", 1.2f)
        set(v) = p.edit().putFloat("camh", v).apply()
    var accel: String
        get() = p.getString("accel", "npu") ?: "npu"
        set(v) = p.edit().putString("accel", v).apply()
    var hudMac: String
        get() = p.getString("mac", "") ?: ""
        set(v) = p.edit().putString("mac", v).apply()
    var showMap: Boolean
        get() = p.getBoolean("map", true)
        set(v) = p.edit().putBoolean("map", v).apply()
    var showTemp: Boolean
        get() = p.getBoolean("temp", true)
        set(v) = p.edit().putBoolean("temp", v).apply()
    /** Goc dat cua so: 0 tren trai, 1 tren phai, 2 duoi trai, 3 duoi phai. */
    var tempCorner: Int
        get() = p.getInt("tc", 2)
        set(v) = p.edit().putInt("tc", v).apply()
    var mapCorner: Int
        get() = p.getInt("mc", 3)
        set(v) = p.edit().putInt("mc", v).apply()
}
