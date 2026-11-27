package com.evcarhud.tramsac

import android.os.Handler
import android.os.Looper

/** Trang thai chung giua dich vu nen va man hinh (chi doc / ghi tren luong chinh). */
object AppState {
    var running = false
    var gps = "Chưa bật"
    var hud = "Chưa chọn HUD"
    var data = "—"
    var lastFix: Pair<Double, Double>? = null
    var speedKmh = 0
    var ranked: List<Nearby> = emptyList()
    var picked: Nearby? = null
    var sentAt = 0L

    private val main = Handler(Looper.getMainLooper())
    private val listeners = LinkedHashSet<() -> Unit>()

    fun listen(l: () -> Unit) { listeners.add(l) }
    fun unlisten(l: () -> Unit) { listeners.remove(l) }

    /** Goi tu bat ky luong nao: chay doi thay tren luong chinh roi bao man hinh. */
    fun update(block: AppState.() -> Unit) {
        main.post {
            block()
            listeners.toList().forEach { it() }
        }
    }
}
