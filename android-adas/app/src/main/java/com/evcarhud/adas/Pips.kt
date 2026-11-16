package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
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
 * Huong Bac len tren, mui ten = huong xe chay. Cap nhat theo GPS (1 lan/giay).
 */
class TileMap(ctx: Context) : View(ctx) {
    private val zoom = 16
    private var lat = 0.0
    private var lon = 0.0
    private var bearing = 0f
    private var fix = false
    private val mem = LruCache<String, Bitmap>(24)
    private val loading = HashSet<String>()
    private val io = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val dir = File(ctx.cacheDir, "tiles").apply { mkdirs() }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = bold }
    private val clip = Path()

    fun update(s: Snap) {
        if (!s.hasFix) { if (fix) { fix = false; invalidate() }; return }
        if (s.lat == lat && s.lon == lon && s.bearing == bearing && fix) return
        lat = s.lat; lon = s.lon; bearing = s.bearing; fix = true
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
            val tx0 = floor(fx - cx / ts).toInt(); val tx1 = floor(fx + (w - cx) / ts).toInt()
            val ty0 = floor(fy - cy / ts).toInt(); val ty1 = floor(fy + (h - cy) / ts).toInt()
            for (tx in tx0..tx1) for (ty in ty0..ty1) {
                if (ty < 0 || ty >= n) continue
                val bm = tile(((tx % n) + n) % n, ty) ?: continue
                val left = cx + ((tx - fx) * ts).toFloat(); val top = cy + ((ty - fy) * ts).toFloat()
                cv.drawBitmap(bm, null, RectF(left, top, left + ts, top + ts), null)
            }
            // lam toi nhe cho de nhin ban dem
            cv.drawColor(Color.argb(70, 0, 0, 0))
            // mui ten vi tri
            cv.save(); cv.translate(cx, cy); cv.rotate(bearing)
            val a = h * 0.07f
            val arrow = Path().apply { moveTo(0f, -a * 1.3f); lineTo(a, a); lineTo(0f, a * 0.45f); lineTo(-a, a); close() }
            p.style = Paint.Style.FILL; p.color = Color.rgb(71, 224, 138); cv.drawPath(arrow, p)
            p.style = Paint.Style.STROKE; p.strokeWidth = 3f; p.color = Color.WHITE; cv.drawPath(arrow, p)
            cv.restore()
        } else {
            p.style = Paint.Style.FILL; p.color = Color.argb(200, 255, 255, 255); p.textSize = h * 0.08f; p.textAlign = Paint.Align.CENTER
            cv.drawText("Đang chờ GPS…", w / 2, h / 2, p)
        }
        // thanh tieu de
        p.style = Paint.Style.FILL; p.color = Color.argb(225, 8, 10, 14)
        cv.drawRect(0f, 0f, w, h * 0.15f, p)
        p.color = Color.WHITE; p.textSize = h * 0.075f; p.textAlign = Paint.Align.LEFT
        cv.drawText("GPS điện thoại · © OpenStreetMap", h * 0.05f, h * 0.1f, p)
        cv.restore()
        p.style = Paint.Style.STROKE; p.strokeWidth = 4f; p.color = Color.argb(230, 255, 255, 255)
        cv.drawRoundRect(RectF(2f, 2f, w - 2, h - 2), rad, rad, p)
    }

    private fun tile(x: Int, y: Int): Bitmap? {
        val k = "$zoom/$x/$y"
        mem.get(k)?.let { return it }
        if (loading.add(k)) io.execute {
            val f = File(dir, "${zoom}_${x}_$y.png")
            var bm: Bitmap? = null
            try {
                if (f.exists()) bm = BitmapFactory.decodeFile(f.absolutePath)
                if (bm == null) {
                    val c = URL("https://tile.openstreetmap.org/$k.png").openConnection() as HttpURLConnection
                    c.setRequestProperty("User-Agent", "EVCarHUD-Lan/0.1 (Android)")
                    c.connectTimeout = 8000; c.readTimeout = 8000
                    if (c.responseCode == 200) {
                        val bytes = c.inputStream.use { it.readBytes() }
                        f.writeBytes(bytes)
                        bm = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    }
                    c.disconnect()
                }
            } catch (_: Exception) {}
            main.post {
                loading.remove(k)
                if (bm != null) { mem.put(k, bm); invalidate() }
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
