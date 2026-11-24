package com.evcarhud.adas

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * Man cai dat kieu kinh mo (giao dien nguoi dung chot 19/11): trai danh muc, phai noi dung; hang bo tron, mau nhan xanh la.
 * Dung bang ma (khong XML). Kich thuoc theo u = 1 px cua ban giai lap 1600x720.
 */
@SuppressLint("ViewConstructor")
class SettingsView(ctx: Context, private val cats: List<Cat>, private val onClose: () -> Unit) : FrameLayout(ctx) {

    sealed class Item {
        class Toggle(val title: String, val desc: String?, val get: () -> Boolean, val set: (Boolean) -> Unit, val isNew: Boolean = false) : Item()
        class Seg(val title: String, val desc: String?, val opts: List<String>, val get: () -> Int, val set: (Int) -> Unit, val isNew: Boolean = false) : Item()
        class Action(val title: String, val desc: String?, val value: String?, val onClick: () -> Unit) : Item()
        class Header(val text: String) : Item()
    }

    class Cat(val key: String, val title: String, val sub: () -> String, val items: () -> List<Item>)

    private val fBold = try { Typeface.createFromAsset(ctx.assets, "fonts/BeVietnamPro-Bold.ttf") } catch (_: Exception) { Typeface.DEFAULT_BOLD }
    private val fSemi = try { Typeface.createFromAsset(ctx.assets, "fonts/BeVietnamPro-SemiBold.ttf") } catch (_: Exception) { fBold }
    private var u = 1f
    private var cur = 0
    private val rail = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
    private val pane = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
    private val railScroll = ScrollView(ctx).apply { isVerticalScrollBarEnabled = false; addView(rail) }
    private val paneScroll = ScrollView(ctx).apply { isVerticalScrollBarEnabled = false; addView(pane) }
    private val paneBox = FrameLayout(ctx).apply { addView(paneScroll, LayoutParams(-1, -1)) }
    private var built = false

    init {
        setBackgroundColor(0xB0080C0E.toInt())     // phu toi ~70%: chu doc ro tren hinh duong pho (nen phia sau lam mo neu may ho tro)
        isClickable = true                          // khong cho cham xuyen xuong lop camera
        addView(railScroll)
        addView(paneBox)
    }

