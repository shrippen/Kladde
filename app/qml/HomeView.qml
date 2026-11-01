import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import "Metrics.js" as Metrics

// Home (Lastenheft LF-31): one tile per metric, newest value, change and the last seven
// days. Tapping a tile opens its history.
QQC2.ScrollView {
    id: view
    signal open(string kind)

    contentWidth: availableWidth
    Component.onCompleted: dashboard.reload()

    KantePullToRefresh {
        parent: view
        anchors.fill: parent
        z: 10
        flickable: view.contentItem as Flickable
        busy: dashboard.loading
        onRefreshRequested: dashboard.reload()
    }

    ColumnLayout {
        width: view.availableWidth
        spacing: Kirigami.Units.largeSpacing

        Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            type: Kirigami.MessageType.Error
            visible: dashboard.error.length > 0
            text: dashboard.error
        }

        // The first load: say it is coming (the summary can take seconds on a large account).
        KanteLoader {
            Layout.alignment: Qt.AlignHCenter
            Layout.margins: Kirigami.Units.largeSpacing
            visible: dashboard.loading && dashboard.metrics.length === 0
        }

        KanteEmptyState {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: !dashboard.loading && dashboard.error.length === 0 && dashboard.metrics.length === 0
            title: qsTr("No measurements yet")
            text: qsTr("Values arrive here from the watch and from the Log tab.")
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            columns: Math.max(1, Math.floor(view.availableWidth / Kirigami.Units.gridUnit / 11))
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: dashboard.metrics

                KanteCard {
                    id: card
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: tile.implicitHeight + 2 * Kirigami.Units.largeSpacing
                    interactive: true
                    Accessible.role: Accessible.Button
                    Accessible.name: Metrics.title(modelData.kind) + ": " + modelData.value + " " + modelData.unit

                    TapHandler { onTapped: view.open(card.modelData.kind) }

                    ColumnLayout {
                        id: tile
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        KanteKpi {
                            Layout.fillWidth: true
                            // The unit in the label: the figure is set in capitals, mmHg is not MMHG.
                            label: card.modelData.unit && card.modelData.kind !== "steps"
                                   ? Metrics.title(card.modelData.kind) + " · " + card.modelData.unit
                                   : Metrics.title(card.modelData.kind)
                            value: card.modelData.value
                            trend: card.modelData.trend
                            good: card.modelData.good !== 0 ? card.modelData.good
                                                            : Metrics.goodDirection(card.modelData.kind)
                        }
                        KanteSparkline {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 2
                            visible: card.modelData.sparkline.length > 1
                            values: card.modelData.sparkline
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: !isNaN(card.modelData.updatedAt)
                            color: KanteStyle.mutedTextColor
                            font: KanteStyle.smallFont
                            text: qsTr("Last 7 days · %1").arg(card.modelData.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat))
                        }
                    }
                }
            }
        }
    }
}
