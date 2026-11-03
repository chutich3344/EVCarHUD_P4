package com.evcarhud.tramsac

import android.content.Context
import android.util.JsonReader
import android.util.JsonToken
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.GZIPInputStream

/**
 * CSDL tram sac cong dong EVCarHUD, dung offline: tai ca file /api/stations-offline (~1 MB nen) mot lan,
 * luu tren may, moi 30 phut hoi lai bang If-None-Match (304 neu chua doi). Loc tram gan ngay tren may,
 * khong goi API theo tung vi tri nua (do ton luot doc D1).
 * Dinh dang: {v, built, n, cols:["id","name","address","lat","lon","operator","kw","ports","source"], rows:[[...]]}
 */
object Community {
    private const val URL_OFFLINE = "https://evcarhud.com/api/stations-offline"
    private const val CHECK_MS = 30 * 60_000L
    private const val UA = "EVCarHUD-TramSac/0.3 (Android)"

    @Volatile private var cache: List<Station>? = null
    @Volatile private var checkedAt = 0L

    fun near(ctx: Context, lat: Double, lon: Double, radiusM: Int): List<Station> {
        val all = load(ctx)
        val dLat = radiusM / 111_000.0
        val dLon = dLat / Math.cos(Math.toRadians(lat)).coerceAtLeast(0.2)
        return all.filter {
            Math.abs(it.lat - lat) <= dLat && Math.abs(it.lon - lon) <= dLon &&
                Geo.distM(lat, lon, it.lat, it.lon) <= radiusM
        }
    }

    @Synchronized
    private fun load(ctx: Context): List<Station> {
        val f = File(ctx.filesDir, "congdong.bin")
        val verF = File(ctx.filesDir, "congdong.ver")
        if (cache == null && f.exists()) cache = try { readCache(f) } catch (_: Exception) { null }
        val now = System.currentTimeMillis()
        val have = cache
        if (have != null && now - checkedAt < CHECK_MS) return have
        val c = URL(URL_OFFLINE).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = 15000
            c.readTimeout = 60000
            c.setRequestProperty("User-Agent", UA)
            c.setRequestProperty("Accept", "application/json")
            c.setRequestProperty("Accept-Encoding", "gzip")
            if (have != null && verF.exists()) c.setRequestProperty("If-None-Match", verF.readText())
            val code = c.responseCode
            if (code == 304 && have != null) { checkedAt = now; return have }
            if (code !in 200..299) throw RuntimeException("HTTP $code")
            val raw = c.inputStream
            val input = if ("gzip".equals(c.contentEncoding, true)) GZIPInputStream(raw) else raw
            val list = parse(input)
            if (list.isEmpty()) throw RuntimeException("File trạm trống")
            writeCache(f, list)
            c.getHeaderField("ETag")?.let { verF.writeText(it) }
            cache = list; checkedAt = now
            return list
        } catch (e: Exception) {
            if (have != null) { checkedAt = now - CHECK_MS + 5 * 60_000L; return have }   // dung ban cu, thu lai sau 5 phut
            throw RuntimeException("Không tải được CSDL cộng đồng: ${e.message}")
        } finally {
            c.disconnect()
        }
    }

    /** Doc tung dong, khong nap ca file thanh chuoi. Thu tu cot lay theo "cols". */
    private fun parse(input: java.io.InputStream): List<Station> {
        val out = ArrayList<Station>(4000)
        var cols = listOf("id", "name", "address", "lat", "lon", "operator", "kw", "ports", "source")
        JsonReader(InputStreamReader(input, Charsets.UTF_8)).use { r ->
            r.beginObject()
            while (r.hasNext()) {
                when (r.nextName()) {
                    "cols" -> { val l = ArrayList<String>(); r.beginArray(); while (r.hasNext()) l.add(r.nextString()); r.endArray(); cols = l }
                    "rows" -> {
                        val iName = cols.indexOf("name"); val iLat = cols.indexOf("lat"); val iLon = cols.indexOf("lon")
                        val iOp = cols.indexOf("operator"); val iKw = cols.indexOf("kw"); val iPorts = cols.indexOf("ports")
                        r.beginArray()
                        while (r.hasNext()) {
                            r.beginArray()
                            val v = arrayOfNulls<String>(cols.size)
                            var k = 0
                            while (r.hasNext()) {
                                if (k < v.size && r.peek() != JsonToken.NULL && r.peek() != JsonToken.BEGIN_ARRAY && r.peek() != JsonToken.BEGIN_OBJECT) {
                                    v[k] = if (r.peek() == JsonToken.BOOLEAN) r.nextBoolean().toString() else r.nextString()
                                } else r.skipValue()
                                k++
                            }
                            r.endArray()
                            val la = v.getOrNull(iLat)?.toDoubleOrNull() ?: continue
                            val lo = v.getOrNull(iLon)?.toDoubleOrNull() ?: continue
                            out.add(Station(
                                v.getOrNull(iName).orEmpty().ifBlank { "Trạm sạc" }, la, lo,
                                v.getOrNull(iOp).orEmpty(),
                                v.getOrNull(iKw)?.toDoubleOrNull()?.let { Math.round(it).toInt() } ?: 0,
                                v.getOrNull(iPorts)?.toDoubleOrNull()?.toInt() ?: 0,
                            ))
                        }
                        r.endArray()
                    }
                    else -> r.skipValue()
                }
            }
            r.endObject()
        }
        return out
    }

    private fun writeCache(f: File, list: List<Station>) {
        val tmp = File(f.path + ".tmp")
        DataOutputStream(tmp.outputStream().buffered()).use { o ->
            o.writeInt(list.size)
            list.forEach { o.writeUTF(it.name); o.writeDouble(it.lat); o.writeDouble(it.lon); o.writeUTF(it.operator); o.writeInt(it.maxKw); o.writeInt(it.ports) }
        }
        tmp.renameTo(f)
    }

    private fun readCache(f: File): List<Station> = DataInputStream(f.inputStream().buffered()).use { i ->
        val n = i.readInt()
        List(n) { Station(i.readUTF(), i.readDouble(), i.readDouble(), i.readUTF(), i.readInt(), i.readInt()) }
    }
}
