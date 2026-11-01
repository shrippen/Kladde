find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
# Kirigami (org.kde.kirigami) is only a QML import at run time, nothing to link.

qt_add_executable(kladde
    main.cpp
    connection.h connection.cpp
    qml/kante.qrc
    qml/kladde.qrc
)
target_link_libraries(kladde PRIVATE kladde-core Qt6::Quick Qt6::QuickControls2)
