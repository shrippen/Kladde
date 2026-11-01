// Kladde's Android sync: reads Health Connect, maps to HealthLog's batch format, queues and
// uploads in the background (WorkManager). The Qt app talks to it through KladdeSync (JNI).
// The mapping must match core/mapper.cpp: both test against tests/fixtures/*.expected.json.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "de.arianw.kladde.sync"
    compileSdk = 36
    defaultConfig {
        minSdk = 34 // Health Connect is part of the system from Android 14 (Lastenheft 3)
        // Tests on a phone (src/androidTest): ./gradlew :sync:connectedDebugAndroidTest
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
    }
    sourceSets {
        getByName("test").resources.srcDir("../../tests/fixtures")
    }
}

// Bytecode for Java 17 from whatever JDK runs Gradle (CI: 17; Fedora 44 ships only 25).
kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.health.connect:connect-client:1.1.0")
    implementation("androidx.work:work-runtime-ktx:2.10.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")

    testImplementation("junit:junit:4.13.2")
    // Android's org.json is a stub in JVM unit tests; the real one for the tests.
    testImplementation("org.json:json:20250517")

    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
