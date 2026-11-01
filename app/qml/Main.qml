import QtQuick
import org.kde.kirigami as Kirigami
import Kante

Kirigami.ApplicationWindow {
    id: root
    title: "Kladde"
    width: 420
    height: 760

    // Kante is the app's look on every platform (dark ground; Android runs Material Dark).
    Binding { target: KanteStyle; property: "kind"; value: KanteStyle.Kind.Kante }
    Binding { target: KanteStyle; property: "preferDark"; value: Qt.platform.os === "android" }
    KanteScope { target: root.contentItem }

    pageStack.initialPage: ConnectionPage {}
}
