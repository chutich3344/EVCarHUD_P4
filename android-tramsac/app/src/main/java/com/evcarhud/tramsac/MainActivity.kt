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
        /** Bat bam vao nut Trang chu (icon nha) tren trang tram-sac -> ve man HUD cua app. */
        private const val HOME_JS = """(function(){ if (window.__hudHome) return; window.__hudHome = 1;
  document.addEventListener('click', function(e){
    var a = e.target.closest && e.target.closest('a,button'); if (!a) return;
    var l = (a.getAttribute('aria-label') || a.textContent || '').trim().toLowerCase();
    var h = a.getAttribute('href') || '';
    if (l == 'trang chủ' || l == 'trang chu' || l == 'về trang chủ' || h == '#/' || h == '#/home') {
      e.preventDefault(); e.stopPropagation(); if (window.AndroidHub) AndroidHub.home();
    }
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
        window.statusBarColor = Color.parseColor("#06110b")
        window.navigationBarColor = Color.parseColor("#06110b")
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
        root = FrameLayout(this).apply { setBackgroundColor(Color.parseColor("#06110b")) }
        web = WebView(this).apply {
            setBackgroundColor(Color.parseColor("#06110b"))
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
    }

    override fun onPause() {
        AppState.unlisten(refresh)
        super.onPause()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val h = hub
        if (h != null) { if (h.canGoBack()) h.goBack() else closeHub(); return }
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
            w.setBackgroundColor(Color.parseColor("#06110b"))
            w.settings.javaScriptEnabled = true
            w.settings.domStorageEnabled = true
            w.settings.setGeolocationEnabled(true)
            w.settings.cacheMode = WebSettings.LOAD_DEFAULT
            w.webChromeClient = object : WebChromeClient() {
                override fun onGeolocationPermissionsShowPrompt(origin: String, cb: GeolocationPermissions.Callback) {
                    val ok = origin.startsWith("https://evcarhud.com") &&
                        checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                    cb.invoke(origin, ok, false)
                }
            }
            // Nut "Trang chu" cua trang web: quay ve man HUD cua app (chi co 1 ham, vo hai)
            w.addJavascriptInterface(object {
                @JavascriptInterface fun home() = ui { closeHub() }
            }, "AndroidHub")
            w.webViewClient = object : WebViewClient() {
                override fun onPageFinished(v: WebView, url: String) {
                    v.evaluateJavascript(HOME_JS, null)
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
        h.loadUrl(HUB + path)
    }

    private fun closeHub() {
        hub?.let { root.removeView(it); it.destroy() }
        hub = null
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
        if (AppState.running) { TramSacService.stop(this); return }
        withPerms {
            if (prefs.hudMac.isBlank()) pickHud { TramSacService.start(this) } else TramSacService.start(this)
        }
    }

    /** Gui ngay tram dang chon len HUD (bat dich vu neu chua chay). */
    private fun sendNow() {
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
                    done?.invoke()
                }
                .setNegativeButton("Đóng", null)
                .setOnDismissListener { h.removeCallbacks(stopScan); stopScan.run() }
                .show()
        }
    }
}
