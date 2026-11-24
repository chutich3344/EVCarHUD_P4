package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.os.SystemClock
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.webkit.WebViewClient
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Man canh lai 3D kieu Tesla (22/11): ve lai the gioi quanh xe tu dung nhung gi AI thay.
 * Trang assets/scene3d/index.html (Three.js r128, MIT) tu ve 60 khung/giay; moi lan AI chay ta goi feed(json):
 *  - vach lane, mep duong, duong di: openpilot (Snap.out)
 *  - xe/nguoi: YOLOX (VehDet toan khung khi bat 3D, khong co thi vung quanh xe truoc), chieu chan hop xuong mat duong
 *  - xe truoc: khoang cach openpilot (chuan hon chieu hop)
 * Nhe: canh it tam giac, WebView dung GPU; khi an thi khong goi feed va tam dung ve.
 */
@SuppressLint("SetJavaScriptEnabled", "ViewConstructor")
class Scene3D(ctx: Context) : WebView(ctx) {
    private var ready = false
    private var lastFeed = 0L
    /** "chase" (sau xe) | "high" (tren cao) */
    var view = "chase"
        set(v) { field = v; sentView = "" }
    private var sentView = ""
    /** Che do ngay/dem: 0 tu dong theo mat troi, 1 luon dem, 2 luon ngay. */
    var themeMode = 0
    /** Nguoi dung bam nut doi goc tren man 3D -> luu cai dat. */
    var onView: ((String) -> Unit)? = null

    private inner class Bridge {
        @JavascriptInterface fun setView(v: String) {
            if (v !in MainActivity.VIEWS3D) return
            post { view = v; sentView = v; onView?.invoke(v) }
        }
    }

    init {
        setBackgroundColor(Color.rgb(21, 24, 29))
        settings.javaScriptEnabled = true
        settings.allowFileAccess = true
        @Suppress("DEPRECATION") settings.allowFileAccessFromFileURLs = true   // nap mau xe .glb trong filesDir
        settings.mediaPlaybackRequiresUserGesture = true
        isVerticalScrollBarEnabled = false; isHorizontalScrollBarEnabled = false
        webViewClient = object : WebViewClient() {
            override fun onPageFinished(v: WebView?, url: String?) { ready = true; applyEgo() }
        }
        addJavascriptInterface(Bridge(), "LaneApp")
        loadUrl("file:///android_asset/scene3d/index.html")
    }

    /** Mau xe minh (.glb), null = xe mac dinh. */
    var egoFile: String? = null
    var egoFlip = false
    var egoWhite = false
    /** Cay + nha minh hoa (Cai dat > Hien thi). */
    var deco = false
    var egoKind = "vf7"
    fun applyEgo() {
        if (!ready) return
        val f = egoFile?.takeIf { java.io.File(it).isFile }
        val u = if (f == null) "null" else "'file://" + f.replace("'", "") + "?t=" + java.io.File(f).lastModified() + "'"
        evaluateJavascript("window.setEgoModel && setEgoModel($u, $egoFlip, $egoWhite, '${if (egoKind == "basic") "basic" else "vf7"}')", null)
    }

    fun pause() { onPause() }
    fun resume() { onResume() }

    /** Goi tren luong giao dien moi Snap. Gioi han 20 lan/giay. */
    private var sentTl = ""
    private var outAt = 0L; private var infoAt = 0L; private var gapF = 0f; private var pendDrop = 0; private var idleSkip = 0
    companion object {
        /** Chan doan cho log may chu: khoang cach giua 2 lan co ket qua AI toi man 3D, so goi bi bo. */
        @Volatile var info = "-"
    }
    fun update(sn: Snap) {
        if (!ready || visibility != VISIBLE) return
        val now = SystemClock.elapsedRealtime()
        // 23/11 (lane 3D nhay): Snap KHONG co ket qua AI (HUD/GPS/xe YOLO goi publishIdle) khi AI van dang chay -> bo qua,
        // truoc day goi nay chiem cho 45 ms va lam ROI goi co ket qua openpilot that ngay sau do.
        // 23/11 toi (gop y): KHONG bo han goi nay (xe YOLO / GPS / HUD van phai cap nhat) -> goi co ket qua AI luon qua ngay,
        // goi khong co ket qua chi gui toi da ~8 lan/giay va giu nguyen vach + xe truoc cua lan AI gan nhat (khong nhay).
        if (sn.out == null && now - lastFeed < 120) { idleSkip++; return }
        if (sn.out != null) { if (outAt > 0) gapF += 0.2f * ((now - outAt) - gapF); outAt = now }
        lastFeed = now
        if (now - infoAt >= 10_000) { infoAt = now; info = "${gapF.toInt()}ms/lan · bo ${pendDrop} · nghi ${idleSkip}"; pendDrop = 0; idleSkip = 0 }
        // den giao thong + so giay: goi rieng, chay ca khi AI dang nghi (xe do / dung den do lau, khong co sn.out)
        val tk = (sn.tl?.toString() ?: "-") + (sn.tlSec ?: -1) + sn.tlGo
        if (tk != sentTl) { sentTl = tk; evaluateJavascript("window.setTl && setTl(${sn.tl?.let { "'$it'" } ?: "null"}, ${sn.tlSec ?: "null"}, '${sn.tlGo}')", null) }
        val js = json(sn) ?: return
        evaluateJavascript("feed($js)", null)
    }

