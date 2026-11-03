package com.evcarhud.tramsac

import android.content.Context

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("tramsac", Context.MODE_PRIVATE)

    var hudMac: String
        get() = sp.getString("hud_mac", "") ?: ""
        set(v) = sp.edit().putString("hud_mac", v).apply()
    var hudName: String
        get() = sp.getString("hud_name", "") ?: ""
        set(v) = sp.edit().putString("hud_name", v).apply()
    var source: String
        get() = sp.getString("source2", Sources.VF_OSM) ?: Sources.VF_OSM
        set(v) = sp.edit().putString("source2", v).apply()
    var ocmKey: String
        get() = sp.getString("ocm_key", "") ?: ""
        set(v) = sp.edit().putString("ocm_key", v.trim()).apply()
    var customUrl: String
        get() = sp.getString("custom_url", "") ?: ""
        set(v) = sp.edit().putString("custom_url", v.trim()).apply()
    /** Ban kinh tim tram (km). */
    var radiusKm: Int
        get() = sp.getInt("radius_km", 15)
        set(v) = sp.edit().putInt("radius_km", v.coerceIn(2, 50)).apply()
}
