package com.evcarhud.adas

import java.nio.ByteBuffer

/**
 * Khung YUV trung lap giua cac nguon camera: camera dien thoai (android.media.Image) va cam Wi-Fi
 * (ffmpeg giai ma -> buffer thu cong). Mo phong cach doc cua android.media.Image.Plane de ben nhan
 * (AdasService, SignDet) dung chung mot duong, khong phu thuoc android.media.Image.
 *
 * Luu y: buffer chi dam bao hop le TRONG luc goi onFrame dong bo. Ben nhan phai chep (copy) truoc khi
 * tra ve / dong khung. Goi close() khi dung xong.
 */
class YuvFrame(
    val width: Int,
    val height: Int,
    /** Moc thoi gian (nanosecond), nhu Image.timestamp. */
    val timestamp: Long,
    val planes: Array<Plane>,
    private val onClose: () -> Unit,
) {
    class Plane(val buffer: ByteBuffer, val rowStride: Int, val pixelStride: Int)
    fun close() { onClose() }
}
