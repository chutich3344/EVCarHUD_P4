package com.evcarhud.adas

import android.os.Bundle
import android.os.SystemClock

/**
 * Dan duong dang chay trong Waze Mod (doc qua WazeMod Data API): huong re (ma HLP), khoang cach, ten duong ke tiep, con lai, gio den.
 * Waze khong gui hinh tuyen -> ban do PiP ve 1 doan xanh theo duong OSM dang di toi diem re (stubLat/stubLon) + mui ten re o cuoi (PA3).
 */
object WazeNav {
    @Volatile var turn = 0; private set
    @Volatile var distM = -1; private set
    @Volatile var exit = 0; private set
    @Volatile var street = ""; private set
    @Volatile var remainM = -1; private set
    @Volatile var etaMin = -1; private set
    @Volatile var etaClock = ""; private set
    @Volatile var stubLat: DoubleArray? = null; private set
    @Volatile var stubLon: DoubleArray? = null; private set
    @Volatile private var at = 0L
    val listeners = ArrayList<() -> Unit>()

    /** Dang dan duong va du lieu con moi (Waze im > 10 s thi coi nhu thoi). */
    val active get() = turn > 0 && distM >= 0 && SystemClock.elapsedRealtime() - at < 10_000

    fun update(b: Bundle?, ahead: (Int) -> Pair<DoubleArray, DoubleArray>?) {
        val was = active
        val nav = b != null && (b.get("navigating") as? Boolean ?: (b.getInt("turn", 0) > 0))
        if (b == null || !nav) {
            turn = 0; distM = -1; stubLat = null; stubLon = null
            if (was) changed(); return
        }
        turn = b.getInt("turn", 0)
        distM = b.getInt("turn_distance_m", -1)
        exit = b.getInt("roundabout_exit", 0)
        street = b.get("next_street")?.toString() ?: ""
        remainM = b.getInt("remaining_m", -1)
        etaMin = b.getInt("eta_minutes", -1)
        etaClock = b.get("eta_clock")?.toString() ?: ""
        at = SystemClock.elapsedRealtime()
        // doan xanh toi diem re, toi da 1,5 km (xa hon thi ngoai khung ban do)
        val st = if (distM > 0) ahead(minOf(distM, 1500)) else null
        stubLat = st?.first; stubLon = st?.second
        changed()
    }

    private fun changed() { listeners.toList().forEach { it() } }
}
