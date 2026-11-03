package com.evcarhud.tramsac

import java.util.UUID

/**
 * Giao thuc BLE "Tram sac" giua app va HUD (xem docs/tram-sac-ble.md).
 * Dich vu GATT rieng, khong dung chung voi HLP (Waze Mod) hay GOFA, nen chay song song duoc tren cung ket noi.
 */
object HudProto {
    val SERVICE: UUID = UUID.fromString("4556d001-8c2a-4b7e-9d3f-5a1c0e7b2f10")
    /** Ghi (co / khong phan hoi): 1 ban tin = 1 lan ghi, chuoi UTF-8. */
    val STATION: UUID = UUID.fromString("4556d002-8c2a-4b7e-9d3f-5a1c0e7b2f10")
    /** Doc: phien ban giao thuc phia HUD, vd. "TRAMSAC1". */
    val INFO: UUID = UUID.fromString("4556d003-8c2a-4b7e-9d3f-5a1c0e7b2f10")

    private const val NAME_MAX_BYTES = 60

    /**
     * T1|<ten>|<khoang cach m>|<phia A/R/L/B/?>|<goc lech do, rong neu chua biet>|<kW>|<so cong>|<so tram trong ban kinh>|<che do>
     * che do: D = tram dich nguoi dung chon (HUD chuyen sang man dan duong), N = tram gan nhat tu dong (dai nho).
     * "T1|" (khong co gi them) = khong co tram nao -> HUD xoa o tram sac.
     */
    fun encode(n: Nearby?, count: Int, target: Boolean = false): ByteArray {
        if (n == null) return "T1|".toByteArray()
        val name = cutUtf8(n.st.name.replace('|', '/').replace('\n', ' ').trim(), NAME_MAX_BYTES)
        val s = "T1|$name|${n.distM}|${n.side}|${n.relDeg ?: ""}|${n.st.maxKw}|${n.st.ports}|$count|${if (target) 'D' else 'N'}"
        return s.toByteArray(Charsets.UTF_8)
    }

    /** Cat chuoi cho vua maxBytes UTF-8 ma khong cat ngang mot ky tu. */
    fun cutUtf8(s: String, maxBytes: Int): String {
        if (s.toByteArray(Charsets.UTF_8).size <= maxBytes) return s
        val sb = StringBuilder()
        var used = 0
        var i = 0
        while (i < s.length) {
            val cp = s.codePointAt(i)
            val len = String(Character.toChars(cp)).toByteArray(Charsets.UTF_8).size
            if (used + len > maxBytes) break
            sb.appendCodePoint(cp); used += len; i += Character.charCount(cp)
        }
        return sb.toString().trimEnd()
    }
}
