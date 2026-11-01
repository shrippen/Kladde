find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 LinguistTools)
# Kirigami (org.kde.kirigami) is only a QML import at run time, nothing to link.

qt_add_executable(kladde
    main.cpp
    account.h account.cpp
    dashboard.h dashboard.cpp
    history.h history.cpp
    entry.h entry.cpp
    errors.h errors.cpp
    syncbridge.h syncbridge.cpp
    keychainstore.h keychainstore.cpp
    qml/kante.qrc
    qml/kladde.qrc
)
target_link_libraries(kladde PRIVATE kladde-core Qt6::Quick Qt6::QuickControls2)

# The refresh token goes to the platform keychain (Lastenheft LF-02): KWallet / Secret
# Service on Linux, a key in the Android Keystore on Android (QtKeychain built statically by
# scripts/build-deps-android.sh).
find_package(Qt6Keychain REQUIRED)
target_link_libraries(kladde PRIVATE Qt6Keychain::Qt6Keychain)

# The background sync: on Android the Kotlin module android/sync through JNI.
if(ANDROID)
    # No icon theme on Android: the Breeze Dark icons the app and Kirigami use come along.
    target_sources(kladde PRIVATE syncbridge_android.cpp platform/androidbackfilter.h icons/icons.qrc)
else()
    target_sources(kladde PRIVATE syncbridge_none.cpp)
endif()

# German next to the English source texts (`cmake --build … --target update_translations`
# after changing a text, then translate in i18n/kladde_de.ts).
file(GLOB kladde_qml CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js)
qt_add_lupdate(kladde TS_FILES i18n/kladde_de.ts SOURCES
    ${kladde_qml} account.cpp entry.cpp errors.cpp syncbridge_none.cpp syncbridge_android.cpp NO_GLOBAL_TARGET OPTIONS -no-obsolete)
qt_add_lrelease(kladde TS_FILES i18n/kladde_de.ts QM_FILES_OUTPUT_VARIABLE kladde_qm NO_GLOBAL_TARGET)
qt_add_resources(kladde kladde_i18n PREFIX /i18n BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${kladde_qm})

if(KLADDE_DEMO)
    # The world is copied from shrippen.github.io/demo (sync-demo.py), never edited here.
    target_compile_definitions(kladde PRIVATE KLADDE_DEMO)
    target_sources(kladde PRIVATE demo/demobackend.h demo/demobackend.cpp)
    qt_add_resources(kladde demo_world PREFIX /demo BASE ${PROJECT_SOURCE_DIR}/demo
                     FILES ${PROJECT_SOURCE_DIR}/demo/world.json)
endif()

if(ANDROID)
    # Version code: major·10000 + minor·100 + patch, so every release counts up.
    math(EXPR kladde_version_code
         "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    # The package source is generated: build.gradle needs the absolute path of android/sync.
    set(kladde_package ${CMAKE_CURRENT_BINARY_DIR}/android-package)
    set(KLADDE_SYNC_DIR ${PROJECT_SOURCE_DIR}/android/sync)
    configure_file(android/build.gradle.in ${kladde_package}/build.gradle @ONLY)
    file(GLOB_RECURSE kladde_android_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/android
         ${CMAKE_CURRENT_SOURCE_DIR}/android/*)
    list(REMOVE_ITEM kladde_android_files build.gradle.in)
    foreach(f IN LISTS kladde_android_files)
        configure_file(android/${f} ${kladde_package}/${f} COPYONLY)
    endforeach()

    set_target_properties(kladde PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${kladde_package}
        QT_ANDROID_PACKAGE_NAME "de.arianw.kladde"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${kladde_version_code}"
        # Health Connect is part of the system from Android 14 (Lastenheft 3).
        QT_ANDROID_MIN_SDK_VERSION "34"
        QT_ANDROID_TARGET_SDK_VERSION "34"
        # Pinned, not "newest installed": the same SDK on every machine.
        QT_ANDROID_COMPILE_SDK_VERSION "36"
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
    )

    # HTTPS: Qt loads libssl_3.so / libcrypto_3.so at run time, and Kirigami's QML plugins
    # need the NDK's libomp.so (androiddeployqt does not find either by itself).
    set(KLADDE_ANDROID_DEPS "" CACHE PATH "Prefix of scripts/build-deps-android.sh")
    file(GLOB kladde_libomp "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/*/lib/linux/aarch64/libomp.so")
    set(kladde_ssl ${KLADDE_ANDROID_DEPS}/openssl/libssl_3.so ${KLADDE_ANDROID_DEPS}/openssl/libcrypto_3.so)
    foreach(lib IN LISTS kladde_ssl kladde_libomp)
        if(NOT EXISTS "${lib}")
            message(FATAL_ERROR "Android: ${lib} missing (run scripts/build-deps-android.sh)")
        endif()
    endforeach()
    list(GET kladde_libomp 0 kladde_libomp)
    # The libraries behind Kirigami's QML plugins: the app does not link them, so
    # androiddeployqt does not follow them (tests/check_apk_libs.py).
    file(GLOB kladde_kirigami "${KLADDE_ANDROID_DEPS}/lib/libKirigami*.so")
    if(NOT kladde_kirigami)
        message(FATAL_ERROR "Android: no libKirigami*.so in ${KLADDE_ANDROID_DEPS}/lib")
    endif()
    # Kirigami itself needs Qt Concurrent.
    list(APPEND kladde_kirigami "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}/libQt6Concurrent_${ANDROID_ABI}.so")
    set_target_properties(kladde PROPERTIES QT_ANDROID_EXTRA_LIBS "${kladde_ssl};${kladde_libomp};${kladde_kirigami}")

    # Kirigami's QML modules come from the dependency prefix, not from Qt.
    get_filename_component(kladde_kf6_qml "${KLADDE_ANDROID_DEPS}/lib/qml" ABSOLUTE)
    set_target_properties(kladde PROPERTIES _qt_native_qml_import_paths "${kladde_kf6_qml}")
endif()
