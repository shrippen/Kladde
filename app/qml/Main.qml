import QtQuick
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

Kirigami.ApplicationWindow {
    id: root
    title: "Kladde"
    width: 420
    height: 780

    // Kante is the app's look on every platform (dark ground; Android runs Material Dark).
    Binding { target: KanteStyle; property: "kind"; value: KanteStyle.Kind.Kante }
    Binding { target: KanteStyle; property: "preferDark"; value: Qt.platform.os === "android" }
    KanteScope { target: root.contentItem }

    // Signed in: the four tabs; otherwise where the server is and who signs in.
    // The open tab of the main page (also set by the demo's screenshot plan).
    property int tab: 0

    readonly property bool signedIn: account.state === Account.SignedIn
    onSignedInChanged: show()
    Component.onCompleted: show()

    function show() {
        pageStack.clear()
        pageStack.push(signedIn ? mainPage : setupPage)
    }

    Component { id: setupPage; SetupPage {} }
    Component { id: mainPage; MainPage {} }
}
