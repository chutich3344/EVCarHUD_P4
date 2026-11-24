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
            // 24/11: OPPO bao "APK contains no signature files" voi 0.1.140 (file tren GitHub van dung chu ky v2).
            // Ky them v1 (JAR) de trinh cai nao chi doc v1 / lo tai hong v2 van cai duoc.
            enableV1Signing = true
            enableV2Signing = true
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
        // .so cua ffmpeg (bytedeco) KHONG can le 16KB nhu onnxruntime -> de khong nen (extractNativeLibs=false)
        // thi OPPO/Android 15 khong phan tich duoc APK ("Parsing failed"). Dong goi NEN + giai nen khi cai
        // (useLegacyPackaging=true + manifest extractNativeLibs=true): cai duoc moi may, khong phu thuoc can le.
        jniLibs {
            useLegacyPackaging = true
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
