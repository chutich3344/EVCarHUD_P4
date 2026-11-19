package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Lop ve tren camera (kieu C, camera toan man):
 *  - duong di openpilot (xanh / vang khi gan hon khoang cach an toan / do khi TTC < 2,5 s), vach lan, mep duong,
 *    tam giac nho duoi xe phia truoc (khong ghi so tren xe)
 *  - hang tren cung: o XE TRUOC, o LECH LAN | toc do giua | chip HUD, LAN, nut cai dat ben phai
 *  - lan lan: vach do doc chop o mep bi lan; nguy hiem: vien do; PHANH: phu do toan man
 *  - che do hieu chinh: keo dau + ve diem tu
 */
class Overlay(ctx: Context) : View(ctx) {
    var snap: Snap? = null
        set(v) { field = v; invalidate() }
    /** Anh camera tu ve (640x360). */
    var frame: android.graphics.Bitmap? = null
        set(v) { field = v; invalidate() }
    private val dst = RectF()
    var imgW = 1280
    var imgH = 720
    var onGear: () -> Unit = {}
    var onCalibDone: (Boolean) -> Unit = {}

    // hieu chinh
    var calib = false
    /** Ve duong di/vach de len taplo (keo xuong day man). */
    var overHood = true
        set(v) { field = v; invalidate() }
    var vpX = 0.5f
    var vpY = 0.5f

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    /** Font Be Vietnam Pro (OFL, thiet ke cho tieng Viet) thay font he thong. */
    private val fBold = try { Typeface.createFromAsset(ctx.assets, "fonts/BeVietnamPro-Bold.ttf") } catch (_: Exception) { Typeface.create(Typeface.DEFAULT, Typeface.BOLD) }
    private val fSemi = try { Typeface.createFromAsset(ctx.assets, "fonts/BeVietnamPro-SemiBold.ttf") } catch (_: Exception) { fBold }
    private val txt = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; typeface = fSemi }
    private var kindTxt: String? = null
    /** Vung bi o noi che (ban do goc) de cot bien khong ve de len; MainActivity gan. */
    var avoid: () -> RectF? = { null }
    /** Bao vi tri xe truoc (chuan hoa theo anh) cho bo nhan loai xe. */
    var onLead: (Float, Float, Float) -> Unit = { _, _, _ -> }
    private var kindAt = 0L
    private val path = Path()
    private val bmpPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val pt = FloatArray(4)
    private val gearRect = RectF()
    private val btnOk = RectF()
    private val btnNo = RectF()

    private var s = 1f
    private var ox = 0f
    private var oy = 0f

    private fun layoutScale() {
        s = max(width / imgW.toFloat(), height / imgH.toFloat())
        ox = (width - imgW * s) / 2
        oy = (height - imgH * s) / 2
    }

    private fun proj(c: Op.Cam, x: Float, y: Float, z: Float, o: Int): Boolean {
        if (!c.project(x, y, z, pt, o)) return false
        pt[o] = ox + pt[o] * s
        pt[o + 1] = oy + pt[o + 1] * s
        return true
    }

    override fun onDraw(cv: Canvas) {
        snap?.cam?.let { if (it.w > 0) { imgW = it.w; imgH = it.h } }   // cam guong: kich thuoc biet sau khi luong len
        layoutScale()
        val u = width / 100f
        frame?.let { dst.set(ox, oy, ox + imgW * s, oy + imgH * s); cv.drawBitmap(it, null, dst, bmpPaint) }
        val sn = snap
        val o = sn?.out
        val c = sn?.cam
        if (o != null && c != null && !calib) {
            // tren mep taplo: ve ro; duoi mep (taplo/capo che duong gan): keo tiep xuong day man nhung mo hon
            // (nhu DriveDeck, de liec nhanh) - tat trong Cai dat thi cat han o mep taplo
            val hy = oy + sn.hood * imgH * s
            cv.save(); cv.clipRect(0f, 0f, width.toFloat(), hy)
            drawScene(cv, sn, o, c)
            cv.restore()
            if (overHood && hy < height) {
                val sc = cv.saveLayerAlpha(0f, hy, width.toFloat(), height.toFloat(), 120)
                cv.clipRect(0f, hy, width.toFloat(), height.toFloat())
                drawScene(cv, sn, o, c)
                cv.restoreToCount(sc)
            }
        }
        if (sn != null && !calib && sn.level >= 3) {
            fill.color = Color.argb(150, 220, 30, 40); cv.drawRect(0f, 0f, width.toFloat(), height.toFloat(), fill)
            txt.textSize = 14 * u; txt.textAlign = Paint.Align.CENTER; txt.color = Color.WHITE
            cv.drawText("PHANH!", width / 2f, height / 2f + 5 * u, txt)
        }
        drawTop(cv, sn, u)
        if (sn != null && !calib) drawSigns(cv, sn, u)
        sn?.dbg?.let { drawDbg(cv, it, u) }
        if (sn != null && !calib) drawWarn(cv, sn, u)
        if (calib) { drawCalib(cv, u); if (sn != null && sn.msg.isNotEmpty()) pill(cv, sn.msg, width / 2f, 17.5f * u, u) }
    }

    private fun levelColor(l: Int, a: Int = 255) = when {
        l >= 2 -> Color.argb(a, 240, 60, 60)
        l == 1 -> Color.argb(a, 245, 200, 40)
        else -> Color.argb(a, 60, 200, 90)
    }

    private fun drawScene(cv: Canvas, sn: Snap, o: Op.Out, c: Op.Cam) {
        val d = resources.displayMetrics.density
        // duong di
        if (o.planOk) {
            val col = levelColor(sn.level)
            for (k in 0 until Op.N - 1) {
                val x0 = o.planX[k]; val x1 = o.planX[k + 1]
                if (x0 < 1f || x1 > 120f) continue
                val z0 = o.planZ[k] + sn.camH; val z1 = o.planZ[k + 1] + sn.camH
                if (!proj(c, x0, o.planY[k] - 0.9f, z0, 0) || !proj(c, x0, o.planY[k] + 0.9f, z0, 2)) continue
                val a0 = pt[0]; val b0 = pt[1]; val a1 = pt[2]; val b1 = pt[3]
                if (!proj(c, x1, o.planY[k + 1] + 0.9f, z1, 0) || !proj(c, x1, o.planY[k + 1] - 0.9f, z1, 2)) continue
                path.reset(); path.moveTo(a0, b0); path.lineTo(a1, b1); path.lineTo(pt[0], pt[1]); path.lineTo(pt[2], pt[3]); path.close()
                val alpha = (0.6f * (1 - min(1f, x0 / 90f)) * 255).roundToInt()
                fill.color = (col and 0x00ffffff) or (alpha shl 24)
                cv.drawPath(path, fill)
            }
        }
        // vach lan + mep duong
        for (i in 0..3) {
            val p = o.laneProb[i]
            if (p < 0.15f) continue
            val inner = i == 1 || i == 2
            val g = if (inner) (150 + 105 * p).roundToInt() else (160 * p).roundToInt()
            line.color = Color.argb(if (inner) 235 else 170, g, g, g)
            line.strokeWidth = (if (inner) 3f else 2f) * d
            polyline(cv, c, o.laneY[i], o.laneZ[i])
        }
        line.color = Color.argb(220, 230, 40, 40); line.strokeWidth = 2f * d
        for (i in 0..1) polyline(cv, c, o.edgeY[i], o.edgeZ[i])
        // xe phia truoc: tam giac nho duoi xe
        val lx = sn.leadM
        if (lx != null) {
            var zg = 0f
            for (k in 0 until Op.N - 1) if (Op.X_IDXS[k + 1] >= lx) {
                val t = (lx - Op.X_IDXS[k]) / (Op.X_IDXS[k + 1] - Op.X_IDXS[k])
                val zk = (o.laneZ[1][k] + o.laneZ[2][k]) / 2; val zk1 = (o.laneZ[1][k + 1] + o.laneZ[2][k + 1]) / 2
                zg = zk + (zk1 - zk) * t; break
            }
            if (proj(c, lx, o.leadY - 0.9f, zg, 0) && proj(c, lx, o.leadY + 0.9f, zg, 2)) {
                val cx = (pt[0] + pt[2]) / 2; val yb = (pt[1] + pt[3]) / 2
                val w = max(10f, abs(pt[2] - pt[0]) * 0.45f)
                matchKind(sn, cx, yb, abs(pt[2] - pt[0]))
                if (imgW > 0 && s > 0) onLead((cx - ox) / (imgW * s), (yb - oy) / (imgH * s), abs(pt[2] - pt[0]) / (imgW * s))
                path.reset(); path.moveTo(cx, yb - w * 0.9f); path.lineTo(cx - w, yb + 2); path.lineTo(cx + w, yb + 2); path.close()
                fill.color = when { sn.level >= 2 -> Color.rgb(240, 60, 60); sn.level == 1 -> Color.rgb(255, 210, 40); else -> Color.rgb(235, 235, 235) }
                cv.drawPath(path, fill)
            }
        }
    }

    private fun polyline(cv: Canvas, c: Op.Cam, ys: FloatArray, zs: FloatArray) {
        path.reset()
        var started = false
        for (k in 0 until Op.N) {
            if (Op.X_IDXS[k] > 80f) break
            if (!proj(c, Op.X_IDXS[k], ys[k], zs[k], 0)) continue
            if (!started) { path.moveTo(pt[0], pt[1]); started = true } else path.lineTo(pt[0], pt[1])
        }
        if (started) cv.drawPath(path, line)
    }

    private fun box(cv: Canvas, x: Float, y: Float, u: Float, value: String, label: String, valColor: Int): Float {
        txt.textAlign = Paint.Align.CENTER
        txt.textSize = 2.2f * u
        val vw = txt.measureText(value)
        txt.textSize = 0.9f * u
        val lw = txt.measureText(label)
        val w = max(7f * u, max(vw, lw) + 1.8f * u)
        val h = 4.6f * u
        val r = RectF(x, y, x + w, y + h)
        fill.color = Color.argb(107, 18, 24, 32); cv.drawRoundRect(r, u, u, fill)
        line.color = Color.argb(72, 255, 255, 255); line.strokeWidth = 0.12f * u; cv.drawRoundRect(r, u, u, line)
        txt.color = valColor; txt.textSize = 2.2f * u
        cv.drawText(value, x + w / 2, y + 2.65f * u, txt)
        txt.color = Color.argb(217, 255, 255, 255); txt.textSize = 0.9f * u
        cv.drawText(label, x + w / 2, y + 3.95f * u, txt)
        return w
    }

    private fun chip(cv: Canvas, xRight: Float, y: Float, u: Float, label: String, dot: Int?, bg: Int): Float {
        txt.textSize = 1.25f * u; txt.textAlign = Paint.Align.LEFT
        val tw = txt.measureText(label)
        val w = tw + (if (dot != null) 2.6f else 1.6f) * u
        val r = RectF(xRight - w, y, xRight, y + 2.6f * u)
        fill.color = bg; cv.drawRoundRect(r, 1.3f * u, 1.3f * u, fill)
        var tx = r.left + 0.8f * u
        if (dot != null) { fill.color = dot; cv.drawCircle(tx + 0.4f * u, r.centerY(), 0.45f * u, fill); tx += 1.1f * u }
        txt.color = Color.WHITE
        cv.drawText(label, tx, r.centerY() + 0.45f * u, txt)
        return w
    }

    /** Loai xe truoc: hop YOLOX co day gan diem xe truoc (openpilot) nhat; giu 2 s cho khoi nhay. */
    private fun matchKind(sn: Snap, cx: Float, yb: Float, laneW: Float) {
        val now = SystemClock.elapsedRealtime()
        var best: SignDet.Obj? = null; var bd = Float.MAX_VALUE
        for (o in sn.objs ?: emptyList()) {
            val x1 = ox + o.x1 * imgW * s; val x2 = ox + o.x2 * imgW * s; val y2 = oy + o.y2 * imgH * s; val bh = (o.y2 - o.y1) * imgH * s
            val m = max(0.15f * (x2 - x1), 0.2f * laneW)
            if (cx < x1 - m || cx > x2 + m || abs(y2 - yb) > max(0.35f * bh, 0.25f * laneW + 6f)) continue
            val d = abs((x1 + x2) / 2 - cx) + abs(y2 - yb) + (if (o.cls == 0) 10_000f else 0f)   // uu tien xe hon nguoi (nguoi ngoi tren xe may)
            if (d < bd) { bd = d; best = o }
        }
        val b = best ?: return
        // kiem tra kich thuoc that: laneW = 1,8 m o khoang cach xe truoc -> xe buyt/tai phai cao >= 2,3 m
        val hM = (b.y2 - b.y1) * imgH * s / max(1f, laneW / 1.8f)
        // nguoi lam xe truoc (openpilot chi bat vat dang di tren lan) = nguoi ngoi xe may
        val cls = if ((b.cls == 5 || b.cls == 7) && hM < 2.3f) 2 else if (b.cls == 0) 3 else b.cls
        kindTxt = SignDet.kind(cls); kindAt = now
    }

    private fun drawTop(cv: Canvas, sn: Snap?, u: Float) {
        val top = 2.2f * u
        // goc trai kieu clip demo: so lon = khoang cach xe truoc, duoi la toc do/lech lan va dong trang thai
        val x = 2f * u
        val lead = sn?.leadM
        val red = Color.rgb(255, 84, 104); val yel = Color.rgb(255, 210, 63)
        val leadCol = when { sn == null || lead == null -> Color.WHITE; sn.level >= 2 -> red; sn.level == 1 -> yel; else -> Color.rgb(71, 224, 138) }
        txt.textAlign = Paint.Align.LEFT
        txt.setShadowLayer(0.8f * u, 0f, 0.2f * u, Color.argb(170, 0, 0, 0))
        txt.color = leadCol; txt.textSize = 6.6f * u; txt.typeface = fBold
        cv.drawText(if (lead != null && !calib) "${lead.roundToInt()}" else "--", x, top + 5.6f * u, txt)
        txt.typeface = fSemi; txt.textSize = 1.5f * u; txt.color = Color.WHITE
        val kind = if (lead != null && SystemClock.elapsedRealtime() - kindAt < 4000) kindTxt else null
        val sub = when {
            sn != null && sn.state == 1 && lead == null -> "m · xe đỗ, AI tạm nghỉ"
            sn != null && sn.state == 3 && lead == null -> "m · máy nóng, AI tạm nghỉ"
            lead == null || calib -> "m · chưa thấy xe trước"
            sn.level >= 1 -> "m · ${kind ?: "tối thiểu"} ${if (kind != null) "· tối thiểu " else ""}${Op.safeDist(sn.vKmh).roundToInt()} m" + (sn.ttc?.let { " · %.1f s".format(it) } ?: "")
            else -> "m · ${kind ?: "xe trước"}"
        }
        cv.drawText(sub, x, top + 7.6f * u, txt)
        val off = sn?.offCm
        val offTxt = when { off == null || calib -> "lệch làn --"; abs(off) < 5 -> "giữa làn"; off > 0 -> "lệch trái $off cm"; else -> "lệch phải ${-off} cm" }
        txt.color = if (sn != null && sn.ldw != '-') red else Color.rgb(120, 200, 255)
        cv.drawText("${sn?.vKmh?.roundToInt() ?: 0} km/h · $offTxt", x, top + 9.6f * u, txt)
        txt.clearShadowLayer()
        val st = sn?.stat ?: ""
        if (st.isNotEmpty()) {
            txt.textSize = 1.2f * u
            val w = minOf(txt.measureText(st), width * 0.6f) + 1.4f * u
            fill.color = Color.argb(130, 0, 0, 0)
            cv.drawRoundRect(RectF(x, top + 10.4f * u, x + w, top + 12.3f * u), 0.8f * u, 0.8f * u, fill)
            txt.color = Color.argb(235, 255, 255, 255)
            cv.drawText(st, x + 0.7f * u, top + 11.75f * u, txt)
        }

        // chip ben phai: [cai dat] [LAN] [HUD]
        var xr = width - 2f * u
        txt.textSize = 1.25f * u
        val gw = 2.6f * u
        gearRect.set(xr - gw, top, xr, top + 2.6f * u)
        fill.color = Color.argb(140, 18, 24, 32); cv.drawRoundRect(gearRect, 1.3f * u, 1.3f * u, fill)
        txt.textAlign = Paint.Align.CENTER; txt.color = Color.WHITE; txt.textSize = 1.6f * u
        cv.drawText("⚙", gearRect.centerX(), gearRect.centerY() + 0.55f * u, txt)
        xr -= gw + 0.6f * u
        val bothLanes = sn?.out?.let { it.laneProb[1] > 0.5f && it.laneProb[2] > 0.5f } ?: false
        val lanBg = when { sn != null && sn.ldw != '-' -> Color.argb(220, 220, 50, 60); bothLanes -> Color.argb(200, 30, 158, 90); else -> Color.argb(140, 90, 96, 104) }
        xr -= chip(cv, xr, top, u, "LÀN", null, lanBg) + 0.6f * u
        val hudOk = sn?.hudOk == true
        chip(cv, xr, top, u, "HUD", if (hudOk) Color.rgb(71, 224, 138) else Color.rgb(150, 150, 150), Color.argb(140, 18, 24, 32))
    }

    /** Hinh bien mau (assets/bien/<lop>.png, ve tu SVG MIT @road-signs/vn); 17 = khung bien toc do trong. */
    private val icons = HashMap<Int, Bitmap?>()
    private fun icon(cls: Int): Bitmap? = icons.getOrPut(cls) {
        try { context.assets.open("bien/$cls.png").use { android.graphics.BitmapFactory.decodeStream(it) } } catch (_: Exception) { null }
    }

    /** Ve bien toc do: khung mau + so (chu dam, hep nhu bien that). */
    private fun drawSpeed(cv: Canvas, v: Int, cx: Float, cy: Float, d: Float) {
        val b = icon(SignDet.SPEED)
        if (b != null) cv.drawBitmap(b, null, RectF(cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2), bmpPaint)
        else { fill.color = Color.WHITE; cv.drawCircle(cx, cy, d / 2, fill)
            line.color = Color.rgb(240, 10, 10); line.strokeWidth = d * 0.14f; cv.drawCircle(cx, cy, d * 0.43f, line) }
        val t = "$v"
        txt.textAlign = Paint.Align.CENTER; txt.color = Color.BLACK; txt.textSize = d * 0.52f; txt.typeface = fBold
        txt.textScaleX = 1f; val w = txt.measureText(t); val maxW = d * (if (t.length >= 3) 0.6f else 0.5f)
        if (w > maxW) txt.textScaleX = maxW / w
        cv.drawText(t, cx, cy + d * 0.185f, txt); txt.textScaleX = 1f; txt.typeface = fSemi
    }

    /** Cot bien ben phai (kieu PiP VietMap): bien toc do / bien vua qua (hinh mau) + bien phu (anh that). */
    private fun drawSigns(cv: Canvas, sn: Snap, u: Float) {
        // cot doc ben phai, to hon ban dau (11u ngang); thieu cho (ban do goc che) thi thu nho dan, toi thieu bo the camera roi the bien
        val x1 = width - 2f * u; val top = 2.2f * u + 3.4f * u
        val lim = sn.limit
        val sg0 = sn.sign?.takeIf { !(it.cls == SignDet.SPEED && it.value == lim) }
        val av = avoid()
        val bottom = if (av != null && av.left < x1 && av.right > x1 - 11f * u && av.top > top) av.top - 0.6f * u else height - 2f * u
        fun need(sg: SignDet.Hit?, cam: Boolean): Float = (if (lim != null) 7.5f else 0f) +
            (if (sg != null) 7.8f + (if (sg.aux != null) 2.8f else 0f) + 0.5f else 0f) + (if (cam) 3.6f else 0f)
        var sg = sg0; var cam = sn.camType != null
        var q = minOf(1.4f * u, (bottom - top) / maxOf(1f, need(sg, cam)))
        if (q < 0.85f * u && cam) { cam = false; q = minOf(1.4f * u, (bottom - top) / maxOf(1f, need(sg, cam))) }
        if (q < 0.85f * u && sg != null) { sg = null; q = minOf(1.4f * u, (bottom - top) / maxOf(1f, need(sg, cam))) }
        if (q < 0.7f * u) return
        val cw = 8f * q; val x0 = x1 - cw
        var y = top
        if (lim != null) {
            val h = 7f * q
            fill.color = Color.argb(170, 10, 14, 22); cv.drawRoundRect(RectF(x0, y, x1, y + h), 1.2f * q, 1.2f * q, fill)
            val cx = (x0 + x1) / 2
            drawSpeed(cv, lim, cx, y + 3.3f * q, 5.6f * q)
            txt.textAlign = Paint.Align.CENTER; txt.textSize = 1.05f * q; txt.color = if (sn.over) Color.rgb(255, 84, 104) else Color.WHITE
            val why = sn.limitWhy
            if (why != null && !sn.over) { txt.textSize = 0.85f * q; txt.color = Color.rgb(170, 205, 255) }
            val cap = if (sn.over) "VƯỢT TỐC ĐỘ" else if (why != null) "giới hạn tốc độ" else "tốc độ tối đa"
            cv.drawText(cap, cx, y + h - 0.45f * q, txt)
            y += h + 0.5f * q
        }
        if (sg != null) y = drawSignCard(cv, sg, x0, x1, y, q) + 0.5f * q
        if (cam) drawCam(cv, sn, x0, x1, y, q)
    }

    /** The camera (tu HUD: Waze/GOFA): cham cam + ten + met con lai. */
    private fun drawCam(cv: Canvas, sn: Snap, x0: Float, x1: Float, y: Float, u: Float) {
        val t = sn.camType ?: return
        val h = 3.6f * u
        fill.color = Color.argb(170, 10, 14, 22); cv.drawRoundRect(RectF(x0, y, x1, y + h), 1.2f * u, 1.2f * u, fill)
        val r = 1.2f * u; val cx = x0 + 0.7f * u + r; val cy = y + h / 2
        fill.color = Color.rgb(255, 138, 61); cv.drawCircle(cx, cy, r, fill)
        txt.textAlign = Paint.Align.CENTER; txt.color = Color.rgb(20, 20, 20); txt.textSize = 0.65f * u; txt.typeface = fBold
        cv.drawText("CAM", cx, cy + 0.23f * u, txt); txt.typeface = fSemi
        txt.textAlign = Paint.Align.LEFT; txt.color = Color.WHITE; txt.textSize = 0.8f * u
        val tx = cx + r + 0.5f * u
        cv.drawText(CamDb.name(t), tx, y + 1.45f * u, txt)
        txt.color = Color.rgb(255, 138, 61); txt.textSize = 1.2f * u; txt.typeface = fBold
        cv.drawText(sn.camM?.let { "$it m" } ?: "phía trước", tx, y + 2.95f * u, txt); txt.typeface = fSemi
    }

    /** The bien vua qua (hinh mau) + bien phu; tra ve day the. */
    private fun drawSignCard(cv: Canvas, sg: SignDet.Hit, x0: Float, x1: Float, y: Float, u: Float): Float {
        val cw = x1 - x0
        val img = 5.4f * u
        val auxH = sg.aux?.let { minOf(2.6f * u, (cw - 1f * u) * it.height / it.width) } ?: 0f
        val h = img + auxH + 2.4f * u
        fill.color = Color.argb(170, 10, 14, 22); cv.drawRoundRect(RectF(x0, y, x1, y + h), 1.2f * u, 1.2f * u, fill)
        val cx = (x0 + x1) / 2
        val ic = if (sg.cls == SignDet.SPEED) null else icon(sg.cls)
        val bh: Float
        if (sg.cls == SignDet.SPEED && sg.value != null) { drawSpeed(cv, sg.value, cx, y + 0.5f * u + img / 2, img); bh = img }
        else { val b = ic ?: sg.crop; val k = minOf(img / b.height, (cw - 1f * u) / b.width)
            val bw = b.width * k; bh = b.height * k
            cv.drawBitmap(b, null, RectF(cx - bw / 2, y + 0.5f * u, cx + bw / 2, y + 0.5f * u + bh), bmpPaint) }
        var yy = y + 0.5f * u + bh + 0.3f * u
        sg.aux?.let { a -> val ak = minOf(auxH / a.height, (cw - 1f * u) / a.width); val aw = a.width * ak; val ah = a.height * ak
            cv.drawBitmap(a, null, RectF(cx - aw / 2, yy, cx + aw / 2, yy + ah), bmpPaint); yy += ah + 0.2f * u }
        txt.textAlign = Paint.Align.CENTER; txt.color = Color.rgb(255, 210, 63); txt.textSize = 0.95f * u
        val name = SignDet.NAME[sg.cls]
        cv.drawText(if (txt.measureText(name) > cw - 0.6f * u) name.take(16) + "…" else name, cx, minOf(yy + 1.2f * u, y + h - 0.4f * u), txt)
        return y + h
    }

    /** Bang so lieu chan doan trong suot o mep trai (de chup man hinh khi chay thu). */
    private fun drawDbg(cv: Canvas, s: String, u: Float) {
        txt.textAlign = Paint.Align.LEFT; txt.textSize = 1.15f * u
        val maxW = width * 0.42f
        val lines = ArrayList<String>()
        for (raw in s.split("\n")) {
            var cur = ""
            for (part in raw.split(" · ")) {
                val t = if (cur.isEmpty()) part else "$cur · $part"
                if (txt.measureText(t) > maxW && cur.isNotEmpty()) { lines += cur; cur = part } else cur = t
            }
            if (cur.isNotEmpty()) lines += cur
        }
        val lh = 1.5f * u
        val x = 2f * u; val y0 = 15f * u
        val w = lines.maxOf { txt.measureText(it) } + 1.6f * u
        fill.color = Color.argb(120, 0, 0, 0)
        cv.drawRoundRect(RectF(x, y0, x + w, y0 + lines.size * lh + 0.8f * u), u, u, fill)
        txt.color = Color.argb(235, 255, 255, 255)
        lines.forEachIndexed { i, l -> cv.drawText(l, x + 0.8f * u, y0 + (i + 1) * lh, txt) }
    }

    private fun drawWarn(cv: Canvas, sn: Snap, u: Float) {
        val t = SystemClock.uptimeMillis()
        if (sn.ldw != '-') {
            val on = (t / 250) % 2 == 0L
            if (on) {
                fill.color = Color.argb(230, 240, 50, 60)
                val w = 2.4f * u
                if (sn.ldw == 'L') cv.drawRect(0f, 0f, w, height.toFloat(), fill) else cv.drawRect(width - w, 0f, width.toFloat(), height.toFloat(), fill)
            }
            postInvalidateDelayed(120)
        }
        if (sn.level == 2) {
            line.color = Color.argb(220, 240, 50, 60); line.strokeWidth = 1.2f * u
            cv.drawRect(0f, 0f, width.toFloat(), height.toFloat(), line)
        }
        if (sn.msg.isNotEmpty()) pill(cv, sn.msg, width / 2f, height - 4f * u, u)
        else if (sn.over) pill(cv, "Vượt tốc độ ${sn.limit} km/h", width / 2f, height - 4f * u, u)
    }

    private fun pill(cv: Canvas, m: String, cx: Float, cy: Float, u: Float) {
        txt.textSize = 1.5f * u; txt.textAlign = Paint.Align.CENTER; txt.color = Color.WHITE
        val w = txt.measureText(m) + 3f * u
        val r = RectF(cx - w / 2, cy - 1.7f * u, cx + w / 2, cy + 1.7f * u)
        fill.color = Color.argb(190, 12, 16, 22); cv.drawRoundRect(r, 1.7f * u, 1.7f * u, fill)
        cv.drawText(m, cx, cy + 0.55f * u, txt)
    }

    private fun drawCalib(cv: Canvas, u: Float) {
        val x = ox + vpX * imgW * s
        val y = oy + vpY * imgH * s
        line.color = Color.argb(200, 71, 224, 138); line.strokeWidth = 0.25f * u
        cv.drawLine(0f, y, width.toFloat(), y, line)
        cv.drawLine(x, y - 12 * u, x, height.toFloat(), line)
        line.strokeWidth = 0.4f * u; line.color = Color.WHITE
        cv.drawCircle(x, y, 2.2f * u, line)
        cv.drawLine(x - 3.5f * u, y, x + 3.5f * u, y, line); cv.drawLine(x, y - 3.5f * u, x, y + 3.5f * u, line)
        pill(cv, "Xe đi thẳng giữa làn. Kéo dấu + tới chỗ 2 vạch làn gặp nhau ở xa (điểm tụ).", width / 2f, 12.5f * u, u)
        // nut Luu / Huy
        val bw = 12f * u; val bh = 4.4f * u; val by = height - bh - 3f * u
        btnNo.set(width / 2f - bw - u, by, width / 2f - u, by + bh)
        btnOk.set(width / 2f + u, by, width / 2f + u + bw, by + bh)
        fill.color = Color.argb(200, 60, 66, 74); cv.drawRoundRect(btnNo, 2.2f * u, 2.2f * u, fill)
        fill.color = Color.argb(235, 30, 158, 90); cv.drawRoundRect(btnOk, 2.2f * u, 2.2f * u, fill)
        txt.textSize = 1.7f * u; txt.textAlign = Paint.Align.CENTER; txt.color = Color.WHITE
        cv.drawText("Huỷ", btnNo.centerX(), btnNo.centerY() + 0.6f * u, txt)
        cv.drawText("Lưu", btnOk.centerX(), btnOk.centerY() + 0.6f * u, txt)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (calib) {
            if (e.action == MotionEvent.ACTION_UP) {
                if (btnOk.contains(e.x, e.y)) { calib = false; onCalibDone(true); return true }
                if (btnNo.contains(e.x, e.y)) { calib = false; onCalibDone(false); return true }
            }
            if (e.action == MotionEvent.ACTION_DOWN && (btnOk.contains(e.x, e.y) || btnNo.contains(e.x, e.y))) return true
            if (e.action == MotionEvent.ACTION_DOWN || e.action == MotionEvent.ACTION_MOVE) {
                layoutScale()
                vpX = ((e.x - ox) / (imgW * s)).coerceIn(0.15f, 0.85f)
                vpY = ((e.y - oy) / (imgH * s)).coerceIn(0.2f, 0.8f)
                invalidate()
            }
            return true
        }
        if (e.action == MotionEvent.ACTION_DOWN) return gearRect.contains(e.x, e.y)
        if (e.action == MotionEvent.ACTION_UP && gearRect.contains(e.x, e.y)) { onGear(); return true }
        return false
    }
}