    private fun f(v: Float, d: Int = 2): String {
        if (v.isNaN() || v.isInfinite()) return "0"
        val m = when (d) { 1 -> 10f; else -> 100f }
        return ((v * m).roundToInt() / m).toString()
    }

    /** 23/11: xe dung lau (AI openpilot nghi cho mat may) Snap khong co out -> giu hinh duong lan cuoi (hoac duong thang mac dinh)
     *  de xe YOLO quanh xe (xe chay ngang, xe dau) van hien khi minh dung yen. */
    private var lastOut: Op.Out? = null
    private val defOut: Op.Out by lazy { Op.Out().also { d ->
        val ys = floatArrayOf(-5.4f, -1.8f, 1.8f, 5.4f); val ps = floatArrayOf(0.2f, 0.6f, 0.6f, 0.2f)
        for (i in 0..3) { d.laneY[i].fill(ys[i]); d.laneProb[i] = ps[i] }
        d.edgeY[0].fill(-5.6f); d.edgeY[1].fill(2.9f)
    } }

    private fun json(sn: Snap): String? {
        sn.out?.let { lastOut = it }
        val live = sn.out != null || (outAt > 0 && SystemClock.elapsedRealtime() - outAt < 700)   // AI van dang chay: goi phu giu ket qua moi nhat
        val o = sn.out ?: lastOut ?: defOut
        val sb = StringBuilder(2600)
        sb.append("{\"v\":").append(f(sn.vKmh / 3.6f))
        sb.append(",\"th\":\"").append(if (isNight(sn)) "night" else "day").append('"')
        if (sentView != view) { sb.append(",\"view\":\"").append(view).append('"'); sentView = view }
        sb.append(",\"ln\":[")
        for (i in 0..3) { if (i > 0) sb.append(','); arr(sb, o.laneY[i]) }
        sb.append("],\"lp\":[")
        for (i in 0..3) { if (i > 0) sb.append(','); sb.append(f(o.laneProb[i])) }
        sb.append("],\"ed\":[")
        arr(sb, o.edgeY[0]); sb.append(','); arr(sb, o.edgeY[1]); sb.append(']')
        if (live && o.planOk) {
            sb.append(",\"pl\":[")
            for (k in 0 until Op.N) { if (k > 0) sb.append(','); sb.append('[').append(f(o.planX[k], 1)).append(',').append(f(o.planY[k])).append(']') }
            sb.append(']')
        }
        val leadOk = live && o.leadProb > 0.5f
        if (leadOk) sb.append(",\"ld\":{\"p\":").append(f(o.leadProb)).append(",\"x\":").append(f(o.leadX, 1))
            .append(",\"y\":").append(f(o.leadY)).append(",\"v\":").append(f(o.leadV)).append('}')
        sn.lm?.let { m -> sb.append(",\"lm\":[").append(m.joinToString(",")).append(']') }
        if (sn.wide != null) sb.append(",\"os\":").append(sn.wideSeq).append(",\"oa\":").append(sn.wideAge)
        if (!sn.cal) sb.append(",\"cal\":0")
        if (deco) sb.append(",\"deco\":1")
        // 23/11 toi: gui CA KHI RONG (co ban do, khong co nhanh) -> trang 3D biet "ban do noi khong co giao lo" de khong dung nga tu tu den gia
        sn.br?.let { b -> sb.append(",\"br\":[").append(b.joinToString(",") { "[" + f(it[0], 1) + "," + it[1].toInt() + "," + it[2].toInt() + "]" }).append("],\"bra\":").append(sn.brAt) }
        tlPos(sn)?.let { sb.append(",\"tlx\":[").append(f(it[0], 1)).append(',').append(f(it[1], 1)).append(']') }
        if (live) sb.append(",\"wz\":").append(f(o.poseRz))   // 23/11 toi: toc do quay xe minh (rad/s) -> biet dang re
        // tuoi vach (ms): goi khong co ket qua AI moi thi bao vach dang dung la ket qua cu bao lau (man 3D lam mo vach > 1,5 s)
        if (!live) sb.append(",\"la\":").append(if (outAt > 0) SystemClock.elapsedRealtime() - outAt else 99999)
        if (sn.stat.isNotEmpty()) sb.append(",\"dbg\":1")   // bat "Ky thuat": ve them vach THO (chua lam muot) de so voi vach da ve
        // 23/11 toi: bien bao + camera phat nguoi cung len man 3D (truoc day chi ve o man camera -> 3D toan man khong thay bien)
        sn.limit?.let { l -> sb.append(",\"lim\":").append(l); if (sn.over) sb.append(",\"ov\":1"); sn.limitSrc?.let { sb.append(",\"ls\":\"").append(esc(it)).append('"') } }
        sn.sign?.takeIf { !(it.cls == SignDet.SPEED && it.value == sn.limit) }?.let { g ->
            sb.append(",\"sg\":{\"c\":").append(g.cls); g.value?.let { sb.append(",\"v\":").append(it) }
            sb.append(",\"n\":\"").append(esc(SignDet.NAME.getOrElse(g.cls) { "" })).append("\"}")
        }
        sn.camType?.let { t -> sb.append(",\"cm\":{\"n\":\"").append(esc(CamDb.name(t))).append('"'); sn.camM?.let { sb.append(",\"m\":").append(it) }; sb.append('}') }
        sb.append(",\"ldw\":\"").append(sn.ldw).append('"')
        if (sn.offCm != null) sb.append(",\"off\":").append(sn.offCm)
        sb.append(",\"ob\":[")
        objs(sn, o, leadOk).forEachIndexed { i, b -> if (i > 0) sb.append(','); sb.append("[\"").append(b.c).append("\",").append(f(b.x, 1)).append(',').append(f(b.y)).append(',').append(if (b.lead) 1 else 0).append(',').append(f(b.s)).append(']') }
        sb.append("]}")
        return sb.toString()
    }

