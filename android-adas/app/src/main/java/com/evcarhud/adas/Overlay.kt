package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
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
        set(v) { field = v; invalidate() }
    var vpX = 0.5f
    var vpY = 0.5f

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    private val txt = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD) }
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
        layoutScale()
        val u = width / 100f
        frame?.let { dst.set(ox, oy, ox + imgW * s, oy + imgH * s); cv.drawBitmap(it, null, dst, bmpPaint) }
        val sn = snap
        val o = sn?.out
        val c = sn?.cam
        if (o != null && c != null && !calib) {
            // khong ve lan/duong di de len taplo (camera dat thap thay taplo che phan duong gan)
            cv.save(); cv.clipRect(0f, 0f, width.toFloat(), oy + sn.hood * imgH * s)
            drawScene(cv, sn, o, c)
            cv.restore()
        }
        if (sn != null && !calib && sn.level >= 3) {
            fill.color = Color.argb(150, 220, 30, 40); cv.drawRect(0f, 0f, width.toFloat(), height.toFloat(), fill)
            txt.textSize = 14 * u; txt.textAlign = Paint.Align.CENTER; txt.color = Color.WHITE
            cv.drawText("PHANH!", width / 2f, height / 2f + 5 * u, txt)
        }
        drawTop(cv, sn, u)
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

    private fun drawTop(cv: Canvas, sn: Snap?, u: Float) {
        val top = 2.2f * u
        // o lech lan ben trai (toc do da co tren HUD nen khong hien)
        val x = 2f * u
        val off = sn?.offCm
        val offTxt = when { off == null || calib -> "--"; abs(off) < 5 -> "giữa"; off > 0 -> "← $off cm"; else -> "${-off} cm →" }
        val offCol = if (sn != null && sn.ldw != '-') Color.rgb(255, 84, 104) else Color.WHITE
        box(cv, x, top, u, offTxt, "LỆCH LÀN", offCol)

        // giua: so lon = khoang cach xe truoc
        val lead = sn?.leadM
        val red = Color.rgb(255, 84, 104); val yel = Color.rgb(255, 210, 63)
        val leadCol = when { sn == null || lead == null -> Color.WHITE; sn.level >= 2 -> red; sn.level == 1 -> yel; else -> Color.rgb(71, 224, 138) }
        txt.textAlign = Paint.Align.CENTER; txt.color = leadCol
        txt.setShadowLayer(0.8f * u, 0f, 0.2f * u, Color.argb(150, 0, 0, 0))
        txt.textSize = 6.6f * u
        cv.drawText(if (lead != null && !calib) "${lead.roundToInt()}" else "--", width / 2f, top + 5.6f * u, txt)
        txt.textSize = 1.5f * u; txt.color = Color.WHITE
        val sub = when {
            lead == null || calib -> "m · chưa thấy xe trước"
            sn.level >= 1 -> "m · tối thiểu ${Op.safeDist(sn.vKmh).roundToInt()} m" + (sn.ttc?.let { " · %.1f s".format(it) } ?: "")
            else -> "m · xe trước"
        }
        cv.drawText(sub, width / 2f, top + 7.6f * u, txt)
        txt.clearShadowLayer()

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
