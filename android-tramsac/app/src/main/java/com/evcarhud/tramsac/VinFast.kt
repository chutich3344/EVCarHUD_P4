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
 * Danh sach tram sac o to VinFast (~25.000 tram ca nuoc).
 * Nguon goc: du lieu cong khai cua trang "Tim kiem showroom & tram sac" tren vinfastauto.com (file 75 MB gom ca
 * showroom, tram doi pin...). GitHub Actions (du-lieu-vinfast.yml) moi ngay rut gon con ten + toa do (~vai tram KB)
 * va dat vao ban phat hanh "du-lieu-tram-sac"; app tai file nho do, luu tren may, kiem tra ban moi moi 24 gio.
 */
object VinFast {
    private const val BASE = "https://github.com/chutich3344/EVCarHUD_P4/releases/download/du-lieu-tram-sac/"
    private const val CHECK_MS = 24 * 3600_000L
    private const val UA = "EVCarHUD-TramSac/0.2 (Android)"

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
            val meta = JSONObject(String(fetch(BASE + "vinfast-meta.json").use { it.readBytes() }, Charsets.UTF_8))
            val gen = meta.optString("generation") + "/" + meta.optString("updated")
            if (have != null && genF.exists() && genF.readText() == gen) {
                checkedAt = now; return have            // chua co ban moi
            }
            val list = fetch(BASE + "vinfast-tram-sac.json.gz").use { parse(it) }
            if (list.size < 100) throw RuntimeException("Dữ liệu VinFast quá ít trạm")
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
        c.readTimeout = 30000
        c.instanceFollowRedirects = true
        c.setRequestProperty("User-Agent", UA)
        c.setRequestProperty("Accept", "application/json, */*")
        if (c.responseCode !in 200..299) { c.disconnect(); throw RuntimeException("HTTP ${c.responseCode}") }
        val bin = BufferedInputStream(c.inputStream)
        bin.mark(2)
        val b0 = bin.read(); val b1 = bin.read()
        bin.reset()
        return if (b0 == 0x1f && b1 == 0x8b) GZIPInputStream(bin) else bin
    }

    /** Doc [["ten", lat, lon], ...] tung phan tu (khong nap ca file thanh chuoi). */
    private fun parse(input: InputStream): List<Station> {
        val out = ArrayList<Station>(30000)
        JsonReader(InputStreamReader(input, Charsets.UTF_8)).use { r ->
            r.beginArray()
            while (r.hasNext()) {
                if (r.peek() != JsonToken.BEGIN_ARRAY) { r.skipValue(); continue }
                r.beginArray()
                val name = if (r.peek() == JsonToken.STRING) r.nextString() else { r.skipValue(); "" }
                val lat = r.nextDouble()
                val lon = r.nextDouble()
                while (r.hasNext()) r.skipValue()
                r.endArray()
                out.add(Station(name.ifBlank { "Trạm sạc VinFast" }, lat, lon, "VinFast"))
            }
            r.endArray()
        }
        return out
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