    private fun esc(t: String) = t.replace("\\", "\\\\").replace("\"", "\\\"")
    private fun arr(sb: StringBuilder, a: FloatArray) { sb.append('['); for (k in a.indices) { if (k > 0) sb.append(','); sb.append(f(a[k])) }; sb.append(']') }

    private class G(var c: String, var x: Float, var y: Float, var lead: Boolean, val s: Float = 1f)

    /** Do cui bu them (rad, duong = camera thuc te ngua len hon hieu chinh dang dung). Tu khop theo khoang cach xe truoc cua openpilot. */
    private var dPitch = 0.0
    private var dPitchN = 0

    /**
     * Hop xe (toa do chuan hoa anh) -> diem tren mat duong (x truoc, y phai, m).
     * 23/11: cam 70mai luc dang tu hieu chinh (cui = 0) lam moi xe bi chieu sat dau xe -> "don cuc".
     * Sua: (1) tu khop do cui bang xe truoc (openpilot biet khoang cach) nen khong phu thuoc hieu chinh;
     * (2) cam mat ca (goc ngang > 100 do, 70mai) dung chieu equidistant de xe hai ben khong bi ep vao giua;
     * (3) bo hop "nguoi" dang ngoi tren xe may / xe dap (truoc ve ca nguoi lan xe thanh 1 dong).
     */
    /** 23/11 toi: vi tri den giao thong tren mat duong (x truoc, y phai, m) tu hop den -> dung nga tu DUNG cho cot den.
     *  Ket hop 2 cach: goc ngang len (den cao ~5 m) va co hop (vo den ~0,4 m); lay trung binh nhan. */
    private fun tlPos(sn: Snap): FloatArray? {
        val cam = sn.cam ?: return null
        val col = sn.tl ?: return null
        val l = sn.tlLamps?.filter { it.col == col }?.maxByOrNull { it.conf } ?: return null
        val m = cam.viewFromCalib
        val fish = cam.fisheye || 2 * Math.atan(cam.w / 2.0 / cam.f) > Math.toRadians(100.0)
        val fEq = if (cam.fisheye) cam.f else cam.f * 561.0 / 450.0
        val u = (l.x1 + l.x2) / 2 * cam.w; val v = (l.y1 + l.y2) / 2 * cam.h
        var vx: Double; var vy: Double; var vz: Double
        if (fish) { val dx = u - cam.cx; val dy = v - cam.cy; val r = Math.hypot(dx, dy) + 1e-9; val th = r / fEq
            vx = Math.sin(th) * dx / r; vy = Math.sin(th) * dy / r; vz = Math.cos(th) }
        else { vx = (u - cam.cx) / cam.f; vy = (v - cam.cy) / cam.f; vz = 1.0 }
        val c = Math.cos(dPitch); val sn2 = Math.sin(dPitch)
        val y2 = vy * c - vz * sn2; val z2 = vy * sn2 + vz * c; vy = y2; vz = z2
        val rx = m[0] * vx + m[3] * vy + m[6] * vz; val ry = m[1] * vx + m[4] * vy + m[7] * vz; val rz = m[2] * vx + m[5] * vy + m[8] * vz
        if (rx <= 0.05) return null
        val fwd = Math.hypot(rx, ry)
        val dEl = if (rz < -0.01) (5.0 - sn.camH) * fwd / -rz else Double.NaN          // z xuong: den o tren -> rz < 0
        val side = minOf((l.x2 - l.x1) * cam.w, (l.y2 - l.y1) * cam.h).toDouble()
        val fpx = if (fish) fEq else cam.f
        val dSz = if (side > 2) 0.4 * fpx / side else Double.NaN
        val d = when { !dEl.isNaN() && !dSz.isNaN() -> Math.sqrt(dEl * dSz); !dEl.isNaN() -> dEl; !dSz.isNaN() -> dSz; else -> return null }.coerceIn(6.0, 90.0)
        return floatArrayOf((d * rx / fwd).toFloat(), (d * ry / fwd).toFloat())
    }

