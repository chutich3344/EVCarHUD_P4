package com.evcarhud.tramsac

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var prefs: Prefs
    private val green = Color.parseColor("#1E9E5A")

    private lateinit var tName: TextView
    private lateinit var tDist: TextView
    private lateinit var tInfo: TextView
    private lateinit var tGps: TextView
    private lateinit var tHud: TextView
    private lateinit var tData: TextView
    private lateinit var tList: TextView
    private lateinit var bRun: Button
    private lateinit var bHud: Button
    private lateinit var eKey: EditText
    private lateinit var eUrl: EditText
    private lateinit var eRadius: EditText

    private val refresh: () -> Unit = { render() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        setContentView(build())
    }

    override fun onResume() {
        super.onResume()
        AppState.listen(refresh)
        render()
    }

    override fun onPause() {
        saveFields()
        AppState.unlisten(refresh)
        super.onPause()
    }

    // ---------------- Giao dien (dung bang code, khong can thu vien) ----------------
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun text(size: Float, bold: Boolean = false, color: Int = Color.WHITE) = TextView(this).apply {
        textSize = size
        setTextColor(color)
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    private fun label(s: String) = text(13f, true, Color.parseColor("#9AA5B1")).apply {
        this.text = s.uppercase(); setPadding(0, dp(18), 0, dp(6))
    }

    private fun build(): View {
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(32))
        }

        // The tram dang chon
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(16))
            background = GradientDrawable().apply { setColor(Color.parseColor("#14351F")); cornerRadius = dp(14).toFloat() }
        }
        card.addView(text(13f, true, Color.parseColor("#7FD8A4")).apply { text = "TRẠM SẠC GẦN NHẤT" })
        tName = text(22f, true).apply { setPadding(0, dp(6), 0, 0) }
        tDist = text(34f, true, Color.parseColor("#4CE08A"))
        tInfo = text(14f, color = Color.parseColor("#C9D3DC"))
        card.addView(tName); card.addView(tDist); card.addView(tInfo)
        col.addView(card)

        bRun = Button(this).apply { setOnClickListener { toggleRun() } }
        col.addView(bRun, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })

        col.addView(label("Trạng thái"))
        tGps = text(15f); tHud = text(15f); tData = text(15f)
        col.addView(tGps); col.addView(tHud); col.addView(tData)

        col.addView(label("HUD"))
        bHud = Button(this).apply { setOnClickListener { pickHud() } }
        col.addView(bHud)
        col.addView(Button(this).apply {
            text = "Gửi thử 1 trạm giả lên HUD"
            setOnClickListener { sendTest() }
        })

        col.addView(label("Nguồn dữ liệu trạm sạc"))
        val rg = RadioGroup(this)
        val opts = listOf(
            Sources.OSM to "OpenStreetMap (miễn phí, không cần khóa)",
            Sources.OCM to "OpenChargeMap (cần khóa API miễn phí)",
            Sources.CUSTOM to "Máy chủ riêng (vd. máy chủ trung gian GOFA)",
        )
        opts.forEachIndexed { i, (k, name) ->
            rg.addView(RadioButton(this).apply {
                id = 1000 + i; text = name; isChecked = prefs.source == k
            })
        }
        rg.setOnCheckedChangeListener { _, id -> prefs.source = opts[id - 1000].first; applySettings() }
        col.addView(rg)
        eKey = EditText(this).apply {
            hint = "Khóa OpenChargeMap"; setText(prefs.ocmKey); isSingleLine = true
        }
        eUrl = EditText(this).apply {
            hint = "https://... (nhận ?lat=&lon=&r=)"; setText(prefs.customUrl); isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        eRadius = EditText(this).apply {
            hint = "Bán kính (km)"; setText(prefs.radiusKm.toString()); inputType = InputType.TYPE_CLASS_NUMBER
        }
        col.addView(eKey); col.addView(eUrl)
        col.addView(text(13f, color = Color.parseColor("#9AA5B1")).apply { text = "Bán kính tìm (km)" })
        col.addView(eRadius)
        col.addView(Button(this).apply {
            text = "Lưu cài đặt"
            setOnClickListener { saveFields(); applySettings() }
        })
        col.addView(Button(this).apply {
            text = "Lấy khóa OpenChargeMap"
            setOnClickListener {
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://openchargemap.org/site/profile/applications")))
            }
        })

        col.addView(label("Các trạm quanh xe"))
        tList = text(14f, color = Color.parseColor("#DDE3E8")).apply { setLineSpacing(0f, 1.25f) }
        col.addView(tList)

        return ScrollView(this).apply {
            setBackgroundColor(Color.parseColor("#0E1216"))
            addView(col)
        }
    }

    private fun render() {
        val s = AppState
        bRun.text = if (s.running) "Dừng" else "Bắt đầu"
        bRun.setBackgroundColor(if (s.running) Color.parseColor("#5A2A2A") else green)
        bRun.setTextColor(Color.WHITE)
        bHud.text = if (prefs.hudMac.isBlank()) "Chọn HUD" else "HUD: ${prefs.hudName.ifBlank { "?" }} (${prefs.hudMac})"
        val p = s.picked
        if (p == null) {
            tName.text = if (s.running) "Đang tìm…" else "Bấm Bắt đầu"
            tDist.text = "—"; tInfo.text = ""
        } else {
            tName.text = p.st.name
            tDist.text = "${Geo.fmtDist(p.distM)}  ${arrow(p.side)}"
            tInfo.text = listOfNotNull(
                Geo.sideText(p.side).replaceFirstChar { it.uppercase() },
                p.st.operator.takeIf { it.isNotBlank() && it != p.st.name },
                p.st.maxKw.takeIf { it > 0 }?.let { "$it kW" },
                p.st.ports.takeIf { it > 0 }?.let { "$it cổng" },
            ).joinToString(" · ") + "\n(khoảng cách đường chim bay)"
        }
        tGps.text = "GPS: ${s.gps}" + if (s.running && s.lastFix != null) " · ${s.speedKmh} km/h" else ""
        val sent = if (s.sentAt > 0) " · gửi lúc " + SimpleDateFormat("HH:mm:ss", Locale.US).format(Date(s.sentAt)) else ""
        tHud.text = "HUD: ${s.hud}$sent"
        tData.text = "Dữ liệu: ${s.data}"
        tList.text = if (s.ranked.isEmpty()) "—" else s.ranked.joinToString("\n") {
            "${Geo.fmtDist(it.distM).padEnd(8)} ${arrow(it.side)}  ${it.st.name}" + (if (it.st.maxKw > 0) " (${it.st.maxKw} kW)" else "")
        }
        eKey.visibility = if (prefs.source == Sources.OCM) View.VISIBLE else View.GONE
        eUrl.visibility = if (prefs.source == Sources.CUSTOM) View.VISIBLE else View.GONE
    }

    private fun arrow(c: Char) = when (c) { 'A' -> "↑"; 'R' -> "→"; 'L' -> "←"; 'B' -> "↓"; else -> "" }

    private fun saveFields() {
        prefs.ocmKey = eKey.text.toString()
        prefs.customUrl = eUrl.text.toString()
        eRadius.text.toString().toIntOrNull()?.let { prefs.radiusKm = it }
    }

    /** Dang chay thi bao dich vu tai lai theo cai dat moi. */
    private fun applySettings() {
        render()
        if (AppState.running) TramSacService.start(this)
    }

    // ---------------- Quyen ----------------
    private fun neededPerms(): Array<String> {
        val l = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        if (Build.VERSION.SDK_INT >= 31) { l += Manifest.permission.BLUETOOTH_SCAN; l += Manifest.permission.BLUETOOTH_CONNECT }
        if (Build.VERSION.SDK_INT >= 33) l += Manifest.permission.POST_NOTIFICATIONS
        return l.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }.toTypedArray()
    }

    private var afterPerms: (() -> Unit)? = null

    private fun withPerms(then: () -> Unit) {
        val need = neededPerms()
        if (need.isEmpty()) { then(); return }
        afterPerms = then
        requestPermissions(need, 7)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val missing = neededPerms().filter { it != Manifest.permission.POST_NOTIFICATIONS }
        if (missing.isEmpty()) afterPerms?.invoke()
        else AlertDialog.Builder(this)
            .setMessage("App cần quyền Vị trí và Thiết bị lân cận (Bluetooth) để tìm trạm và gửi lên HUD.")
            .setPositiveButton("Mở cài đặt") { _, _ ->
                startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
            }
            .setNegativeButton("Để sau", null).show()
        afterPerms = null
    }

    private fun toggleRun() {
        saveFields()
        if (AppState.running) { TramSacService.stop(this); return }
        withPerms {
            if (prefs.hudMac.isBlank()) pickHud { TramSacService.start(this) } else TramSacService.start(this)
        }
    }

    private fun sendTest() {
        withPerms {
            if (prefs.hudMac.isBlank()) { pickHud(); return@withPerms }
            if (!AppState.running) TramSacService.start(this)
            startService(Intent(this, TramSacService::class.java).setAction(TramSacService.TEST))
        }
    }

    // ---------------- Chon HUD ----------------
    @SuppressLint("MissingPermission")
    private fun pickHud(done: (() -> Unit)? = null) {
        withPerms {
            val mgr = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
            val adapter = mgr.adapter
            if (adapter == null || !adapter.isEnabled) {
                AlertDialog.Builder(this).setMessage("Bật Bluetooth trước nhé.").setPositiveButton("OK", null).show()
                return@withPerms
            }
            val found = LinkedHashMap<String, String>()    // mac -> ten
            val items = ArrayList<String>()
            val ad = ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, items)
            fun add(mac: String, name: String?, connected: Boolean) {
                val n = name ?: return
                if (!n.contains("HUD", true)) return
                if (found.containsKey(mac)) return
                found[mac] = n
                items.add("$n  ($mac)" + if (connected) "  · đang nối" else "")
                ad.notifyDataSetChanged()
            }
            // HUD dang noi voi Waze/GOFA thi khong con quang ba: lay tu danh sach ket noi cua dien thoai
            mgr.getConnectedDevices(BluetoothProfile.GATT).forEach { add(it.address, it.name, true) }
            val scanner = adapter.bluetoothLeScanner
            val cb = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, r: ScanResult) {
                    add(r.device.address, r.scanRecord?.deviceName ?: r.device.name, false)
                }
            }
            scanner?.startScan(cb)
            val h = Handler(Looper.getMainLooper())
            val stopScan = Runnable { try { scanner?.stopScan(cb) } catch (_: Exception) {} }
            h.postDelayed(stopScan, 10000)
            AlertDialog.Builder(this)
                .setTitle("Chọn HUD (đang quét…)")
                .setAdapter(ad) { _, which ->
                    val mac = found.keys.elementAt(which)
                    prefs.hudMac = mac; prefs.hudName = found[mac] ?: ""
                    render()
                    if (AppState.running) TramSacService.start(this)
                    done?.invoke()
                }
                .setNegativeButton("Đóng", null)
                .setOnDismissListener { h.removeCallbacks(stopScan); stopScan.run() }
                .show()
        }
    }
}
