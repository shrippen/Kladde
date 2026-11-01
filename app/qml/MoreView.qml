import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante

// More: the account and the background sync (Lastenheft LF-20 … LF-24): what ran when,
// what waits, what went wrong, and the switches for it.
QQC2.ScrollView {
    id: view
    contentWidth: availableWidth

    function when(t) {
        return isNaN(t) ? qsTr("never") : t.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    Component.onCompleted: sync.refresh()

    ColumnLayout {
        width: view.availableWidth - 2 * Kirigami.Units.largeSpacing
        x: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

        KanteHeading {
            level: 4
            text: qsTr("Watch sync")
        }
        KanteStatusLight {
            Layout.fillWidth: true
            visible: !sync.available
            status: KanteStatusLight.State.Off
            name: qsTr("Health Connect")
            detail: sync.reason
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: sync.available
            spacing: Kirigami.Units.largeSpacing

            KanteStatusLight {
                Layout.fillWidth: true
                status: !sync.permitted ? KanteStatusLight.State.Bad
                      : sync.lastError.length > 0 ? KanteStatusLight.State.Warn
                      : KanteStatusLight.State.Ok
                name: qsTr("Health Connect")
                detail: !sync.permitted ? qsTr("No access to the watch's data")
                      : sync.lastError.length > 0 ? sync.lastError
                      : qsTr("Last upload %1").arg(view.when(sync.lastSuccess))
            }
            KanteButton {
                visible: !sync.permitted
                text: qsTr("Allow access")
                emphasis: KanteButton.Emphasis.Primary
                onClicked: sync.requestPermissions()
            }
            KanteStatusLight {
                Layout.fillWidth: true
                visible: sync.silentKinds.length > 0
                status: KanteStatusLight.State.Warn
                name: qsTr("Silent for two days")
                detail: sync.silentKinds.join(", ")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                KanteKpi {
                    Layout.fillWidth: true
                    label: qsTr("Last run")
                    value: view.when(sync.lastRun)
                }
                KanteKpi {
                    Layout.fillWidth: true
                    label: qsTr("Waiting")
                    value: sync.pending
                }
            }
            KanteButton {
                text: qsTr("Sync now")
                enabled: sync.permitted
                onClicked: sync.syncNow()
            }
            KanteSettingRow {
                Layout.fillWidth: true
                title: qsTr("Every")
                hint: qsTr("Minutes between background runs; Android allows 15 at the least.")
                KanteSpinBox {
                    from: 15
                    to: 24 * 60
                    stepSize: 15
                    value: sync.intervalMinutes
                    onValueModified: sync.intervalMinutes = value
                }
            }
            KanteSettingRow {
                Layout.fillWidth: true
                title: qsTr("Days to read back")
                hint: qsTr("On the first run and after reading again.")
                KanteSpinBox {
                    from: 1
                    to: 365
                    value: sync.backfillDays
                    onValueModified: sync.backfillDays = value
                }
            }
            KanteButton {
                text: qsTr("Read everything again")
                onClicked: sync.resync()
            }
        }

        KanteHeading {
            level: 4
            text: qsTr("Account")
        }
        KanteStatusLight {
            Layout.fillWidth: true
            status: KanteStatusLight.State.Ok
            name: account.username
            detail: account.server
        }
        KanteButton {
            text: qsTr("Sign out")
            emphasis: KanteButton.Emphasis.Destructive
            onClicked: account.logout()
        }
    }
}
