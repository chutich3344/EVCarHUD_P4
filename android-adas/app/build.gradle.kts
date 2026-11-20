plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.evcarhud.adas"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.evcarhud.adas"
        minSdk = 26
        targetSdk = 36
        val run = (System.getenv("GITHUB_RUN_NUMBER") ?: "0").toInt()
        versionCode = 100 + run
        versionName = "0.1.$run"
        // ONNX Runtime chi can ban 64-bit cho dien thoai hien nay: bot ~10 MB
        ndk { abiFilters += listOf("arm64-v8a") }
    }

    // Khoa ky co dinh cho ban thu (de trong repo): moi ban build moi cai DE len ban cu duoc.
    // Khong dung cho ban phat hanh Google Play.
    signingConfigs {
        create("lan") {
            storeFile = file("lan-test.jks")
            storePassword = "evcarhudlan"
            keyAlias = "lan"
            keyPassword = "evcarhudlan"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("lan")
        }
    }
    // Mo hinh .onnx de nguyen (khong nen) de chep ra nhanh
    androidResources { noCompress += "onnx" }
    packaging {
        // JavaCV/ffmpeg co the trung ten .so giua cac module -> giu ban dau tien.
        // KHONG dat useLegacyPackaging: manifest da extractNativeLibs=false (nen .so khong nen, can le trang)
        // cho onnxruntime; ep legacy=true lam APK OPPO khong phan tich duoc ("Parsing failed").
        jniLibs {
            pickFirsts += listOf("**/*.so")
        }
        resources {
            excludes += listOf(
                "META-INF/native-image/**",
                "META-INF/*.kotlin_module",
                "org/bytedeco/**/*.properties",
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.20.0")
    // cam guong / 70mai Wi-Fi: RTSP -> giai ma (MirrorCam.kt). Dung ffmpeg (JavaCV) = DUNG engine app hang
    // (media_kit = mpv/ffmpeg) chay UDP on dinh. VLC khong giai ma noi luong cam nay tren chip may (Vout=0
    // ca HW lan SW du da co du lieu). Chi lay native arm64 (ffmpeg + javacpp) nen nang ~25 MB.
    implementation("org.bytedeco:javacv:1.5.10") { isTransitive = false }   // FFmpegFrameGrabber + Frame (khong keo opencv…)
    implementation("org.bytedeco:javacpp:1.5.10")
    implementation("org.bytedeco:javacpp:1.5.10:android-arm64")
    implementation("org.bytedeco:ffmpeg:6.1.1-1.5.10")
    implementation("org.bytedeco:ffmpeg:6.1.1-1.5.10:android-arm64")
}
