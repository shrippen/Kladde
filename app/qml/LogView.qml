import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

// Log (Lastenheft LF-30, LF-33): one value by hand, checked against plausible ranges
// before it is sent. Blood pressure goes as systolic and diastolic, the pulse with it.
QQC2.ScrollView {
    id: view
    signal saved()

    contentWidth: availableWidth

    readonly property var kinds: [
        { kind: Entry.Weight, label: qsTr("Weight"),
          fields: [{ name: "value", label: qsTr("Weight"), unit: "kg" }] },
        { kind: Entry.BloodPressure, label: qsTr("Blood pressure"),
          fields: [{ name: "systolic", label: qsTr("Systolic"), unit: "mmHg" },
                   { name: "diastolic", label: qsTr("Diastolic"), unit: "mmHg" },
                   { name: "pulse", label: qsTr("Pulse (optional)"), unit: qsTr("bpm") }] },
        { kind: Entry.Pulse, label: qsTr("Pulse"),
          fields: [{ name: "value", label: qsTr("Pulse"), unit: qsTr("bpm") }] },
        { kind: Entry.BodyFat, label: qsTr("Body fat"),
          fields: [{ name: "value", label: qsTr("Body fat"), unit: "%" }] },
        { kind: Entry.Mood, label: qsTr("Mood"), fields: [] }
    ]
    readonly property var current: kinds[kindBox.currentIndex]
    property var values: ({})
    property int mood: 3
    readonly property var entered: current.kind === Entry.Mood ? { value: mood } : values
    readonly property var invalid: entry.invalidFields(current.kind, entered)

    function fresh() {
        const now = new Date()
        dateField.date = now
        timeField.hour = now.getHours()
        timeField.minute = now.getMinutes()
        values = {}
        noteField.text = ""
        entry.reset()
    }
    function when() {
        const d = dateField.date || new Date()
        return new Date(d.getFullYear(), d.getMonth(), d.getDate(),
                        Math.max(0, timeField.hour), Math.max(0, timeField.minute))
    }

    Component.onCompleted: fresh()
    Connections {
        target: entry
        function onSaved() {
            view.fresh()
            view.saved()
        }
    }

    ColumnLayout {
        width: view.availableWidth - 2 * Kirigami.Units.largeSpacing
        x: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

        KanteComboBox {
            id: kindBox
            Layout.fillWidth: true
            model: view.kinds.map(k => k.label)
            onActivated: {
                view.values = {}
                entry.reset()
            }
            Accessible.name: qsTr("What to log")
        }

        Repeater {
            model: view.current.fields

            ColumnLayout {
                id: field
                required property var modelData
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                readonly property string text: view.values[modelData.name] !== undefined ? view.values[modelData.name] : ""
                readonly property bool bad: text.length > 0 && view.invalid.indexOf(modelData.name) >= 0

                KanteSectionLabel { text: field.modelData.label }
                RowLayout {
                    Layout.fillWidth: true
                    KanteTextField {
                        Layout.fillWidth: true
                        text: field.text
                        invalid: field.bad
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        placeholderText: entry.range(view.current.kind, field.modelData.name)
                        Accessible.name: field.modelData.label
                        onTextEdited: {
                            const v = Object.assign({}, view.values)
                            if (text.length > 0)
                                v[field.modelData.name] = text
                            else
                                delete v[field.modelData.name]
                            view.values = v
                        }
                    }
                    QQC2.Label {
                        text: field.modelData.unit
                        color: KanteStyle.mutedTextColor
                    }
                }
                QQC2.Label {
                    visible: field.bad
                    color: KanteStyle.negativeTextColor
                    font: KanteStyle.smallFont
                    text: field.modelData.name === "diastolic"
                          ? qsTr("Between %1 mmHg and below the systolic value").arg(entry.range(view.current.kind, "diastolic"))
                          : qsTr("Expected %1").arg(entry.range(view.current.kind, field.modelData.name))
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: view.current.kind === Entry.Mood
            KanteSectionLabel { text: qsTr("How are you?") }
            KanteSegmented {
                Layout.fillWidth: true
                model: [qsTr("Awful"), qsTr("Bad"), qsTr("Okay"), qsTr("Good"), qsTr("Great")]
                currentIndex: view.mood - 1
                onActivated: index => view.mood = index + 1
            }
        }

        KanteSectionLabel { text: qsTr("When") }
        RowLayout {
            Layout.fillWidth: true
            KanteDateField {
                id: dateField
                Layout.fillWidth: true
                Accessible.name: qsTr("Date")
            }
            KanteTimeField {
                id: timeField
                Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                Accessible.name: qsTr("Time")
            }
        }

        KanteSectionLabel { text: qsTr("Note") }
        KanteTextField {
            id: noteField
            Layout.fillWidth: true
            placeholderText: qsTr("Optional")
            Accessible.name: qsTr("Note")
        }

        KanteButton {
            Layout.fillWidth: true
            text: qsTr("Save")
            emphasis: KanteButton.Emphasis.Primary
            busy: entry.busy
            enabled: !busy && view.invalid.length === 0 && dateField.date !== null
            onClicked: entry.save(view.current.kind, view.entered, view.when(), noteField.text)
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: entry.error.length > 0
            text: entry.error
        }
    }
}
