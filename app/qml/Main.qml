import QtQuick
import QtQuick.Controls.Material
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

Kirigami.ApplicationWindow {
    id: root
    title: "Kladde"
    width: 420
    height: 780

    // Kante is the app's look on every platform (dark ground). Android runs Material Dark
    // (main.cpp): Kante reaches Kirigami through the Material colours there, and KanteScope
    // stays off (Kante README, "Material style").
    readonly property bool android: Qt.platform.os === "android"
    Binding { target: KanteStyle; property: "kind"; value: KanteStyle.Kind.Kante }
    Binding { target: KanteStyle; property: "preferDark"; value: root.android }
    Binding { target: KanteStyle; property: "materialStyle"; value: root.android }
    KanteScope { target: root.contentItem }
    Binding { target: root; property: "color"; value: KanteStyle.backgroundColor; when: root.android }
    Binding { target: root.Material; property: "accent"; value: KanteStyle.accentColor; when: root.android }
    Binding { target: root.Material; property: "background"; value: KanteStyle.backgroundColor; when: root.android }
    Binding { target: root.Material; property: "foreground"; value: KanteStyle.textColor; when: root.android }
    Binding { target: root.Material; property: "primary"; value: KanteStyle.backgroundColor; when: root.android }

    // Android's Back key (platform/androidbackfilter.h): "pass" leaves it to Kirigami
    // (pages, dialogs), "handled" means done here, "leave" sends the app to the background.
    function androidBack() {
        if (pageStack.depth > 1)
            return "pass"
        if (signedIn && tab !== 0) {
            tab = 0
            return "handled"
        }
        return "leave"
    }

    // Signed in: the four tabs; otherwise where the server is and who signs in.
    // The open tab of the main page.
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
