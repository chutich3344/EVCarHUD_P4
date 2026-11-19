package com.evcarhud.adas

import android.media.AudioManager
import android.media.ToneGenerator
import android.os.SystemClock

/**
 * Tieng "ting" canh bao tren dien thoai (di qua loa xe neu dang noi Bluetooth).
 * Chong keu inh oi pho dong: muc 1 (gan xe truoc) im; muc 2 tit doi, toi da 1 lan/3 s;
 * muc 3 (PHANH) tit nhanh lien tuc; lech lan (>= 50 km/h) 1 tieng khi bat dau, nghi 2 s.
 */
class Beeper {
    private var tg: ToneGenerator? = try { ToneGenerator(AudioManager.STREAM_MUSIC, 90) } catch (_: Exception) { null }
    private var lastL2 = 0L
    private var lastL3 = 0L
    private var lastLdw = 0L
    private var prevLdw = '-'
    private var prevLevel = 0

    fun update(level: Int, ldw: Char, on: Boolean) {
        val t = SystemClock.elapsedRealtime()
        if (!on) { prevLevel = level; prevLdw = ldw; return }
        when {
            level >= 3 -> if (t - lastL3 >= 300) { play(ToneGenerator.TONE_PROP_BEEP, 180); lastL3 = t }
            level == 2 -> if (prevLevel < 2 && t - lastL2 >= 3000) { play(ToneGenerator.TONE_PROP_BEEP2, 400); lastL2 = t }
            ldw != '-' -> if (prevLdw == '-' && t - lastLdw >= 2000) { play(ToneGenerator.TONE_PROP_ACK, 250); lastLdw = t }
        }
        prevLevel = level; prevLdw = ldw
    }

    fun test() = play(ToneGenerator.TONE_PROP_BEEP2, 400)

    private fun play(tone: Int, ms: Int) { try { tg?.startTone(tone, ms) } catch (_: Exception) {} }

    fun release() { try { tg?.release() } catch (_: Exception) {}; tg = null }
}
