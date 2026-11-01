import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

// Phase 0: where HealthLog lives, and whether it answers. Token and sync status follow.
Kirigami.ScrollablePage {
    id: page
    title: qsTr("HealthLog")

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        KanteHeading {
            level: 2
            text: qsTr("Server")
        }
        KanteTextField {
            id: serverField
            Layout.fillWidth: true
            placeholderText: "https://hl.example.org"
            text: connection.server
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            onEditingFinished: connection.server = text
        }
        KanteButton {
            text: qsTr("Check connection")
            emphasis: KanteButton.Emphasis.Primary
            busy: connection.state === Connection.Checking
            enabled: serverField.text.length > 0 && !busy
            onClicked: {
                connection.server = serverField.text
                connection.check()
            }
        }
        KanteStatusLight {
            Layout.fillWidth: true
            visible: connection.state !== Connection.Unknown
            status: connection.state === Connection.Reachable ? KanteStatusLight.State.Ok
                  : connection.state === Connection.Checking ? KanteStatusLight.State.Warn
                  : KanteStatusLight.State.Bad
            name: qsTr("HealthLog")
            detail: connection.state === Connection.Reachable ? qsTr("Version %1").arg(connection.serverVersion)
                  : connection.state === Connection.Checking ? qsTr("Checking …")
                  : connection.error
        }
    }
}
