package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.util.LruCache
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.roundToInt
import kotlin.math.tan

private val bold: Typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)

/** O nhiet trong suot: MAY (muc nhiet Android) | PIN do C | AI lan/giay. */
class TempPip(ctx: Context) : View(ctx) {
    var snap: Snap? = null
        set(v) { field = v; invalidate() }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = bold }

    override fun onDraw(cv: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        val r = RectF(1f, 1f, w - 1, h - 1)
        p.style = Paint.Style.FILL; p.color = Color.argb(140, 18, 24, 32)
        cv.drawRoundRect(r, h * 0.2f, h * 0.2f, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = 2f; p.color = Color.argb(90, 255, 255, 255)
        cv.drawRoundRect(r, h * 0.2f, h * 0.2f, p)
        p.style = Paint.Style.FILL
        val s = snap
        val yel = Color.rgb(255, 210, 63); val red = Color.rgb(255, 84, 104)
        val th = s?.thermal ?: 0
        val may = when (th) { 0 -> "Mát"; 1 -> "Ấm"; 2 -> "Nóng"; else -> "Rất nóng" }
        val mayC = when { th >= 3 -> red; th >= 1 -> yel; else -> Color.WHITE }
        val b = s?.batt ?: 0f
        val pinC = when { b >= 45 -> red; b >= 42 -> yel; else -> Color.WHITE }
        val ai = if (s == null || s.hz == 0) "dừng" else "${s.hz}/s"
        val aiC = if (s != null && s.state >= 2) yel else Color.WHITE
        val cells = listOf(Triple(may, "MÁY", mayC), Triple(if (b > 0) "${b.roundToInt()}°" else "--", "PIN", pinC),
            Triple(ai, if (s != null) "AI · ${s.backend} ${s.ms}ms" else "AI", aiC))
        val cw = w / cells.size
        p.textAlign = Paint.Align.CENTER
        cells.forEachIndexed { i, (v, l, c) ->
            p.color = c; p.textSize = h * 0.36f
            cv.drawText(v, cw * i + cw / 2, h * 0.52f, p)
            p.color = Color.argb(217, 255, 255, 255); p.textSize = h * 0.17f
            cv.drawText(l, cw * i + cw / 2, h * 0.8f, p)
        }
    }
}

/**
 * Ban do goc: nen OpenStreetMap (luu tam tren may, vung da di qua xem lai duoc khi mat mang) + vi tri GPS dien thoai.
 * Ban do xoay theo huong xe chay (mui ten luon chi len tren, nhu app dan duong). Cap nhat theo GPS (1 lan/giay).
 */
