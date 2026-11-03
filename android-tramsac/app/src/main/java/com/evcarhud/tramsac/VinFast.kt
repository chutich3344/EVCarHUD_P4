package com.evcarhud.tramsac

import android.content.Context
import android.util.JsonReader
import android.util.JsonToken
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.InputStream
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.GZIPInputStream

/**
 * Danh sach tram sac o to VinFast, lay tu du lieu cong khai ma trang "Tim kiem showroom & tram sac" cua
 * vinfastauto.com dung: static-cms-prod.vinfastauto.com/locators/locators-meta.json -> file locators-N.json.gz
 * (moi showroom, xuong dich vu, tram sac ca nuoc). Chi giu category_slug = car_charging_station.
 * Tai ca file mot lan, luu dem tren may, kiem tra ban moi moi 24 gio. Loc theo ban kinh lam tren may.
 */
object VinFast {
    private const val CDN = "https://static-cms-prod.vinfastauto.com/locators/"
    private const val CHECK_MS = 24 * 3600_000L
    private const val UA = "Mozilla/5.0 (Linux; Android) EVCarHUD-TramSac/0.2"

    @Volatile private var cache: List<Station>? = null
    @Volatile private var checkedAt = 0L

    /** Tram VinFast trong ban kinh. Chay tren luong nen. */
    fun near(ctx: Context, lat: Double, lon: Double, radiusM: Int): List<Station> {
        val all = load(ctx)
        // loc nhanh theo hop bao truoc khi tinh khoang cach that
        val dLat = radiusM / 111_000.0
        val dLon = dLat / Math.cos(Math.toRadians(lat)).coerceAtLeast(0.2)
        return all.filter {
            Math.abs(it.lat - lat) <= dLat && Math.abs(it.lon - lon) <= dLon &&
                Geo.distM(lat, lon, it.lat, it.lon) <= radiusM
        }
    }

    /** So tram VinFast dang co trong bo nho dem (de hien thong bao). */
    fun total(): Int = cache?.size ?: 0

    @Synchronized
    private fun load(ctx: Context): List<Station> {
        val f = File(ctx.filesDir, "vinfast.bin")
        val genF = File(ctx.filesDir, "vinfast.gen")
        if (cache == null && f.exists()) cache = try { readCache(f) } catch (_: Exception) { null }
        val now = System.currentTimeMillis()
        val have = cache
        if (have != null && now - checkedAt < CHECK_MS) return have
        try {
            val meta = JSONObject(String(fetch(CDN + "locators-meta.json").use { it.readBytes() }, Charsets.UTF_8))
            val full = meta.optString("full")
            val gen = meta.optString("generation")
            if (!Regex("locators-\\d+\\.json\\.gz").matches(full)) throw RuntimeException("meta VinFast lạ")
            if (have != null && genF.exists() && genF.readText() == gen) {
                checkedAt = now; return have            // chua co ban moi
            }
            val list = fetch(CDN + full).use { parse(it) }
            if (list.isEmpty()) throw RuntimeException("Dữ liệu VinFast không có trạm sạc ô tô")
            writeCache(f, list); genF.writeText(gen)
            cache = list; checkedAt = now
            return list
        } catch (e: Exception) {
            if (have != null) { checkedAt = now - CHECK_MS + 3600_000L; return have }  // dung ban cu, thu lai sau 1 gio
            throw RuntimeException("Không tải được dữ liệu VinFast: ${e.message}")
        }
    }

    /** Mo URL, tu giai nen gzip neu can (file .json.gz co the den nguyen ban nen hoac da duoc giai nen). */
    private fun fetch(url: String): InputStream {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 15000
        c.readTimeout = 60000
        c.setRequestProperty("User-Agent", UA)
        c.setRequestProperty("Accept", "application/json, */*")
        c.setRequestProperty("Referer", "https://vinfastauto.com/vn_vi/tim-kiem-showroom-tram-sac")
        if (c.responseCode !in 200..299) { c.disconnect(); throw RuntimeException("HTTP ${c.responseCode}") }
        val bin = BufferedInputStream(c.inputStream)
        bin.mark(2)
        val b0 = bin.read(); val b1 = bin.read()
        bin.reset()
        return if (b0 == 0x1f && b1 == 0x8b) GZIPInputStream(bin) else bin
    }

    /** Doc tung ban ghi (khong nap ca file vao bo nho). */
    private fun parse(input: InputStream): List<Station> {
        val out = ArrayList<Station>()
        JsonReader(InputStreamReader(input, Charsets.UTF_8)).use { r ->
            if (r.peek() == JsonToken.BEGIN_ARRAY) readRows(r, out) else {
                r.beginObject()
                while (r.hasNext()) {
                    if (r.nextName() == "data" && r.peek() == JsonToken.BEGIN_ARRAY) readRows(r, out) else r.skipValue()
                }
                r.endObject()
            }
        }
        return out
    }

    private fun readRows(r: JsonReader, out: MutableList<Station>) {
        r.beginArray()
        while (r.hasNext()) {
            if (r.peek() != JsonToken.BEGIN_OBJECT) { r.skipValue(); continue }
            var name = ""; var addr = ""; var slug = ""; var status = ""
            var lat = Double.NaN; var lon = Double.NaN
            r.beginObject()
            while (r.hasNext()) {
                val k = r.nextName()
                if (r.peek() == JsonToken.NULL) { r.nextNull(); continue }
                when (k) {
                    "name" -> name = str(r)
                    "address" -> addr = str(r)
                    "category_slug" -> slug = str(r)
                    "status" -> status = str(r)
                    "lat" -> lat = str(r).toDoubleOrNull() ?: Double.NaN
                    "lng" -> lon = str(r).toDoubleOrNull() ?: Double.NaN
                    else -> r.skipValue()
                }
            }
            r.endObject()
            if (slug != "car_charging_station") continue
            if (status == "0" || status.equals("false", true) || status.equals("inactive", true)) continue
            if (lat.isNaN() || lon.isNaN() || (lat == 0.0 && lon == 0.0)) continue
            out.add(Station(name.trim().ifBlank { addr.ifBlank { "Trạm sạc VinFast" } }, lat, lon, "VinFast"))
        }
        r.endArray()
    }

    private fun str(r: JsonReader): String = when (r.peek()) {
        JsonToken.STRING, JsonToken.NUMBER -> r.nextString()
        JsonToken.BOOLEAN -> r.nextBoolean().toString()
        else -> { r.skipValue(); "" }
    }

    private fun writeCache(f: File, list: List<Station>) {
        val tmp = File(f.path + ".tmp")
        DataOutputStream(tmp.outputStream().buffered()).use { o ->
            o.writeInt(list.size)
            list.forEach { o.writeUTF(it.name); o.writeDouble(it.lat); o.writeDouble(it.lon) }
        }
        tmp.renameTo(f)
    }

    private fun readCache(f: File): List<Station> = DataInputStream(f.inputStream().buffered()).use { i ->
        val n = i.readInt()
        List(n) { Station(i.readUTF(), i.readDouble(), i.readDouble(), "VinFast") }
    }
}