    fun selectKey(k: String) { cats.indexOfFirst { it.key == k }.takeIf { it >= 0 }?.let { cur = it; if (built) refresh() } }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        super.onSizeChanged(w, h, ow, oh)
        if (w == 0 || h == 0) return
        u = minOf(w / 1600f, h / 720f)
        val railW = (450 * u).toInt()   // rong hon cho chu danh muc to (textScale 1.3) khong bi cat
        val mx = (36 * u).toInt(); val my = (24 * u).toInt()
        railScroll.layoutParams = LayoutParams(railW, h - 2 * my).apply { leftMargin = mx; topMargin = my }
        paneBox.layoutParams = LayoutParams(w - railW - 2 * mx - (22 * u).toInt(), h - 2 * my).apply { leftMargin = mx + railW + (22 * u).toInt(); topMargin = my }
        paneBox.background = glass(30f, 0x73141C1C, 0x1FFFFFFF)
        val p = (28 * u).toInt(); paneScroll.setPadding(p, (22 * u).toInt(), p, (22 * u).toInt())
        post { built = true; refresh() }
    }

    /** Ve lai ca 2 cot (sau moi lan doi gia tri). */
    fun refresh() {
        buildRail(); buildPane()
    }

    private fun glass(r: Float, fill: Int, stroke: Int) = GradientDrawable().apply {
        cornerRadius = r * u; setColor(fill); setStroke(maxOf(1, (1.2f * u).toInt()), stroke)
    }

    // Chu Cai dat to hon ~30% (nguoi dung: "ko doc dc gi" 23/11). Hang bo tron wrap_content + 2 cot deu nam
    // trong ScrollView nen chu to chi lam cuon dai them, khong vo layout ("to qua cho no cuon cung dc").
    private val textScale = 1.3f
    private fun tv(t: String, size: Float, color: Int = Color.WHITE, bold: Boolean = false) = TextView(context).apply {
        text = t; textSize = 0f; setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, size * u * textScale); setTextColor(color)
        typeface = if (bold) fBold else fSemi; includeFontPadding = false
    }

    private fun buildRail() {
        rail.removeAllViews()
        val top = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val back = IconView(context, "back").apply {
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(0x33223042); setStroke(maxOf(1, (1.6f * u).toInt()), 0x59FFFFFF.toInt()) }
        }   // nut to hon (64u) + vien ro, de bam khi lai
        back.setOnClickListener { onClose() }
        top.addView(back, LinearLayout.LayoutParams((64 * u).toInt(), (64 * u).toInt()))
        top.addView(tv("Cài đặt", 30f, bold = true), LinearLayout.LayoutParams(-2, -2).apply { leftMargin = (14 * u).toInt() })
        rail.addView(top, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = (12 * u).toInt(); leftMargin = (4 * u).toInt() })
        cats.forEachIndexed { i, c ->
            val on = i == cur
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
                background = if (on) glass(22f, 0x3834C759, 0xA634C759.toInt()) else glass(22f, 0x12FFFFFF, 0x1AFFFFFF)
                setPadding((16 * u).toInt(), (9 * u).toInt(), (18 * u).toInt(), (9 * u).toInt())
                setOnClickListener { cur = i; refresh() }
            }
            val ic = IconView(context, c.key).apply {
                ring = if (on) 0xFF34C759.toInt() else 0x59FFFFFF
            }
            row.addView(ic, LinearLayout.LayoutParams((42 * u).toInt(), (42 * u).toInt()))
            row.addView(tv(c.title, 21f).apply { setSingleLine(); ellipsize = TextUtils.TruncateAt.END },
                LinearLayout.LayoutParams(0, -2, 1f).apply { leftMargin = (14 * u).toInt() })
            row.addView(tv(c.sub(), 15f, 0x8CFFFFFF.toInt()).apply { setSingleLine() })
            rail.addView(row, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = (7 * u).toInt() })
        }
    }

    private fun buildPane() {
        pane.removeAllViews()
        val c = cats.getOrNull(cur) ?: return
        pane.addView(tv(c.title, 26f, bold = true), LinearLayout.LayoutParams(-2, -2).apply { bottomMargin = (12 * u).toInt() })
        for (it in c.items()) pane.addView(row(it), LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = (10 * u).toInt() })
    }

    private fun row(it: Item): View {
        if (it is Item.Header) return tv(it.text.uppercase(), 16f, 0x99FFFFFF.toInt()).apply {
            letterSpacing = 0.06f; setPadding((4 * u).toInt(), (8 * u).toInt(), 0, 0)
        }
        val r = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            background = glass(20f, 0x12FFFFFF, 0x17FFFFFF)
            setPadding((20 * u).toInt(), (13 * u).toInt(), (18 * u).toInt(), (13 * u).toInt())
            minimumHeight = (64 * u).toInt()
        }
        val (title, desc, isNew) = when (it) {
            is Item.Toggle -> Triple(it.title, it.desc, it.isNew)
            is Item.Seg -> Triple(it.title, it.desc, it.isNew)
            is Item.Action -> Triple(it.title, it.desc, false)
            else -> Triple("", null, false)
        }
        val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val tl = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        tl.addView(tv(title, 20f))
        if (isNew) tl.addView(tv("MỚI", 12f, Color.BLACK, true).apply {
            background = GradientDrawable().apply { cornerRadius = 8 * u; setColor(0xFFFF9F0A.toInt()) }
            setPadding((7 * u).toInt(), (2 * u).toInt(), (7 * u).toInt(), (2 * u).toInt())
        }, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = (8 * u).toInt() })
        col.addView(tl)
        if (desc != null) col.addView(tv(desc, 15f, 0x8CFFFFFF.toInt()), LinearLayout.LayoutParams(-2, -2).apply { topMargin = (3 * u).toInt() })
        r.addView(col, LinearLayout.LayoutParams(0, -2, 1f))
        when (it) {
            is Item.Toggle -> {
                val sw = SwitchView(context).apply { on = it.get() }
                r.addView(sw, LinearLayout.LayoutParams((58 * u).toInt(), (34 * u).toInt()))
                r.setOnClickListener { _ -> it.set(!it.get()); refresh() }
            }
            is Item.Seg -> {
                val seg = LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL
                    background = GradientDrawable().apply { cornerRadius = 16 * u; setColor(0x1AFFFFFF) }
                    setPadding((3 * u).toInt(), (3 * u).toInt(), (3 * u).toInt(), (3 * u).toInt())
                }
                val sel = it.get()
                it.opts.forEachIndexed { k, o ->
                    seg.addView(tv(o, 17f, if (k == sel) Color.WHITE else 0xCCFFFFFF.toInt()).apply {
                        setPadding((15 * u).toInt(), (7 * u).toInt(), (15 * u).toInt(), (7 * u).toInt())
                        if (k == sel) background = GradientDrawable().apply { cornerRadius = 13 * u; setColor(0xFF34C759.toInt()) }
                        setOnClickListener { _ -> it.set(k); refresh() }
                    })
                }
                r.addView(seg)
            }
            is Item.Action -> {
                if (it.value != null) r.addView(tv(it.value, 18f, 0xBFFFFFFF.toInt()).apply { setSingleLine() },
                    LinearLayout.LayoutParams(-2, -2).apply { rightMargin = (10 * u).toInt() })
                r.addView(IconView(context, "chev"), LinearLayout.LayoutParams((22 * u).toInt(), (22 * u).toInt()))
                r.setOnClickListener { _ -> it.onClick() }
            }
            else -> {}
        }
        return r
    }

    /** Cong tac bat/tat kieu iOS. */
    private inner class SwitchView(c: Context) : View(c) {
        var on = false
        private val p = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onDraw(cv: Canvas) {
            val h = height.toFloat(); val w = width.toFloat()
            p.color = if (on) 0xFF34C759.toInt() else 0x38FFFFFF
            cv.drawRoundRect(RectF(0f, 0f, w, h), h / 2, h / 2, p)
            p.color = Color.WHITE
            val r = h / 2 - 3 * u
            cv.drawCircle(if (on) w - h / 2 else h / 2, h / 2, r, p)
        }
    }

    /** Bieu tuong net manh ve tren luoi 24x24 (giong ban giai lap), co vong tron ngoai. */
    private inner class IconView(c: Context, private val k: String) : View(c) {
        var ring = 0
        private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
        override fun onDraw(cv: Canvas) {
            val w = width.toFloat(); val h = height.toFloat()
            if (ring != 0) { p.color = ring; p.strokeWidth = 1.5f * u; cv.drawCircle(w / 2, h / 2, w / 2 - p.strokeWidth, p) }
            val s = if (k == "chev") w / 24f else w * 0.52f / 24f
            cv.save(); cv.translate(w / 2 - 12 * s, h / 2 - 12 * s); cv.scale(s, s)
            p.color = if (k == "chev") 0x99FFFFFF.toInt() else Color.WHITE; p.strokeWidth = 2f
            val pa = Path()
            when (k) {
                "back" -> { pa.moveTo(14f, 6f); pa.lineTo(8f, 12f); pa.lineTo(14f, 18f) }
                "chev" -> { pa.moveTo(9f, 6f); pa.lineTo(15f, 12f); pa.lineTo(9f, 18f) }
                "canh" -> { pa.moveTo(12f, 3f); pa.lineTo(21f, 19f); pa.lineTo(3f, 19f); pa.close(); pa.moveTo(12f, 10f); pa.lineTo(12f, 14f); pa.moveTo(12f, 16.8f); pa.lineTo(12f, 17f) }
                "cam" -> { pa.addRoundRect(RectF(3f, 7f, 21f, 20f), 2f, 2f, Path.Direction.CW); pa.addCircle(12f, 13.5f, 3.5f, Path.Direction.CW); pa.moveTo(8f, 7f); pa.lineTo(10f, 4f); pa.lineTo(14f, 4f); pa.lineTo(16f, 7f) }
                "hien" -> { pa.addRoundRect(RectF(3f, 4f, 21f, 17f), 2f, 2f, Path.Direction.CW); pa.moveTo(8f, 21f); pa.lineTo(16f, 21f); pa.moveTo(12f, 17f); pa.lineTo(12f, 21f) }
                "hud" -> { pa.moveTo(7f, 7f); pa.lineTo(17f, 17f); pa.lineTo(12f, 21f); pa.lineTo(12f, 3f); pa.lineTo(17f, 7f); pa.lineTo(7f, 17f) }
                "dan" -> { pa.moveTo(3f, 11f); pa.lineTo(21f, 3f); pa.lineTo(13f, 21f); pa.lineTo(11f, 13f); pa.close() }
                "nang" -> { pa.moveTo(13f, 2f); pa.lineTo(4f, 14f); pa.lineTo(11f, 14f); pa.lineTo(10f, 22f); pa.lineTo(19f, 10f); pa.lineTo(12f, 10f); pa.close() }
                "kt" -> { pa.addCircle(12f, 12f, 3f, Path.Direction.CW); for (a in 0 until 8) { val r = Math.toRadians(a * 45.0); pa.moveTo(12f + 6f * Math.cos(r).toFloat(), 12f + 6f * Math.sin(r).toFloat()); pa.lineTo(12f + 9f * Math.cos(r).toFloat(), 12f + 9f * Math.sin(r).toFloat()) } }
                else -> { pa.addCircle(12f, 12f, 9f, Path.Direction.CW); pa.moveTo(12f, 11f); pa.lineTo(12f, 17f); pa.moveTo(12f, 7.6f); pa.lineTo(12f, 8f) }
            }
            cv.drawPath(pa, p); cv.restore()
        }
    }
}
