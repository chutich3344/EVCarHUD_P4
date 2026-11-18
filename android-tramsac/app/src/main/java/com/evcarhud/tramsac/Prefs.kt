package com.evcarhud.tramsac

import android.content.Context

class Prefs(ctx: Context) {
    companion object {
        /** API tram sac cong dong tren evcarhud.com (Cloudflare). */
        const val DEFAULT_URL = "https://evcarhud.com/api/stations?vehicle=car"
        const val SUBMIT_URL = "https://evcarhud.com/tram-sac#/gui"
    }

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
        get() = (sp.getString("custom_url", "") ?: "").ifBlank { DEFAULT_URL }
        set(v) = sp.edit().putString("custom_url", v.trim()).apply()
    /** Tram dich chon tu web (nut "Gui len HUD"); null = tu chon tram gan nhat. */
    var target: Station?
        get() {
            val s = sp.getString("target", "") ?: ""
            val p = s.split('\u0001')
            if (p.size < 5) return null
            val lat = p[1].toDoubleOrNull() ?: return null
            val lon = p[2].toDoubleOrNull() ?: return null
            return Station(p[0], lat, lon, "", p[3].toIntOrNull() ?: 0, p[4].toIntOrNull() ?: 0)
        }
        set(v) = sp.edit().putString(
            "target", v?.let { listOf(it.name, it.lat, it.lon, it.maxKw, it.ports).joinToString("\u0001") } ?: ""
        ).apply()

    /** Ban kinh tim tram (km). */
    var radiusKm: Int
        get() = sp.getInt("radius_km2", 3)
        set(v) = sp.edit().putInt("radius_km2", v.coerceIn(1, 50)).apply()
}
