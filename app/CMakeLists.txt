find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 LinguistTools)
# Kirigami (org.kde.kirigami) is only a QML import at run time, nothing to link.

qt_add_executable(kladde
    main.cpp
    account.h account.cpp
    dashboard.h dashboard.cpp
    history.h history.cpp
    entry.h entry.cpp
    syncbridge.h syncbridge.cpp
    qml/kante.qrc
    qml/kladde.qrc
)
target_link_libraries(kladde PRIVATE kladde-core Qt6::Quick Qt6::QuickControls2)

# German next to the English source texts (`cmake --build … --target update_translations`
# after changing a text, then translate in i18n/kladde_de.ts).
file(GLOB kladde_qml CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js)
qt_add_lupdate(kladde TS_FILES i18n/kladde_de.ts SOURCES
    ${kladde_qml} account.cpp entry.cpp syncbridge.cpp NO_GLOBAL_TARGET)
qt_add_lrelease(kladde TS_FILES i18n/kladde_de.ts QM_FILES_OUTPUT_VARIABLE kladde_qm NO_GLOBAL_TARGET)
qt_add_resources(kladde kladde_i18n PREFIX /i18n BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${kladde_qm})

if(KLADDE_DEMO)
    # The world is copied from shrippen.github.io/demo (sync-demo.py), never edited here.
    target_compile_definitions(kladde PRIVATE KLADDE_DEMO)
    target_sources(kladde PRIVATE demo/demobackend.h demo/demobackend.cpp)
    qt_add_resources(kladde demo_world PREFIX /demo BASE ${PROJECT_SOURCE_DIR}/demo
                     FILES ${PROJECT_SOURCE_DIR}/demo/world.json)
elseif(NOT ANDROID)
    # The refresh token goes to KWallet / Secret Service (Lastenheft LF-02).
    find_package(Qt6Keychain REQUIRED)
    target_sources(kladde PRIVATE keychainstore.h keychainstore.cpp)
    target_link_libraries(kladde PRIVATE Qt6Keychain::Qt6Keychain)
endif()
