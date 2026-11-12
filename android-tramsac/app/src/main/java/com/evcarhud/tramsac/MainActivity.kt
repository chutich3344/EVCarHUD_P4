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
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.webkit.GeolocationPermissions
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.ArrayAdapter
import android.widget.FrameLayout
import org.json.JSONArray
import org.json.JSONObject

/**
 * Giao dien theo thiet ke kinh 3D (assets/hud.html) trong WebView, noi voi dich vu nen qua cau noi "Android".
 * Ban do / chi tiet / gui tram moi: trang evcarhud.com/tram-sac mo trong WebView thu hai; nut "Gui len HUD"
 * tren trang do (evcarhud-tramsac://gui?...) duoc bat ngay trong app.
 */
class MainActivity : Activity() {
    companion object {
        const val HUB = "https://evcarhud.com/tram-sac"
        private const val REQ_LOC = 8
        private const val REQ_AUTO = 9
        /** Trang tram-sac la man chinh cua app: doi nut cuoi thanh dieu huong (thong tin EVCarHUD) thanh nut mo man HUD. */
        private const val NAV_JS = """(function(){ if (window.__hudNav) return; window.__hudNav = 1;
  function fix(){ var a = document.querySelector('nav.nav a[href="/"]'); if (!a || a.dataset.hud) return;
    a.dataset.hud = 1; a.setAttribute('aria-label', 'HUD'); a.setAttribute('href', '#hud');
    a.innerHTML = '<i class="fa-solid fa-display"></i><span>HUD</span>'; }
  fix(); new MutationObserver(fix).observe(document.body, {childList:true, subtree:true});
  document.addEventListener('click', function(e){
    var a = e.target.closest && e.target.closest('a'); if (!a || !a.dataset.hud) return;
    e.preventDefault(); e.stopPropagation(); if (window.AndroidHub) AndroidHub.hud();
  }, true);
})();"""
        private val SOURCES = listOf(
            Sources.VF_OSM to "VinFast + OSM",
            Sources.VF to "Chỉ VinFast",
            Sources.CUSTOM to "Cộng đồng EVCarHUD",
            Sources.OSM to "OpenStreetMap",
            Sources.OCM to "OpenChargeMap",
        )
    }

