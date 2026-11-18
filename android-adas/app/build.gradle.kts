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
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.20.0")
    // cam guong Wi-Fi: RTSP -> giai ma (MirrorCam.kt)
    implementation("androidx.media3:media3-exoplayer:1.4.1")
    implementation("androidx.media3:media3-exoplayer-rtsp:1.4.1")
}
