import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

// More: the account and the background sync (Lastenheft LF-20 … LF-24): what ran when,
// what waits, what went wrong, and the switches for it.
QQC2.ScrollView {
    id: view
    contentWidth: availableWidth

    function when(t) {
        return isNaN(t) ? qsTr("never") : t.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    Component.onCompleted: {
        sync.refresh()
        if (visible) {
            sync.loadApps()
        }
    }
    // The tabs create every page at the start: read Health Connect only once shown.
    onVisibleChanged: {
        if (visible) {
            sync.loadApps()
        }
    }

    // One kind's apps, first wins an overlap; moved with the arrows. Two apps at the least,
    // one alone has nothing to win against.
    component AppOrder: ColumnLayout {
        id: order
        property string title
        property var apps: []
        property int kind

        visible: apps.length > 1
        spacing: 0

        KanteSectionLabel {
            text: order.title
        }
        Repeater {
            model: order.apps
            KanteListRow {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                leadingText: index + 1
                text: modelData.label
                subtitle: modelData.app
                focusOnClick: false
                // The slot stacks its items; side by side in a row.
                trailing: Row {
                    KanteToolButton {
                        icon.name: "go-up"
                        text: qsTr("Up")
                        display: QQC2.AbstractButton.IconOnly
                        enabled: index > 0
                        onClicked: sync.moveApp(order.kind, index, index - 1)
                    }
                    KanteToolButton {
                        icon.name: "go-down"
                        text: qsTr("Down")
                        display: QQC2.AbstractButton.IconOnly
                        enabled: index < order.apps.length - 1
                        onClicked: sync.moveApp(order.kind, index, index + 1)
                    }
                }
            }
        }
    }

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
                visible: !sync.complete
                text: sync.permitted ? qsTr("Allow more kinds") : qsTr("Allow access")
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
                busy: sync.running
                enabled: sync.permitted && !busy
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

            KanteHeading {
                level: 4
                visible: sleepOrder.visible || workoutOrder.visible
                text: qsTr("App order")
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: sleepOrder.visible || workoutOrder.visible
                wrapMode: Text.Wrap
                color: KanteStyle.mutedTextColor
                text: qsTr("Where two apps saw the same night or training, the upper one's goes to HealthLog. Applies to nights and trainings that begin from now on; those before keep the order they were sent by.")
            }
            AppOrder {
                id: sleepOrder
                Layout.fillWidth: true
                title: qsTr("Sleep")
                apps: sync.sleepApps
                kind: SyncBridge.Sleep
            }
            AppOrder {
                id: workoutOrder
                Layout.fillWidth: true
                title: qsTr("Trainings")
                apps: sync.workoutApps
                kind: SyncBridge.Workouts
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
