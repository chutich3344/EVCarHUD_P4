package com.evcarhud.tramsac

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** Mot tram sac lay tu nguon du lieu. */
data class Station(
    val name: String,
    val lat: Double,
    val lon: Double,
    val operator: String = "",
    val maxKw: Int = 0,      // cong suat lon nhat (kW), 0 = khong ro
    val ports: Int = 0,      // so cong / tru sac, 0 = khong ro
)

/** Trạm đã tính khoảng cách và hướng so với xe. */
data class Nearby(
    val st: Station,
    val distM: Int,
    val relDeg: Int?,        // goc lech so voi huong xe chay (-180..180, duong = ben phai); null = chua biet huong
) {
    /** A = thang phia truoc, R = phai, L = trai, B = phia sau, ? = chua biet huong xe. */
    val side: Char
        get() {
            val r = relDeg ?: return '?'
            return when {
                abs(r) <= 20 -> 'A'
                abs(r) >= 110 -> 'B'
                r > 0 -> 'R'
                else -> 'L'
            }
        }
}

object Geo {
    private const val R = 6371000.0

    fun distM(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val p1 = Math.toRadians(lat1); val p2 = Math.toRadians(lat2)
        val dp = p2 - p1; val dl = Math.toRadians(lon2 - lon1)
        val a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2)
        return 2 * R * atan2(sqrt(a), sqrt(1 - a))
    }

    /** Huong tu diem 1 toi diem 2, do (0 = Bac, 90 = Dong). */
    fun bearing(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val p1 = Math.toRadians(lat1); val p2 = Math.toRadians(lat2)
        val dl = Math.toRadians(lon2 - lon1)
        val y = sin(dl) * cos(p2)
        val x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)
        return (Math.toDegrees(atan2(y, x)) + 360) % 360
    }

    /** Chuan hoa goc ve -180..180. */
    fun wrap(d: Double): Double {
        var x = d % 360
        if (x > 180) x -= 360
        if (x <= -180) x += 360
        return x
    }

    /**
     * Sap xep tram theo khoang cach. heading = huong xe (do) khi dang chay, null khi dung yen / chua biet.
     */
    fun rank(lat: Double, lon: Double, heading: Double?, list: List<Station>): List<Nearby> =
        list.map { s ->
            val d = distM(lat, lon, s.lat, s.lon)
            val rel = heading?.let { wrap(bearing(lat, lon, s.lat, s.lon) - it).roundToInt() }
            Nearby(s, d.roundToInt(), rel)
        }.sortedBy { it.distM }

    /**
     * Chon tram de hien len HUD: khi dang chay uu tien tram phia truoc / hai ben (khong lay tram da vuot qua),
     * tru khi tram phia sau rat gan (< 300 m) ma tram phia truoc xa hon nhieu.
     */
    fun pick(ranked: List<Nearby>): Nearby? {
        if (ranked.isEmpty()) return null
        val first = ranked[0]
        if (first.relDeg == null) return first
        val ahead = ranked.firstOrNull { it.side != 'B' } ?: return first
        return if (first.side == 'B' && first.distM < 300 && ahead.distM > first.distM * 4) first else ahead
    }

    fun fmtDist(m: Int): String = when {
        m < 1000 -> "$m m"
        m < 10000 -> String.format("%.1f km", m / 1000.0)
        else -> "${(m / 1000.0).roundToInt()} km"
    }

    fun sideText(c: Char): String = when (c) {
        'A' -> "phía trước"
        'R' -> "bên phải"
        'L' -> "bên trái"
        'B' -> "phía sau"
        else -> "—"
    }
}
