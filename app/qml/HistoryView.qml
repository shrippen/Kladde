import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import "Metrics.js" as Metrics

// History (Lastenheft LF-32): one metric over a week, a month, half a year or a year,
// as a line chart with its scale, plus average, range and change.
QQC2.ScrollView {
    id: view
    contentWidth: availableWidth

    readonly property var ranges: [7, 30, 182, 365]
    function withUnit(label) {
        return history.unit && history.kind !== "steps" ? label + " · " + history.unit : label
    }
    Component.onCompleted: history.reload()

    ColumnLayout {
        width: view.availableWidth - 2 * Kirigami.Units.largeSpacing
        x: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

        KanteComboBox {
            Layout.fillWidth: true
            model: history.kinds.map(k => Metrics.title(k))
            currentIndex: history.kinds.indexOf(history.kind)
            onActivated: index => history.kind = history.kinds[index]
            Accessible.name: qsTr("Metric")
        }
        KanteSegmented {
            Layout.fillWidth: true
            model: [qsTr("Week"), qsTr("Month"), qsTr("6 months"), qsTr("Year")]
            currentIndex: view.ranges.indexOf(history.days)
            onActivated: index => history.days = view.ranges[index]
        }

        KanteLineChart {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            visible: history.count > 0
            series: history.series
            labels: history.labels
            minValue: history.minValue
            unit: history.unit
            axis: true
        }
        // The time axis: first and last day of the range shown.
        RowLayout {
            Layout.fillWidth: true
            visible: history.count > 0
            QQC2.Label {
                text: history.labels.length > 0 ? history.labels[0] : ""
                font: KanteStyle.smallFont
                color: KanteStyle.mutedTextColor
            }
            Item { Layout.fillWidth: true }
            QQC2.Label {
                text: history.labels.length > 0 ? history.labels[history.labels.length - 1] : ""
                font: KanteStyle.smallFont
                color: KanteStyle.mutedTextColor
            }
        }
        // Blood pressure has two lines: say which is which.
        RowLayout {
            visible: history.kind === "bloodPressure" && history.count > 0
            spacing: Kirigami.Units.largeSpacing
            Repeater {
                model: [qsTr("Systolic"), qsTr("Diastolic")]
                RowLayout {
                    required property string modelData
                    required property int index
                    KanteSwatch {
                        size: KanteSwatch.Size.Dense
                        swatchColor: KanteStyle.dataColor(parent.index)
                    }
                    QQC2.Label {
                        text: parent.modelData
                        font: KanteStyle.smallFont
                    }
                }
            }
        }

        KanteLoader {
            Layout.alignment: Qt.AlignHCenter
            visible: history.loading && history.count === 0
        }
        KanteEmptyState {
            Layout.fillWidth: true
            visible: !history.loading && history.count === 0 && history.error.length === 0
            title: qsTr("Nothing in this range")
            text: qsTr("Choose a longer range or another metric.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: history.error.length > 0
            text: history.error
        }

        GridLayout {
            Layout.fillWidth: true
            visible: history.count > 0
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            KanteKpi {
                Layout.fillWidth: true
                label: view.withUnit(qsTr("Average"))
                value: history.mean
            }
            KanteKpi {
                Layout.fillWidth: true
                // HealthLog's change is that of the first value (systolic for blood pressure).
                label: view.withUnit(history.kind === "bloodPressure" ? qsTr("Change, systolic") : qsTr("Change"))
                value: history.change.length > 0 ? history.change : "–"
                trend: history.changeTrend
                good: history.changeGood !== 0 ? history.changeGood : Metrics.goodDirection(history.kind)
            }
            KanteKpi {
                Layout.fillWidth: true
                label: view.withUnit(qsTr("Lowest"))
                value: history.low
            }
            KanteKpi {
                Layout.fillWidth: true
                label: view.withUnit(qsTr("Highest"))
                value: history.high
            }
        }
    }
}
