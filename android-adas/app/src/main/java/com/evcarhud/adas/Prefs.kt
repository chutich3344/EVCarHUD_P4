package com.evcarhud.adas

import android.content.Context

/** Cai dat luu tren may. */
class Prefs(ctx: Context) {
    private val p = ctx.getSharedPreferences("adas", Context.MODE_PRIVATE)

    var calibrated: Boolean
        get() = p.getBoolean("calib", false)
        set(v) = p.edit().putBoolean("calib", v).apply()
    /** Tu hieu chinh (mac dinh): goc cui/nghieng tu cam bien trong luc, goc lech trai/phai tu chuyen dong (mo hinh). */
    var autoCalib: Boolean
        get() = p.getBoolean("auto", true)
        set(v) = p.edit().putBoolean("auto", v).apply()
    /** Hieu chinh dang dung (rad). */
    var pitch: Float
        get() = p.getFloat("pitch", 0f)
        set(v) = p.edit().putFloat("pitch", v).apply()
    var yaw: Float
        get() = p.getFloat("yaw", 0f)
        set(v) = p.edit().putFloat("yaw", v).apply()
    var roll: Float
        get() = p.getFloat("roll", 0f)
        set(v) = p.edit().putFloat("roll", v).apply()
    /** Chieu cao camera so voi mat duong (m). */
    var hood: Float
        get() = p.getFloat("hood", 1f)
        set(v) = p.edit().putFloat("hood", v).apply()
    var camH: Float
        get() = p.getFloat("camh", 1.2f)
        set(v) = p.edit().putFloat("camh", v).apply()
    var accel: String
        get() = p.getString("accel", "npu") ?: "npu"
        set(v) = p.edit().putString("accel", v).apply()
    var hudMac: String
        get() = p.getString("mac", "") ?: ""
        set(v) = p.edit().putString("mac", v).apply()
    var mapSrc: String
        get() = (p.getString("msrc", "google") ?: "google").let { if (it == "carto") "google" else it }
        set(v) = p.edit().putString("msrc", v).apply()
    var showMap: Boolean
        get() = p.getBoolean("map", true)
        set(v) = p.edit().putBoolean("map", v).apply()
    /** Ban do Google tu dong sang/toi theo gio mat troi (ngay = anh goc, dem = dao mau toi). Chi tac dung voi nguon Google. Mac dinh TAT. */
    var mapAuto: Boolean
        get() = p.getBoolean("mapauto", false)
        set(v) = p.edit().putBoolean("mapauto", v).apply()
    /** Chia doi man hinh: trai = camera + ADAS, phai = ban do. Mac dinh TAT. */
    var splitScreen: Boolean
        get() = p.getBoolean("split", false)
        set(v) = p.edit().putBoolean("split", v).apply()
    /** Cong tac tat NHANH het cua so noi (PiP: ban do + o nhiet) de xem camera gon. Mac dinh TAT (van hien PiP). */
    var hidePip: Boolean
        get() = p.getBoolean("hidepip", false)
        set(v) = p.edit().putBoolean("hidepip", v).apply()
    var overHood: Boolean
        get() = p.getBoolean("overhood", true)
        set(v) = p.edit().putBoolean("overhood", v).apply()
    /** Nguon camera: "phone" hoac "mirror" (cam guong Wi-Fi). */
    var camSrc: String
        get() = p.getString("camsrc", "phone") ?: "phone"
        set(v) = p.edit().putString("camsrc", v).apply()
    /** Do phan giai cam Wi-Fi: false = 720p (mat, muot, do tre), true = 1080p (net hon, nong hon). */
    var camHd: Boolean
        get() = p.getBoolean("camhd", false)
        set(v) = p.edit().putBoolean("camhd", v).apply()
    /** Lam net anh xem cam Wi-Fi (unsharp nhe tren preview, khong dung cho AI). */
    var camSharpen: Boolean
        get() = p.getBoolean("camsharp", true)
        set(v) = p.edit().putBoolean("camsharp", v).apply()
    /** Can mau tu dong cam Wi-Fi: gian dai gia tri (limited->full) + tang bao hoa cho preview. */
    var camColor: Boolean
        get() = p.getBoolean("camcolor", true)
        set(v) = p.edit().putBoolean("camcolor", v).apply()
    /** Tu noi Wi-Fi cam (WifiNetworkSpecifier, mat khau 12345678): app tu nam + giu chat ket noi Wi-Fi
     *  cam de Android khong ngat (tranh rot ENONET). Lan dau Android hoi 1 lan. Mac dinh TAT. */
    var camAutoWifi: Boolean
        get() = p.getBoolean("camautowifi", false)
        set(v) = p.edit().putBoolean("camautowifi", v).apply()
    var showDbg: Boolean
        get() = p.getBoolean("dbg", false)
        set(v) = p.edit().putBoolean("dbg", v).apply()
    var signs: Boolean
        get() = p.getBoolean("signs", true)
        set(v) = p.edit().putBoolean("signs", v).apply()
    /** Den giao thong + dem giay (quet khi xe cham/dung gan giao lo; AI openpilot nghi bot khi dung den do). */
    var tlight: Boolean
        get() = p.getBoolean("tlight", true)
        set(v) = p.edit().putBoolean("tlight", v).apply()
    var beep: Boolean
        get() = p.getBoolean("beep", true)
        set(v) = p.edit().putBoolean("beep", v).apply()
    var showTemp: Boolean
        get() = p.getBoolean("temp", true)
        set(v) = p.edit().putBoolean("temp", v).apply()
    /** Goc dat cua so: 0 tren trai, 1 tren phai, 2 duoi trai, 3 duoi phai. */
    var tempCorner: Int
        get() = p.getInt("tc", 2)
        set(v) = p.edit().putInt("tc", v).apply()
    var mapCorner: Int
        get() = p.getInt("mc", 2)   // mac dinh goc duoi-trai (khop thiet ke 21/11); overlay.avoid() da tranh ve vach de len map
        set(v) = p.edit().putInt("mc", v).apply()
    /** Khoa HERE Routing v8 (platform.here.com), trong = dung OSRM. */
    var hereKey: String
        get() = p.getString("here", "") ?: ""
        set(v) = p.edit().putString("here", v.trim()).apply()
    /** Am luong tieng canh bao: 0 nho, 1 vua, 2 to. */
    var vol: Int
        get() = p.getInt("vol", 1)
        set(v) = p.edit().putInt("vol", v).apply()
    /** Canh bao lech lan (tat khi di pho dong). */
    var ldwOn: Boolean
        get() = p.getBoolean("ldw", true)
        set(v) = p.edit().putBoolean("ldw", v).apply()
    /** Do nhay canh bao xe truoc: 0 som, 1 vua, 2 muon. */
    var fcwSens: Int
        get() = p.getInt("fcw", 1)
        set(v) = p.edit().putInt("fcw", v).apply()
    /** Keu khi vuot toc do: -1 tat, 0 / 5 / 10 km/h qua bien. */
    var overKmh: Int
        get() = p.getInt("over", 5)
        set(v) = p.edit().putInt("over", v).apply()
    var camAlert: Boolean
        get() = p.getBoolean("camal", true)
        set(v) = p.edit().putBoolean("camal", v).apply()
    /** Toc do mac dinh theo luat (50/60) khi chua thay bien. */
    var defLimit: Boolean
        get() = p.getBoolean("deflim", true)
        set(v) = p.edit().putBoolean("deflim", v).apply()
    /** Dung bien toc do (bong bong) Waze/GOFA ma HUD chuyen sang. */
    var navLimit: Boolean
        get() = p.getBoolean("navlim", true)
        set(v) = p.edit().putBoolean("navlim", v).apply()
    /** Gui bien toc do doc tu camera len HUD (goi S1). */
    var hudSign: Boolean
        get() = p.getBoolean("hudsign", true)
        set(v) = p.edit().putBoolean("hudsign", v).apply()
    /** Che do: 0 tiet kiem, 1 can bang, 2 toi da. */
    var signSize: Int
        get() = p.getInt("signSize", 1)
        set(v) = p.edit().putInt("signSize", v).apply()
    var perf: Int
        get() = p.getInt("perf", 1)
        set(v) = p.edit().putInt("perf", v).apply()
}