class TileMap(ctx: Context) : View(ctx) {
    /** Nguon nen: google (mac dinh, thu nghiem, dao mau thanh nen toi), osm. CARTO da doi khoa API nen bo. */
    var source = "google"
        set(v) { if (field != v) { field = v; mem.evictAll(); loading.clear(); err = ""; invalidate() } }
    private var err = ""
    private val zoom = 16
    private var lat = 0.0
    private var lon = 0.0
    private var bearing = 0f
    private var fix = false
    private val mem = LruCache<String, Bitmap>(48)
    private val loading = HashSet<String>()
    private val io = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val dir = File(ctx.cacheDir, "tiles").apply { mkdirs() }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = bold }
    private val clip = Path()
    // dao mau + xoay sac do 180 do: nen sang -> nen toi, giu mau duong/song gan dung
    private val dark = Paint(Paint.FILTER_BITMAP_FLAG).apply {
        colorFilter = ColorMatrixColorFilter(ColorMatrix(floatArrayOf(
            0.574f, -1.430f, -0.144f, 0f, 255f,
            -0.426f, -0.430f, -0.144f, 0f, 255f,
            -0.426f, -1.430f, 0.856f, 0f, 255f,
            0f, 0f, 0f, 1f, 0f)))
    }

    fun update(s: Snap) {
        if (!s.hasFix) { if (fix) { fix = false; invalidate() }; return }
        // chi lay huong khi xe dang chay (dung yen GPS bao huong lung tung)
        val b = if (s.vKmh > 5f) s.bearing else bearing
        if (s.lat == lat && s.lon == lon && b == bearing && fix) return
        lat = s.lat; lon = s.lon; bearing = b; fix = true
        invalidate()
    }

    override fun onDraw(cv: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        val rad = h * 0.08f
        clip.reset(); clip.addRoundRect(RectF(0f, 0f, w, h), rad, rad, Path.Direction.CW)
        cv.save(); cv.clipPath(clip)
        cv.drawColor(Color.rgb(21, 26, 34))
        if (fix) {
            val n = 1 shl zoom
            val fx = (lon + 180) / 360 * n
            val latR = Math.toRadians(lat)
            val fy = (1 - ln(tan(latR) + 1 / cos(latR)) / PI) / 2 * n
            val ts = 256f * (w / 300f).coerceIn(0.8f, 1.6f)
            val cx = w / 2; val cy = h * 0.58f
            // xoay ca ban do quanh vi tri xe: huong chay len tren -> can phu kin hinh tron ban kinh = duong cheo
            val rr = kotlin.math.hypot(maxOf(cx, w - cx).toDouble(), maxOf(cy, h - cy).toDouble()) / ts
            val tx0 = floor(fx - rr).toInt(); val tx1 = floor(fx + rr).toInt()
            val ty0 = floor(fy - rr).toInt(); val ty1 = floor(fy + rr).toInt()
            cv.save(); cv.rotate(-bearing, cx, cy)
            for (tx in tx0..tx1) for (ty in ty0..ty1) {
                if (ty < 0 || ty >= n) continue
                val bm = tile(((tx % n) + n) % n, ty) ?: continue
                val left = cx + ((tx - fx) * ts).toFloat(); val top = cy + ((ty - fy) * ts).toFloat()
                cv.drawBitmap(bm, null, RectF(left, top, left + ts, top + ts), if (source == "google") dark else null)
            }
            if (source != "google") cv.drawColor(Color.argb(70, 0, 0, 0))   // lam toi nhe cho de nhin ban dem
            drawRoute(cv, n, fx, fy, ts, cx, cy)
            cv.restore()
            // mui ten vi tri: luon chi len (ban do da xoay)
            cv.save(); cv.translate(cx, cy)
            val a = h * 0.07f
            val arrow = Path().apply { moveTo(0f, -a * 1.3f); lineTo(a, a); lineTo(0f, a * 0.45f); lineTo(-a, a); close() }
            p.style = Paint.Style.FILL; p.color = Color.rgb(71, 224, 138); cv.drawPath(arrow, p)
            p.style = Paint.Style.STROKE; p.strokeWidth = 3f; p.color = Color.WHITE; cv.drawPath(arrow, p)
            cv.restore()
        } else {
            p.style = Paint.Style.FILL; p.color = Color.argb(200, 255, 255, 255); p.textSize = h * 0.08f; p.textAlign = Paint.Align.CENTER
            cv.drawText("Đang chờ GPS…", w / 2, h / 2, p)
        }
        if (Nav.active) { drawBanner(cv, w, h); cv.restore(); drawFrame(cv, w, h, rad); return }
        // thanh tieu de
        p.style = Paint.Style.FILL; p.color = Color.argb(225, 8, 10, 14)
        cv.drawRect(0f, 0f, w, h * 0.15f, p)
        p.color = Color.WHITE; p.textSize = h * 0.075f; p.textAlign = Paint.Align.LEFT
        val credit = if (source == "osm") "© OpenStreetMap" else "© Google"
        cv.drawText(if (err.isNotEmpty()) "Lỗi tải bản đồ: $err" else "GPS điện thoại · $credit", h * 0.05f, h * 0.1f, p)
        cv.restore()
        drawFrame(cv, w, h, rad)
    }

    private fun drawFrame(cv: Canvas, w: Float, h: Float, rad: Float) {
        p.style = Paint.Style.STROKE; p.strokeWidth = 4f; p.color = Color.argb(230, 255, 255, 255)
        cv.drawRoundRect(RectF(2f, 2f, w - 2, h - 2), rad, rad, p)
    }

    // ---------------- dan duong: vach tuyen + tram o dich + bang chi dan ----------------
    private val routePath = Path()
    private val navListener: () -> Unit = { postInvalidate() }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); Nav.listeners += navListener }
    override fun onDetachedFromWindow() { Nav.listeners -= navListener; super.onDetachedFromWindow() }

    /** Ve trong canvas da xoay theo huong xe. n: so o o muc zoom; (fx, fy): vi tri xe theo don vi o. */
    private fun drawRoute(cv: Canvas, n: Int, fx: Double, fy: Double, ts: Float, cx: Float, cy: Float) {
        val d = Nav.dest ?: return
        fun px(lo: Double) = cx + (((lo + 180) / 360 * n) - fx).toFloat() * ts
        fun py(la: Double): Float {
            val r = Math.toRadians(la)
            return cy + (((1 - ln(tan(r) + 1 / cos(r)) / PI) / 2 * n) - fy).toFloat() * ts
        }
        val r = Nav.route
        if (r != null) {
            routePath.reset()
            // tu vi tri xe toi het tuyen (bo doan da di), bo bot diem qua sat nhau
            var i = r.cum.indexOfFirst { it >= Nav.along }.let { if (it <= 0) 0 else it - 1 }
            routePath.moveTo(cx, cy)
            var lx = cx; var ly = cy
            val lim = maxOf(width, height) * 3f
            while (i < r.lat.size) {
                val x = px(r.lon[i]); val y = py(r.lat[i])
                if ((x - lx) * (x - lx) + (y - ly) * (y - ly) > 9f || i == r.lat.size - 1) { routePath.lineTo(x, y); lx = x; ly = y }
                if (kotlin.math.abs(x - cx) > lim && kotlin.math.abs(y - cy) > lim) break
                i++
            }
            p.style = Paint.Style.STROKE; p.strokeCap = Paint.Cap.ROUND; p.strokeJoin = Paint.Join.ROUND
            p.strokeWidth = ts * 0.075f; p.color = Color.rgb(6, 40, 26); cv.drawPath(routePath, p)
            p.strokeWidth = ts * 0.05f; p.color = Color.rgb(74, 222, 128); cv.drawPath(routePath, p)
            p.strokeCap = Paint.Cap.BUTT
        }
        // bieu tuong o dich: tram sac (tia set) / diem den (co) - quay nguoc de luon dung thang
        val x = px(d.lon); val y = py(d.lat)
        val rr = height * 0.075f
        cv.save(); cv.rotate(bearing, x, y)
        p.style = Paint.Style.FILL; p.color = if (d.kind == "tramsac") Color.rgb(20, 184, 166) else Color.rgb(255, 84, 104)
        cv.drawCircle(x, y, rr, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = 3f; p.color = Color.WHITE; cv.drawCircle(x, y, rr, p)
        p.style = Paint.Style.FILL
        if (d.kind == "tramsac") {
            val b = Path().apply {
                moveTo(x + rr * 0.12f, y - rr * 0.62f); lineTo(x - rr * 0.38f, y + rr * 0.08f); lineTo(x - rr * 0.02f, y + rr * 0.08f)
                lineTo(x - rr * 0.14f, y + rr * 0.62f); lineTo(x + rr * 0.38f, y - rr * 0.08f); lineTo(x + rr * 0.02f, y - rr * 0.08f); close()
            }
            cv.drawPath(b, p)
        } else cv.drawCircle(x, y, rr * 0.3f, p)
        cv.restore()
    }

    /** Bang tren cung khi dan duong: mui ten re + khoang cach + ten duong; dong duoi: con lai / gio den. */
    private fun drawBanner(cv: Canvas, w: Float, h: Float) {
        val bh = h * 0.27f
        p.style = Paint.Style.FILL; p.color = Color.argb(235, 8, 10, 14)
        cv.drawRect(0f, 0f, w, bh, p)
        val r = Nav.route; val s = Nav.next()
        p.textAlign = Paint.Align.LEFT
        if (r == null && !Nav.arrived) {
            p.color = Color.WHITE; p.textSize = h * 0.075f
            cv.drawText(fit(Nav.msg, w - h * 0.1f), h * 0.05f, bh * 0.58f, p)
            return
        }
        val trn = if (Nav.arrived) 17 else s?.trn ?: 1
        val box = bh * 0.78f; val bx = h * 0.04f; val by = (bh - box) / 2
        p.color = Color.rgb(74, 222, 128); cv.drawRoundRect(RectF(bx, by, bx + box, by + box), box * 0.22f, box * 0.22f, p)
        drawTurn(cv, trn, bx + box / 2, by + box / 2, box * 0.36f, s?.exit ?: 0)
        val tx = bx + box + h * 0.04f
        val dm = Nav.nextDist()
        val big = when { Nav.arrived -> "Đã đến"; dm < 0 -> ""; dm < 1000 -> "${(dm / 10) * 10} m"; else -> "%.1f km".format(dm / 1000f).replace('.', ',') }
        p.color = Color.WHITE; p.textSize = bh * 0.42f; p.typeface = bold
        cv.drawText(big, tx, bh * 0.5f, p)
        val st = if (Nav.arrived || trn == 17) Nav.dest?.name ?: "" else s?.name?.ifEmpty { "" } ?: ""
        p.textSize = bh * 0.2f; p.color = Color.argb(235, 255, 255, 255)
        cv.drawText(fit(st, w - tx - h * 0.03f), tx, bh * 0.82f, p)
        // dong nho ngay duoi bang: con lai + gio den
        val rm = Nav.remainM()
        val info = (if (rm < 1000) "$rm m" else "%.1f km".format(rm / 1000f).replace('.', ',')) + " · ${(Nav.remainS() + 59) / 60} phút · đến ${Nav.eta()}"
        p.textSize = h * 0.06f
        val iw = p.measureText(info) + h * 0.06f
        p.color = Color.argb(200, 8, 10, 14)
        cv.drawRoundRect(RectF(w - iw - h * 0.03f, bh + h * 0.02f, w - h * 0.03f, bh + h * 0.11f), h * 0.04f, h * 0.04f, p)
        p.color = Color.WHITE; p.textAlign = Paint.Align.RIGHT
        cv.drawText(info, w - h * 0.06f, bh + h * 0.087f, p)
        p.textAlign = Paint.Align.LEFT
    }

    private fun fit(t: String, maxW: Float): String {
        if (p.measureText(t) <= maxW) return t
        var k = t.length
        while (k > 1 && p.measureText(t.take(k) + "…") > maxW) k--
        return t.take(k) + "…"
    }

    /** Mui ten re don gian ve bang net (mau toi tren o xanh). */
    private fun drawTurn(cv: Canvas, trn: Int, x: Float, y: Float, s: Float, exit: Int) {
        p.style = Paint.Style.STROKE; p.strokeWidth = s * 0.32f; p.strokeCap = Paint.Cap.ROUND; p.strokeJoin = Paint.Join.ROUND
        p.color = Color.rgb(6, 30, 20)
        val path = Path()
        if (trn == 17) {                                     // toi noi: co
            path.moveTo(x - s * 0.5f, y + s); path.lineTo(x - s * 0.5f, y - s)
            cv.drawPath(path, p)
            p.style = Paint.Style.FILL
            cv.drawPath(Path().apply { moveTo(x - s * 0.5f, y - s); lineTo(x + s * 0.8f, y - s * 0.55f); lineTo(x - s * 0.5f, y - s * 0.1f); close() }, p)
            p.strokeCap = Paint.Cap.BUTT; return
        }
        if (trn in 10..12 || trn == 19 || trn == 20) {         // vong xuyen + so loi ra
            p.strokeWidth = s * 0.22f
            cv.drawCircle(x, y - s * 0.15f, s * 0.5f, p)
            path.moveTo(x, y + s); path.lineTo(x, y + s * 0.35f); cv.drawPath(path, p)
            if (exit > 0) { p.style = Paint.Style.FILL; p.textAlign = Paint.Align.CENTER; p.textSize = s * 0.8f; cv.drawText("$exit", x, y + s * 0.13f, p); p.textAlign = Paint.Align.LEFT }
            p.strokeCap = Paint.Cap.BUTT; return
        }
        val deg = when (trn) { 2 -> -90f; 3 -> 90f; 4, 13, 15 -> -45f; 5, 14, 16 -> 45f; 6 -> -135f; 7 -> 135f; else -> 0f }
        if (trn == 8 || trn == 9) {                          // quay dau
            path.moveTo(x + s * 0.45f, y + s); path.lineTo(x + s * 0.45f, y - s * 0.2f)
            path.arcTo(RectF(x - s * 0.45f, y - s * 0.65f, x + s * 0.45f, y + s * 0.25f), 0f, -180f)
            path.lineTo(x - s * 0.45f, y + s * 0.3f)
            cv.drawPath(path, p)
            arrowHead(cv, x - s * 0.45f, y + s * 0.55f, 180f, s)
        } else {
            path.moveTo(x, y + s); path.lineTo(x, y)
            val rad = Math.toRadians(deg.toDouble())
            val ex = x + (kotlin.math.sin(rad) * s * 0.75).toFloat(); val ey = y - (kotlin.math.cos(rad) * s * 0.75).toFloat()
            path.lineTo(ex, ey)
            cv.drawPath(path, p)
            arrowHead(cv, ex, ey, deg, s)
        }
        p.strokeCap = Paint.Cap.BUTT
    }

    private fun arrowHead(cv: Canvas, x: Float, y: Float, deg: Float, s: Float) {
        cv.save(); cv.rotate(deg, x, y)
        p.style = Paint.Style.FILL
        cv.drawPath(Path().apply { moveTo(x, y - s * 0.55f); lineTo(x + s * 0.45f, y + s * 0.05f); lineTo(x - s * 0.45f, y + s * 0.05f); close() }, p)
        p.style = Paint.Style.STROKE
        cv.restore()
    }

    private val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager

    private fun inetNet(): android.net.Network? {
        fun ok(n: android.net.Network?) = n != null && cm.getNetworkCapabilities(n)?.hasCapability(android.net.NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true
        val a = cm.activeNetwork
        if (ok(a)) return a
        @Suppress("DEPRECATION")
        return cm.allNetworks.firstOrNull { ok(it) }
    }

    private fun tile(x: Int, y: Int): Bitmap? {
        val src = source
        val k = "$zoom/$x/$y"
        mem.get(k)?.let { return it }
        if (loading.add(k)) io.execute {
            val f = File(dir, "${src}_${zoom}_${x}_$y.png")
            val url = when (src) {
                "google" -> "https://mt1.google.com/vt/lyrs=m&hl=vi&x=$x&y=$y&z=$zoom"
                else -> "https://tile.openstreetmap.org/$k.png"
            }
            var e = ""
            var bm: Bitmap? = null
            try {
                if (f.exists()) bm = BitmapFactory.decodeFile(f.absolutePath)
                if (bm == null) {
                    // dang noi Wi-Fi cam guong (khong co Internet): tai qua mang co Internet that (4G/5G)
                    val c = (inetNet()?.openConnection(URL(url)) ?: URL(url).openConnection()) as HttpURLConnection
                    c.setRequestProperty("User-Agent", "EVCarHUD-Lan/0.1 (Android)")
                    c.connectTimeout = 8000; c.readTimeout = 8000
                    val code = c.responseCode
                    if (code != 200) e = "mã $code"
                    if (code == 200) {
                        val bytes = c.inputStream.use { it.readBytes() }
                        f.writeBytes(bytes)
                        bm = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    }
                    c.disconnect()
                }
            } catch (ex: Exception) { e = ex.javaClass.simpleName }
            main.post {
                loading.remove(k)
                if (src != source) return@post
                err = if (bm != null) "" else e
                if (bm != null) mem.put(k, bm)
                invalidate()
            }
        }
        return null
    }
}

/**
 * Keo tha cua so noi; tha tay thi hit vao goc gan nhat (khong de len hang thong so tren cung).
 * topMin: toa do y thap nhat cho phep (px).
 */
@SuppressLint("ClickableViewAccessibility")
fun makeDraggable(v: View, topMin: () -> Float, pad: Float, corner: Int, onCorner: (Int) -> Unit) {
    fun place(c: Int) {
        val par = v.parent as? ViewGroup ?: return
        val left = c % 2 == 0; val topSide = c < 2
        v.x = if (left) pad else par.width - v.width - pad
        v.y = if (topSide) topMin() else par.height - v.height - pad
    }
    v.post { place(corner) }
    var dx = 0f; var dy = 0f
    v.setOnTouchListener { _, e ->
        val par = v.parent as ViewGroup
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { dx = v.x - e.rawX; dy = v.y - e.rawY; true }
            MotionEvent.ACTION_MOVE -> {
                v.x = (e.rawX + dx).coerceIn(0f, (par.width - v.width).toFloat())
                v.y = (e.rawY + dy).coerceIn(topMin(), (par.height - v.height).toFloat())
                true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val c = (if (v.x + v.width / 2 < par.width / 2) 0 else 1) + (if (v.y + v.height / 2 < par.height / 2) 0 else 2)
                v.animate().x(if (c % 2 == 0) pad else par.width - v.width - pad)
                    .y(if (c < 2) topMin() else par.height - v.height - pad).setDuration(160).start()
                onCorner(c)
                true
            }
            else -> false
        }
    }
}