    private fun objs(sn: Snap, o: Op.Out, leadOk: Boolean): List<G> {
        val cam = sn.cam ?: return emptyList()
        val raw = sn.wide ?: sn.objs ?: emptyList()
        val m = cam.viewFromCalib
        // 23/11: Op.Cam co co fisheye (70mai khu meo equidistant, f = tieu cu equidistant 561 px @1280) -> dung thang f
        val fish = cam.fisheye || 2 * Math.atan(cam.w / 2.0 / cam.f) > Math.toRadians(100.0)
        val fEq = if (cam.fisheye) cam.f else cam.f * 561.0 / 450.0          // 70mai: r = f*theta (do tu video goc 2880: f=1262)
        fun ray(u: Float, v: Float, dp: Double = dPitch): DoubleArray {
            var vx: Double; var vy: Double; var vz: Double
            if (fish) {
                val dx = u - cam.cx; val dy = v - cam.cy; val r = Math.hypot(dx, dy) + 1e-9; val th = r / fEq
                vx = Math.sin(th) * dx / r; vy = Math.sin(th) * dy / r; vz = Math.cos(th)
            } else { vx = (u - cam.cx) / cam.f; vy = (v - cam.cy) / cam.f; vz = 1.0 }
            // bu do cui: quay tia quanh truc ngang cua camera
            val c = Math.cos(dp); val s = Math.sin(dp)
            val y2 = vy * c - vz * s; val z2 = vy * s + vz * c; vy = y2; vz = z2
            // calib = M^T * view
            return doubleArrayOf(m[0] * vx + m[3] * vy + m[6] * vz, m[1] * vx + m[4] * vy + m[7] * vz, m[2] * vx + m[5] * vy + m[8] * vz)
        }
        // bo nguoi lai xe may / xe dap: hop nguoi chong len hop xe 2 banh
        val twoW = raw.filter { it.cls == 1 || it.cls == 3 }
        val src = raw.filter { b -> b.cls != 0 || twoW.none { w ->
            val ix = minOf(b.x2, w.x2) - maxOf(b.x1, w.x1); val iy = minOf(b.y2, w.y2) - maxOf(b.y1, w.y1)
            ix > 0.5f * (b.x2 - b.x1) && iy > 0f } }
        val hoodPx = (if (sn.hood in 0.3f..1f) sn.hood else 1f) * cam.h
        // tu khop do cui theo xe truoc: chon hop xe cung huong ngang voi xe truoc, gan nhat
        if (leadOk && o.leadProb > 0.7f && o.leadX in 6f..80f) {
            val aLead = Math.atan2(o.leadY.toDouble(), o.leadX.toDouble())
            var best: SignDet.Obj? = null; var bestW = 0f
            for (b in src) if (b.cls == 2 || b.cls == 5 || b.cls == 7) {
                if (b.y2 * cam.h > hoodPx - 3 || b.conf < 0.35f) continue   // 24/11: hop diem thap (chi de noi vet) khong dung khop do cui
                val r = ray((b.x1 + b.x2) / 2 * cam.w, b.y2 * cam.h)
                if (abs(Math.atan2(r[1], r[0]) - aLead) < 0.09 && b.x2 - b.x1 > bestW) { best = b; bestW = b.x2 - b.x1 }
            }
            if (best != null) {
                val r = ray((best.x1 + best.x2) / 2 * cam.w, best.y2 * cam.h)
                val aCur = Math.atan2(r[2], Math.hypot(r[0], r[1]))
                val aDes = Math.atan2(sn.camH.toDouble(), Math.hypot(o.leadX.toDouble(), o.leadY.toDouble()))
                val k = if (dPitchN < 20) 0.35 else 0.12
                dPitch = (dPitch + k * (aCur - aDes)).coerceIn(-0.35, 0.35); dPitchN++
            }
        }
        val res = ArrayList<G>()
        for (b in src) {
            val c = when (b.cls) { 0 -> "person"; 1 -> "bike"; 2 -> "car"; 3 -> "moto"; 5 -> "bus"; 7 -> "truck"; else -> continue }
            val u = (b.x1 + b.x2) / 2 * cam.w
            val vb = minOf(b.y2 * cam.h, hoodPx - 2)
            val r = ray(u, vb)
            if (r[2] <= 0.004) continue
            val t = sn.camH / r[2]
            val gx = (r[0] * t).toFloat(); val gy = (r[1] * t).toFloat()
            if (gx < 1.5f || gx > 110f) continue
            var cc = c
            if (c == "truck" || c == "bus") {
                // xe tai/buyt thap hon 2,4 m thuc ra la SUV / ban tai (xe sat capo: khoang cach bi kep -> chieu cao nho, nghieng ve "o to")
                val rt = ray(u, b.y1 * cam.h); val sc = gx / maxOf(rt[0], 1e-3)
                if (sn.camH - rt[2] * sc < 2.4) cc = "car"
            }
            res += G(cc, gx, gy, false, b.conf)
        }
        if (leadOk) {
            var best: G? = null
            for (g in res) if ((g.c == "car" || g.c == "truck" || g.c == "bus") && abs(g.y - o.leadY) < 2f && abs(g.x - o.leadX) < 0.3f * o.leadX + 5f)
                if (best == null || abs(g.x - o.leadX) < abs(best.x - o.leadX)) best = g
            if (best != null) { best.x = o.leadX; best.y = o.leadY; best.lead = true }
            else if (o.leadProb > 0.7f) res += G("car", o.leadX, o.leadY, true)
        }
        return res
    }

    private fun isNight(sn: Snap): Boolean {
        if (themeMode == 1) return true
        if (themeMode == 2) return false
        val cal = java.util.Calendar.getInstance()
        val h = cal.get(java.util.Calendar.HOUR_OF_DAY) + cal.get(java.util.Calendar.MINUTE) / 60.0
        if (!sn.hasFix) return h !in 6.0..18.0
        // goc cao mat troi > -6 do = ngay (cung cach ban do Google tu ngay/dem)
        val doy = cal.get(java.util.Calendar.DAY_OF_YEAR)
        val decl = Math.toRadians(23.45) * Math.sin(2 * PI * (284 + doy) / 365.0)
        val tz = (cal.get(java.util.Calendar.ZONE_OFFSET) + cal.get(java.util.Calendar.DST_OFFSET)) / 3_600_000.0
        val hAng = Math.toRadians(15.0 * (h - tz + sn.lon / 15.0 - 12))
        val latR = Math.toRadians(sn.lat)
        val sinElev = Math.sin(latR) * Math.sin(decl) + Math.cos(latR) * Math.cos(decl) * Math.cos(hAng)
        return sinElev <= Math.sin(Math.toRadians(-6.0))
    }
}