    private lateinit var prefs: Prefs
    private lateinit var web: WebView
    private var hub: WebView? = null
    private lateinit var root: FrameLayout
    private val main = Handler(Looper.getMainLooper())
    private val refresh: () -> Unit = { pushState() }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        window.statusBarColor = Color.parseColor("#0f1d16")
        window.navigationBarColor = Color.parseColor("#0f1d16")
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
        root = FrameLayout(this).apply { setBackgroundColor(Color.parseColor("#0f1d16")) }
        web = WebView(this).apply {
            setBackgroundColor(Color.parseColor("#0f1d16"))
            settings.javaScriptEnabled = true
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            addJavascriptInterface(Bridge(), "Android")
            webViewClient = object : WebViewClient() {
                // Man HUD chi la trang noi bo: moi lien ket ra ngoai mo bang trinh duyet
                override fun shouldOverrideUrlLoading(v: WebView, r: WebResourceRequest): Boolean {
                    if (r.url.toString().startsWith("file:///android_asset/")) return false
                    openExternal(r.url); return true
                }
            }
            loadUrl("file:///android_asset/hud.html")
        }
        root.addView(web, ViewGroup.LayoutParams(-1, -1))
        setContentView(root)
        openHub("")                                      // mo app vao thang trang chu tram sac
        // Xin vi tri ngay khi mo app de trang chu hien ban do va tram quanh xe.
        // Da chon HUD: xin luon Bluetooth va tu gui vi tri GPS cho HUD (Mi Mi tim tram can GPS that), khong can bam chon tram.
        if (prefs.hudMac.isNotBlank()) {
            val need = neededPerms()
            if (need.isEmpty()) TramSacService.startPos(this) else requestPermissions(need, REQ_AUTO)
        } else if (!hasLoc())
            requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION), REQ_LOC)
        handleLink(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleLink(intent)
    }

    /** evcarhud-tramsac://gui?id=..&name=..&lat=..&lon=..&kw=..&ports=.. tu nut "Gui len HUD" tren web. */
    private fun handleLink(i: Intent?) {
        val u = i?.data ?: return
        if (u.scheme != "evcarhud-tramsac" || u.host != "gui") return
        i.data = null                                    // xoay man hinh khong xu ly lai
        val lat = u.getQueryParameter("lat")?.toDoubleOrNull()
        val lon = u.getQueryParameter("lon")?.toDoubleOrNull()
        if (lat == null || lon == null || lat !in -90.0..90.0 || lon !in -180.0..180.0) {
            AlertDialog.Builder(this).setMessage("Liên kết trạm sạc không hợp lệ.").setPositiveButton("OK", null).show()
            return
        }
        val name = (u.getQueryParameter("name") ?: "").trim().take(120).ifBlank { "Trạm sạc" }
        val kw = u.getQueryParameter("kw")?.toDoubleOrNull()?.toInt()?.coerceIn(0, 2000) ?: 0
        val ports = u.getQueryParameter("ports")?.toIntOrNull()?.coerceIn(0, 999) ?: 0
        prefs.target = Station(name, lat, lon, "", kw, ports)
        pushState()
        closeHub()
        withPerms {
            if (prefs.hudMac.isBlank()) pickHud { TramSacService.start(this) } else TramSacService.start(this)
        }
    }

    override fun onResume() {
        super.onResume()
        AppState.listen(refresh)
        pushState()
        if (AppState.running) askBattery()
    }

    override fun onPause() {
        AppState.unlisten(refresh)
        super.onPause()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val h = hub
        if (h != null && h.visibility == View.VISIBLE) { if (h.canGoBack()) { h.goBack(); return } }
        else if (h != null) { h.visibility = View.VISIBLE; return }      // dang o man HUD -> ve trang chu
        @Suppress("DEPRECATION") super.onBackPressed()
    }

    // ---------------- Trang thai -> giao dien ----------------
    private fun state(): String {
        val s = AppState
        val o = JSONObject()
        o.put("running", s.running); o.put("gps", s.gps); o.put("hud", s.hud); o.put("data", s.data)
        o.put("speed", s.speedKmh); o.put("hudName", prefs.hudName); o.put("target", prefs.target != null)
        o.put("targetKey", prefs.target?.let { key(it) } ?: "")
        o.put("sentAt", s.sentAt)
        o.put("battOk", battOk())
        o.put("source", prefs.source); o.put("radius", prefs.radiusKm); o.put("ocmKey", prefs.ocmKey)
        val src = JSONArray(); SOURCES.forEach { src.put(JSONArray().put(it.first).put(it.second)) }; o.put("sources", src)
        // Tram dich vua chon: hien ngay, tinh khoang cach tu vi tri gan nhat neu co
        val tgt = prefs.target
        val p = if (tgt != null && (s.picked == null || key(s.picked!!.st) != key(tgt))) {
            s.lastFix?.let { (la, lo) -> Geo.rank(la, lo, null, listOf(tgt)).first() } ?: Nearby(tgt, -1, null)
        } else s.picked
        o.put("picked", p?.let { n -> js(n) } ?: JSONObject.NULL)
        val list = JSONArray(); s.ranked.forEach { list.put(js(it)) }; o.put("list", list)
        return o.toString()
    }

    private fun key(st: Station) = "${st.name}|${"%.5f".format(java.util.Locale.US, st.lat)}|${"%.5f".format(java.util.Locale.US, st.lon)}"

    private fun js(n: Nearby) = JSONObject().apply {
        put("key", key(n.st))
        put("name", n.st.name); put("op", n.st.operator); put("kw", n.st.maxKw); put("ports", n.st.ports)
        put("dist", if (n.distM < 0) JSONObject.NULL else n.distM)
        put("rel", n.relDeg ?: JSONObject.NULL); put("side", if (n.distM < 0) "" else n.side.toString())
    }

    private fun pushState() {
        if (!::web.isInitialized) return
        web.evaluateJavascript("window.onState && window.onState(" + JSONObject.quote(state()) + ")", null)
    }

    /** Ham goi tu trang hud.html (chay tren luong rieng cua WebView -> chuyen ve luong chinh). */
    inner class Bridge {
        @JavascriptInterface fun state(): String = this@MainActivity.state()
        @JavascriptInterface fun toggle() = ui { toggleRun() }
        @JavascriptInterface fun pickHud() = ui { this@MainActivity.pickHud() }
        @JavascriptInterface fun sendTest() = ui { this@MainActivity.sendTest() }
        @JavascriptInterface fun openHub(path: String) = ui { this@MainActivity.openHub(path) }
        @JavascriptInterface fun clearTarget() = ui { prefs.target = null; AppState.picked = null; applySettings() }
        @JavascriptInterface fun setTarget(i: Int) = ui {
            AppState.ranked.getOrNull(i)?.let { prefs.target = it.st; AppState.picked = it; pushState(); sendNow() }
        }
        @JavascriptInterface fun sendNow() = ui { this@MainActivity.sendNow() }
        @JavascriptInterface fun battery() = ui { if (battOk()) openAppSettings() else openBattery() }
        @JavascriptInterface fun setSource(k: String) = ui {
            if (SOURCES.any { it.first == k }) { prefs.source = k; applySettings() }
        }
        @JavascriptInterface fun setRadius(km: Int) = ui { prefs.radiusKm = km; applySettings() }
        @JavascriptInterface fun setOcmKey(k: String) = ui { prefs.ocmKey = k; applySettings() }
    }

    private fun ui(f: () -> Unit) { main.post(f) }

    /** Dang chay thi bao dich vu tai lai theo cai dat moi. */
    private fun applySettings() {
        pushState()
        if (AppState.running) TramSacService.start(this)
    }

    private fun openExternal(u: Uri) {
        try { startActivity(Intent(Intent.ACTION_VIEW, u)) } catch (_: Exception) {}
    }

    // ---------------- Trang tram sac (ban do, chi tiet, gui tram) ----------------
    @SuppressLint("SetJavaScriptEnabled")
    private fun openHub(path: String) {
        val h = hub ?: WebView(this).also { w ->
            w.setBackgroundColor(Color.parseColor("#0f1d16"))
            w.settings.javaScriptEnabled = true
            w.settings.domStorageEnabled = true
            w.settings.setGeolocationEnabled(true)
            w.settings.cacheMode = WebSettings.LOAD_DEFAULT
            w.webChromeClient = object : WebChromeClient() {
                override fun onGeolocationPermissionsShowPrompt(origin: String, cb: GeolocationPermissions.Callback) {
                    val ok = origin.startsWith("https://evcarhud.com") && hasLoc()
                    cb.invoke(origin, ok, false)
                }
            }
            // Nut "Trang chu" cua trang web: quay ve man HUD cua app (chi co 1 ham, vo hai)
            w.addJavascriptInterface(object {
                @JavascriptInterface fun hud() = ui { closeHub() }
            }, "AndroidHub")
            w.webViewClient = object : WebViewClient() {
                override fun onPageFinished(v: WebView, url: String) {
                    v.evaluateJavascript(NAV_JS, null)
                }

                // Mat mang: khong de trang loi, chuyen sang man HUD (van chay offline)
                override fun onReceivedError(v: WebView, r: WebResourceRequest, e: android.webkit.WebResourceError) {
                    if (r.isForMainFrame) { hubFailed = true; ui { closeHub() } }
                }

                override fun shouldOverrideUrlLoading(v: WebView, r: WebResourceRequest): Boolean {
                    val u = r.url
                    if (u.scheme == "evcarhud-tramsac") { handleLink(Intent(Intent.ACTION_VIEW, u)); return true }
                    if (u.scheme == "intent") {                 // Intent URL tu nut "Gui len HUD"
                        try {
                            val i = Intent.parseUri(u.toString(), Intent.URI_INTENT_SCHEME)
                            if (i.data?.scheme == "evcarhud-tramsac") { handleLink(i); return true }
                        } catch (_: Exception) {}
                        return true
                    }
                    if (u.host == "evcarhud.com" && u.scheme == "https") return false
                    openExternal(u); return true
                }
            }
            hub = w
            root.addView(w, ViewGroup.LayoutParams(-1, -1))
        }
        h.visibility = View.VISIBLE
        val cur = h.url
        if (hubFailed || cur == null || !cur.startsWith(HUB)) { hubFailed = false; h.loadUrl(HUB + path.ifEmpty { "#/" }) }
        else h.evaluateJavascript("location.hash=" + JSONObject.quote(path.ifEmpty { "#/" }), null)   // da tai: chi doi trang, khong tai lai
    }

    /** An trang tram-sac (giu lai de quay ve nhanh), hien man HUD cua app. */
    private fun closeHub() {
        hub?.visibility = View.GONE
        pushState()
    }

    private var hubFailed = false

    // ---------------- Quyen ----------------
    /** Vi tri chinh xac hoac gan dung (Android 12+ cho chon "gan dung") deu dung duoc cho trang web. */
    private fun hasLoc() = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED ||
        checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED

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
        if (requestCode == REQ_AUTO) {
            if (hasLoc()) hub?.reload()
            if (neededPerms().none { it != Manifest.permission.POST_NOTIFICATIONS }) TramSacService.startPos(this)
            return
        }
        if (requestCode == REQ_LOC) {
            // Vua cho phep vi tri: tai lai trang chu de web lay vi tri that thay vi "dang xem quanh TP.HCM"
            if (hasLoc()) hub?.reload()
            return
        }
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

    /** Xin bo toi uu pin 1 lan: khong thi nhieu may (Xiaomi, Oppo...) dung app khi tat man hinh. */
    @SuppressLint("BatteryLife")
    private fun battOk() = (getSystemService(POWER_SERVICE) as android.os.PowerManager).isIgnoringBatteryOptimizations(packageName)
    private var askedBattery = false

    /** Hoi moi lan mo app (toi da 1 lan) khi chua duoc phep; force = nguoi dung bam nut trong Cai dat. */
    private fun askBattery(force: Boolean = false) {
        if (battOk()) return
        if (askedBattery && !force) return
        askedBattery = true
        AlertDialog.Builder(this)
            .setTitle("Chạy khi tắt màn hình")
            .setMessage("Để HUD vẫn nhận trạm khi tắt màn hình điện thoại, cho app chạy nền không bị giới hạn pin.")
            .setPositiveButton("Cho phép") { _, _ -> openBattery() }
            .setNegativeButton("Để sau", null).show()
    }

    @SuppressLint("BatteryLife")
    private fun openBattery() {
                try {
                    startActivity(Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
                } catch (_: Exception) {
                    try { startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName"))) } catch (_: Exception) {}
                }
    }

    private fun toggleRun() {
        if (AppState.running) { TramSacService.stopFind(this); return }
        askBattery()
        withPerms {
            if (prefs.hudMac.isBlank()) pickHud { TramSacService.start(this) } else TramSacService.start(this)
        }
    }

    /** Gui ngay tram dang chon len HUD (bat dich vu neu chua chay). */
    private fun openAppSettings() {
        try { startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName"))) } catch (_: Exception) {}
    }

    private fun sendNow() {
        askBattery()
        withPerms {
            if (prefs.hudMac.isBlank()) { pickHud { sendNow() }; return@withPerms }
            if (!AppState.running) TramSacService.start(this)
            startService(Intent(this, TramSacService::class.java).setAction(TramSacService.SEND))
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
                    pushState()
                    if (AppState.running) TramSacService.start(this)
                    else if (done == null) TramSacService.startPos(this, reconnect = true)   // vua doi HUD: gui vi tri cho HUD moi
                    done?.invoke()
                }
                .setNegativeButton("Đóng", null)
                .setOnDismissListener { h.removeCallbacks(stopScan); stopScan.run() }
                .show()
        }
    }
}
