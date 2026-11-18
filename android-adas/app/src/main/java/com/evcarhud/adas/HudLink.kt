package com.evcarhud.adas

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.UUID

/**
 * Ket noi BLE toi HUD va ghi goi A1 vao dac tinh 4556d005 (dich vu 4556d001, chung voi app Tram sac).
 * Waze/GOFA tren cung dien thoai dang noi HUD thi Android dung chung duong ket noi do.
 * Tu tim HUD: thiet bi dang noi co ten EVHUD/HUD_, khong co thi quet 10 giay, luu dia chi.
 * Moi ham goi tren luong chinh.
 */
@SuppressLint("MissingPermission")
class HudLink(private val ctx: Context, private val onState: (String, Boolean) -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val mgr = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private var gatt: BluetoothGatt? = null
    private var chr: BluetoothGattCharacteristic? = null
    private var busy = false
    private var pending: ByteArray? = null
    private var navPending: ByteArray? = null
    private var wid = 0
    private var stopped = true
    private var mac = ""
    private var fails = 0
    private var scanning = false
    var onMac: (String) -> Unit = {}

    val ready get() = chr != null

    fun start(address: String) {
        stop()
        stopped = false
        mac = address
        fails = 0
        if (mac.isBlank()) find() else connect()
    }

    fun stop() {
        stopped = true
        main.removeCallbacksAndMessages(null)
        stopScan()
        chr = null; busy = false; pending = null; navPending = null
        gatt?.let { try { it.disconnect(); it.close() } catch (_: Exception) {} }
        gatt = null
    }

    /** Goi dan duong D1 (thua, ~1 lan/giay): ghi co phan hoi (goi dai > MTU van toi du), uu tien hon goi A1. */
    fun sendNav(data: ByteArray) {
        navPending = data
        if (!busy) flush()
    }

    /** Ghi goi moi nhat; dang ghi thi giu goi moi nhat de ghi sau (khong don hang doi). */
    fun send(data: ByteArray) = write(data, false)

    private fun write(data: ByteArray, nav: Boolean) {
        val c = chr; val g = gatt
        if (c == null || g == null || busy) { if (nav) navPending = data else pending = data; return }
        busy = true
        val noRsp = !nav && c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0
        val type = if (noRsp) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, data, type) == 0
        } else {
            @Suppress("DEPRECATION") run { c.writeType = type; c.value = data; g.writeCharacteristic(c) }
        }
        if (!ok) { busy = false; if (nav) navPending = data else pending = data; main.postDelayed({ flush() }, 300); return }
        // Ghi khong phan hoi: vai may khong goi onCharacteristicWrite -> tu mo khoa
        if (noRsp) main.postDelayed({ if (busy) { busy = false; flush() } }, 80)
        else { val w = ++wid; main.postDelayed({ if (busy && w == wid) { busy = false; flush() } }, 1500) }   // mat phan hoi: khong treo
    }

    private fun flush() {
        navPending?.let { navPending = null; write(it, true); return }
        val p = pending ?: return
        pending = null
        write(p, false)
    }

    private fun isHud(name: String?) = name != null && (name.startsWith("EVHUD") || name.startsWith("HUD_") || name.startsWith("EVCarHUD"))

    private fun find() {
        if (stopped) return
        val ad = mgr.adapter
        if (ad == null || !ad.isEnabled) { onState("Bluetooth đang tắt", false); main.postDelayed({ find() }, 5000); return }
        // HUD dang noi voi Waze/GOFA tren dien thoai nay
        val shared = try { mgr.getConnectedDevices(BluetoothProfile.GATT).firstOrNull { isHud(it.name) } } catch (_: Exception) { null }
        if (shared != null) { useMac(shared.address); return }
        val sc = ad.bluetoothLeScanner ?: run { main.postDelayed({ find() }, 5000); return }
        onState("Đang tìm HUD…", false)
        scanning = true
        try {
            sc.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCb)
        } catch (e: Exception) { scanning = false; onState("Không quét được Bluetooth", false) }
        main.postDelayed({
            if (scanning) { stopScan(); onState("Chưa thấy HUD, thử lại sau", false); main.postDelayed({ find() }, 20000) }
        }, 10000)
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try { mgr.adapter?.bluetoothLeScanner?.stopScan(scanCb) } catch (_: Exception) {}
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(type: Int, r: ScanResult) {
            val n = r.scanRecord?.deviceName ?: r.device.name
            if (!isHud(n)) return
            main.post { if (scanning) { stopScan(); useMac(r.device.address) } }
        }
    }

    private fun useMac(a: String) {
        mac = a
        onMac(a)
        connect()
    }

    private fun connect() {
        if (stopped) return
        val ad = mgr.adapter
        if (ad == null || !ad.isEnabled) { onState("Bluetooth đang tắt", false); main.postDelayed({ connect() }, 5000); return }
        val dev: BluetoothDevice = try { ad.getRemoteDevice(mac) } catch (e: Exception) { mac = ""; find(); return }
        val shared = mgr.getConnectedDevices(BluetoothProfile.GATT).any { it.address.equals(mac, true) }
        onState(if (shared) "Mở kênh (chung kết nối Waze/GOFA)…" else "Đang nối HUD…", false)
        gatt = dev.connectGatt(ctx, !shared && fails >= 3, cb, BluetoothDevice.TRANSPORT_LE)
    }

    private fun drop(msg: String, ms: Long) {
        chr = null; busy = false; fails++
        gatt?.let { try { it.close() } catch (_: Exception) {} }
        gatt = null
        onState(msg, false)
        if (!stopped) main.postDelayed({ connect() }, ms)
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    try { g.javaClass.getMethod("refresh").invoke(g) } catch (_: Exception) {}
                    main.postDelayed({ if (g == gatt) g.discoverServices() }, 300)
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) drop("HUD mất kết nối, thử lại…", 3000)
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (g != gatt) return@post
                val c = g.getService(SERVICE)?.getCharacteristic(ADAS)
                if (c == null) { drop("HUD chưa có kênh Làn (cần firmware mới)", 60000); return@post }
                chr = c; fails = 0
                onState("Đã nối HUD", true)
                flush()
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post { if (g == gatt) { busy = false; flush() } }
        }
    }

    companion object {
        val SERVICE: UUID = UUID.fromString("4556d001-8c2a-4b7e-9d3f-5a1c0e7b2f10")
        val ADAS: UUID = UUID.fromString("4556d005-8c2a-4b7e-9d3f-5a1c0e7b2f10")
    }
}
