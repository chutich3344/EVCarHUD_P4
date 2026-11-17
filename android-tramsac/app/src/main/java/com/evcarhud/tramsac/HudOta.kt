package com.evcarhud.tramsac

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.UUID
import kotlin.concurrent.thread

/**
 * Cap nhat firmware HUD qua Bluetooth (HUD 2.8" va cac ban co dich vu "EVCarHUD OTA", xem main/ota.h ben firmware).
 *  1. Noi GATT, doc CTRL: "OTA1|du an|phien ban|MAC chip|model|dung luong khe".
 *  2. Hoi evcarhud.com/api/ota?model=&mac=&v= co ban moi khong (chi HUD da dang ky tren evcarhud.com/account).
 *  3. Nguoi dung dong y: tai file .bin, cat dung phan firmware (file gop nap 0x0 thi bo 64 KB dau), gui qua DATA
 *     (ghi khong phan hoi, kem vi tri), toi da 6 KB truoc "P" cuoi HUD bao; "R|x" / 2 giay khong tien: gui lai.
 *  4. Ghi "E": HUD kiem tra, bao "OK|E|phien ban" roi tu khoi dong lai.
 * Moi callback tra ve tren luong chinh.
 */
@SuppressLint("MissingPermission")
class HudOta(private val ctx: Context, private val ui: Listener) {
    interface Listener {
        fun status(text: String)
        fun info(cur: String, latest: String, notes: String, update: Boolean)
        fun progress(sent: Int, total: Int)
        fun done(ok: Boolean, text: String)
    }

    companion object {
        val SVC: UUID = UUID.fromString("4556d0f0-8c2a-4b7e-9d3f-5a1c0e7b2f10")
        val CTRL: UUID = UUID.fromString("4556d0f1-8c2a-4b7e-9d3f-5a1c0e7b2f10")
        val DATA: UUID = UUID.fromString("4556d0f2-8c2a-4b7e-9d3f-5a1c0e7b2f10")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val API = "https://evcarhud.com/api/ota"
        private const val WINDOW = 6 * 1024
        private const val STALL_MS = 2000L

        /** Do dai anh firmware ESP32 bat dau o off (tieu de 24 byte + cac doan, lam tron 16 + 32 byte SHA neu co). */
        fun appLength(b: ByteArray, off: Int): Int {
            if (off + 24 > b.size || b[off].toInt() and 0xFF != 0xE9) return -1
            val segs = b[off + 1].toInt() and 0xFF
            val hash = b[off + 23].toInt() == 1
            var p = off + 24
            repeat(segs) {
                if (p + 8 > b.size) return -1
                val len = (b[p + 4].toInt() and 0xFF) or ((b[p + 5].toInt() and 0xFF) shl 8) or
                        ((b[p + 6].toInt() and 0xFF) shl 16) or ((b[p + 7].toInt() and 0xFF) shl 24)
                p += 8 + len
            }
            val n = (p - off) / 16 * 16 + 16 + if (hash) 32 else 0
            return if (off + n <= b.size) n else -1
        }

        /** File gop (nap 0x0) co bang phan vung (0xAA50) o 0x8000: firmware o 0x10000. */
        fun appOffset(b: ByteArray): Int =
            if (b.size > 0x10000 && b[0x8000].toInt() and 0xFF == 0xAA && b[0x8001].toInt() and 0xFF == 0x50) 0x10000 else 0
    }

    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var ctrl: BluetoothGattCharacteristic? = null
    private var data: BluetoothGattCharacteristic? = null
    private var mtu = 23
    private var hudModel = ""; private var hudMac = ""; private var hudVer = ""
    private var image: ByteArray? = null
    private var next = 0; private var acked = 0; private var total = 0
    private var writing = false; private var sending = false; private var finished = false
    private var lastProgress = 0L
    private var closed = false

