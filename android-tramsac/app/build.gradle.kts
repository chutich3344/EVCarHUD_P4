plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.evcarhud.tramsac"
    compileSdk = 35

    defaultConfig {
        // Package tren Google Play theo chuoi EVCarHUD (namespace code giu nguyen)
        applicationId = "com.evcarhud.charge"
        minSdk = 26
        // Google Play bat buoc app moi nham Android 15 (API 35) tu 31/8/2025
        targetSdk = 35
        versionCode = (System.getenv("GITHUB_RUN_NUMBER") ?: "1").toInt()
        versionName = "0.1." + (System.getenv("GITHUB_RUN_NUMBER") ?: "0")
    }

    // Khoa tai len (upload key) cho Google Play: GitHub Actions giai ma tu Secrets ra file roi dat bien moi truong.
    // Khong co bien -> ky bang khoa debug nhu cu (APK cai thu duoc, nhung Play khong nhan).
    val ksPath = System.getenv("HUD_KEYSTORE_FILE")
    signingConfigs {
        if (ksPath != null) create("upload") {
            storeFile = file(ksPath)
            storePassword = System.getenv("HUD_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("HUD_KEY_ALIAS")
            keyPassword = System.getenv("HUD_KEY_PASSWORD") ?: System.getenv("HUD_KEYSTORE_PASSWORD")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName(if (ksPath != null) "upload" else "debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
