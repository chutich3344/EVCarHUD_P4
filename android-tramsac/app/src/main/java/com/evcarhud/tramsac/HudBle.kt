package com.evcarhud.tramsac

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper

/**
 * Ket noi GATT toi HUD. HUD chi nhan 1 dien thoai, nen khi Waze Mod / GOFA tren cung dien thoai dang noi,
 * Android dung chung duong ket noi do: app nay chi mo them mot GATT client va ghi vao dich vu Tram sac.
 * Moi ham goi tren luong chinh.
 */
@SuppressLint("MissingPermission")   // quyen BLUETOOTH_CONNECT duoc kiem tra truoc khi khoi dong dich vu
class HudBle(private val ctx: Context, private val onState: (String) -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val mgr = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private var gatt: BluetoothGatt? = null
    private var chr: BluetoothGattCharacteristic? = null
    private var mac = ""
    private var busy = false
    private var pending: ByteArray? = null
    private var posChr: BluetoothGattCharacteristic? = null     // firmware cu khong co -> bo qua vi tri
    private var posPending: ByteArray? = null
    private var stopped = true
    private var refreshed = false

    val ready get() = chr != null

    fun start(address: String) {
        stop()
        mac = address
        stopped = false
        if (mac.isBlank()) { onState("Chưa chọn HUD"); return }
        connect()
    }

    fun stop() {
        stopped = true
        main.removeCallbacksAndMessages(null)
        chr = null; posChr = null; busy = false; pending = null; posPending = null
        gatt?.let { try { it.disconnect(); it.close() } catch (_: Exception) {} }
        gatt = null
    }

    /** HUD co nhan vi tri (thoi tiet theo GPS) khong. */
    val hasPos get() = posChr != null

    /** Gui vi tri xe cho thoi tiet; ghi sau ban tin tram neu dang ban. */
    fun sendPos(data: ByteArray) {
        val c = posChr
        val g = gatt
        if (c == null || g == null || busy) { posPending = data; return }
        write(g, c, data) { posPending = data }
    }

    /** Gui ban tin moi nhat; neu dang ghi thi giu lai ban moi nhat de ghi sau. */
    fun send(data: ByteArray) {
        val c = chr
        val g = gatt
        if (c == null || g == null) { pending = data; return }   // gui ngay khi HUD san sang
        if (busy) { pending = data; return }
        write(g, c, data) { pending = data }
    }

    private fun write(g: BluetoothGatt, c: BluetoothGattCharacteristic, data: ByteArray, keep: () -> Unit) {
        busy = true
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
        } else {
            @Suppress("DEPRECATION")
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            c.value = data
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        if (!ok) { busy = false; keep(); main.postDelayed({ flush() }, 500) }
    }

    private fun flush() {
        val p = pending
        if (p != null) { pending = null; send(p); return }
        val q = posPending ?: return
        if (posChr == null) return
        posPending = null
        sendPos(q)
    }

    private fun connect() {
        if (stopped) return
        val adapter = mgr.adapter
        if (adapter == null || !adapter.isEnabled) {
            onState("Bluetooth đang tắt"); retry(5000); return
        }
        val dev: BluetoothDevice = try { adapter.getRemoteDevice(mac) } catch (e: Exception) {
            onState("Địa chỉ HUD sai"); return
        }
        val shared = mgr.getConnectedDevices(BluetoothProfile.GATT).any { it.address.equals(mac, true) }
        onState(if (shared) "Đang mở kênh (dùng chung kết nối Waze/GOFA)…" else "Đang kết nối HUD…")
        // autoConnect = true: tu noi lai khi HUD bat len / vao vung song, khong can quet
        gatt = dev.connectGatt(ctx, !shared, cb, BluetoothDevice.TRANSPORT_LE)
    }

    private fun retry(ms: Long) {
        if (stopped) return
        main.postDelayed({ connect() }, ms)
    }

    private fun dropAndRetry(msg: String, ms: Long) {
        chr = null; posChr = null; busy = false
        gatt?.let { try { it.close() } catch (_: Exception) {} }
        gatt = null
        onState(msg)
        retry(ms)
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    onState("Đã nối, đang đọc dịch vụ…")
                    if (!g.requestMtu(247)) g.discoverServices()
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    dropAndRetry("HUD mất kết nối, đang thử lại…", 3000)
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            main.post { if (g == gatt) g.discoverServices() }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (g != gatt) return@post
                val c = g.getService(HudProto.SERVICE)?.getCharacteristic(HudProto.STATION)
                if (c == null && !refreshed) {
                    // Android co the con nho danh sach dich vu cua firmware cu: xoa bo nho dem roi doc lai 1 lan
                    refreshed = true
                    try { g.javaClass.getMethod("refresh").invoke(g) } catch (_: Exception) {}
                    main.postDelayed({ if (g == gatt) g.discoverServices() }, 600)
                    return@post
                }
                if (c == null) {
                    refreshed = false
                    // HUD chua co dich vu tram sac (firmware cu): thu lai cham, khong lam phien ket noi Waze/GOFA
                    dropAndRetry("HUD chưa có dịch vụ Trạm sạc (cần firmware mới)", 60000)
                    return@post
                }
                chr = c
                posChr = c.service.getCharacteristic(HudProto.POS)
                refreshed = false
                onState("Đã kết nối HUD")
                flush()
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post {
                if (g != gatt) return@post
                busy = false
                flush()
            }
        }
    }
}