    fun check(address: String) {
        closed = false
        val mgr = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val ad = mgr.adapter
        if (ad == null || !ad.isEnabled) { ui.done(false, "Bật Bluetooth trước nhé."); return }
        val dev = try { ad.getRemoteDevice(address) } catch (e: Exception) { ui.done(false, "Địa chỉ HUD sai."); return }
        ui.status("Đang kết nối HUD…")
        gatt = dev.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
        main.postDelayed({ if (ctrl == null && !closed) fail("Không kết nối được HUD. Bật HUD và để điện thoại gần HUD.") }, 20000)
    }

    fun close() {
        closed = true
        main.removeCallbacksAndMessages(null)
        if (sending && !finished) writeCtrl("A")
        main.postDelayed({ gatt?.let { try { it.disconnect(); it.close() } catch (_: Exception) {} }; gatt = null }, if (sending) 300 else 0)
        sending = false
    }

    private fun fail(msg: String) {
        if (closed) return
        ui.done(false, msg)
        close()
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    if (!g.requestMtu(247)) g.discoverServices()
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED && !finished) {
                    fail(if (sending) "Mất Bluetooth giữa chừng, HUD vẫn chạy bản cũ. Thử lại gần HUD hơn." else "HUD ngắt kết nối.")
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, m: Int, status: Int) {
            main.post { if (g == gatt) { mtu = m; g.discoverServices() } }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (g != gatt) return@post
                val s = g.getService(SVC)
                ctrl = s?.getCharacteristic(CTRL); data = s?.getCharacteristic(DATA)
                if (ctrl == null || data == null) {
                    fail("Firmware HUD này chưa hỗ trợ cập nhật qua app. Nạp bản mới qua USB (trang evcarhud.com/account) một lần.")
                    return@post
                }
                ui.status("Đang đọc phiên bản HUD…")
                g.readCharacteristic(ctrl)
            }
        }

        @Deprecated("API < 33")
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            @Suppress("DEPRECATION") val v = c.value ?: ByteArray(0)
            onRead(g, v)
        }

        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, status: Int) = onRead(g, value)

        private fun onRead(g: BluetoothGatt, v: ByteArray) {
            val p = String(v, Charsets.UTF_8).split('|')
            main.post {
                if (g != gatt) return@post
                if (p.size < 5 || p[0] != "OTA1") { fail("HUD trả thông tin lạ."); return@post }
                hudVer = p[2]; hudMac = p[3]; hudModel = p[4]
                // bat thong bao tren CTRL truoc khi gui
                g.setCharacteristicNotification(ctrl, true)
                val d = ctrl!!.getDescriptor(CCCD)
                if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                else { @Suppress("DEPRECATION") d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; @Suppress("DEPRECATION") g.writeDescriptor(d) }
                askServer()
            }
        }

        @Deprecated("API < 33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") val v = c.value ?: return
            onNotify(String(v, Charsets.UTF_8))
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) =
            onNotify(String(value, Charsets.UTF_8))

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post { if (g == gatt) { writing = false; pump() } }
        }
    }

    // ---------------- may chu ----------------
    private fun askServer() {
        ui.status("Đang kiểm tra bản mới trên máy chủ…")
        val url = "$API?model=${enc(hudModel)}&mac=${enc(hudMac)}&v=${enc(hudVer)}"
        thread {
            val r = try { get(url) } catch (e: Exception) { Pair(-1, e.message ?: "") }
            main.post {
                if (closed) return@post
                val j = try { JSONObject(r.second) } catch (_: Exception) { null }
                if (r.first != 200 || j == null) {
                    fail(j?.optString("error")?.takeIf { it.isNotBlank() } ?: "Không kết nối được máy chủ (${r.first}).")
                    return@post
                }
                ui.info(hudVer, j.optString("version"), j.optString("notes"), j.optBoolean("update", j.optString("version") != hudVer))
            }
        }
    }

    /** Nguoi dung bam Cap nhat: tai file roi gui. */
    fun install() {
        ui.status("Đang tải firmware từ máy chủ…")
        val url = "$API?model=${enc(hudModel)}&mac=${enc(hudMac)}&v=${enc(hudVer)}&download=1"
        thread {
            val bin = try { download(url) } catch (e: Exception) { null }
            main.post {
                if (closed) return@post
                if (bin == null) { fail("Tải firmware lỗi, kiểm tra mạng điện thoại."); return@post }
                val off = appOffset(bin)
                val len = appLength(bin, off)
                if (len <= 0) { fail("File firmware trên máy chủ không hợp lệ."); return@post }
                image = bin.copyOfRange(off, off + len)
                total = len; next = 0; acked = 0
                sending = true
                ui.status("Đang gửi xuống HUD…")
                writeCtrl("B|$total|0")
            }
        }
    }

    private fun onNotify(m: String) {
        main.post {
            if (closed) return@post
            when {
                m == "OK|B" -> { lastProgress = System.currentTimeMillis(); pump(); watchdog() }
                m.startsWith("P|") -> {
                    val v = m.substring(2).toIntOrNull() ?: return@post
                    if (v > acked) { acked = v; lastProgress = System.currentTimeMillis(); ui.progress(acked, total) }
                    if (acked >= total && !finished) { finished = true; ui.status("HUD đang kiểm tra firmware…"); writeCtrl("E") }
                    else pump()
                }
                m.startsWith("R|") -> { m.substring(2).toIntOrNull()?.let { next = it.coerceAtMost(total) }; pump() }
                m.startsWith("OK|E|") -> {
                    sending = false
                    ui.done(true, "Đã cài bản ${m.substring(5)}. HUD đang khởi động lại.")
                    close()
                }
                m.startsWith("ER|") -> fail("HUD báo lỗi: ${m.substring(3)}. HUD vẫn chạy bản cũ.")
            }
        }
    }

    private fun watchdog() {
        if (!sending || finished || closed) return
        if (System.currentTimeMillis() - lastProgress > STALL_MS && next > acked) { next = acked; pump() }
        main.postDelayed({ watchdog() }, 500)
    }

    /** Gui goi DATA tiep theo neu con trong cua so. */
    private fun pump() {
        val g = gatt ?: return
        val c = data ?: return
        val img = image ?: return
        if (!sending || finished || writing || next >= total || next - acked >= WINDOW) return
        val chunk = minOf(mtu - 3 - 4, 240, total - next)
        val pkt = ByteArray(4 + chunk)
        pkt[0] = next.toByte(); pkt[1] = (next shr 8).toByte(); pkt[2] = (next shr 16).toByte(); pkt[3] = (next shr 24).toByte()
        System.arraycopy(img, next, pkt, 4, chunk)
        writing = true
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, pkt, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) == 0
        } else {
            @Suppress("DEPRECATION") c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION") c.value = pkt
            @Suppress("DEPRECATION") g.writeCharacteristic(c)
        }
        if (ok) next += chunk
        else { writing = false; main.postDelayed({ pump() }, 20) }
    }

    private fun writeCtrl(s: String) {
        val g = gatt ?: return
        val c = ctrl ?: return
        val v = s.toByteArray(Charsets.UTF_8)
        fun go(left: Int) {
            val ok = if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(c, v, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
            } else {
                @Suppress("DEPRECATION") c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION") c.value = v
                @Suppress("DEPRECATION") g.writeCharacteristic(c)
            }
            if (ok) writing = true else if (left > 0) main.postDelayed({ go(left - 1) }, 100)
        }
        go(20)
    }

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    private fun get(url: String): Pair<Int, String> {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 10000; c.readTimeout = 15000
        return try {
            val code = c.responseCode
            val body = (if (code in 200..299) c.inputStream else c.errorStream)?.bufferedReader()?.readText() ?: ""
            Pair(code, body)
        } finally { c.disconnect() }
    }

    private fun download(url: String): ByteArray? {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 10000; c.readTimeout = 30000
        return try { if (c.responseCode == 200) c.inputStream.readBytes() else null } finally { c.disconnect() }
    }
}
