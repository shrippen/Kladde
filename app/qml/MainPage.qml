import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante

// The four tabs, as the iOS app's tab bar (without medication, which Kladde does not do):
// Home, Log, History, More.
Kirigami.Page {
    id: page
    title: tabs.model[tabs.currentIndex]
    padding: 0

    // Home's tiles open the history of their kind.
    function showHistory(kind) {
        history.kind = kind
        applicationWindow().tab = 2
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        HomeView { onOpen: kind => page.showHistory(kind) }
        LogView { onSaved: applicationWindow().tab = 0 }
        HistoryView {}
        MoreView {}
    }

    footer: KanteTabBar {
        id: tabs
        model: [qsTr("Home"), qsTr("Log"), qsTr("History"), qsTr("More")]
        icons: ["go-home", "list-add", "office-chart-line", "application-menu"]
        currentIndex: applicationWindow().tab
        onActivated: index => applicationWindow().tab = index
    }
}
