import QtQuick
import QtQuick.Controls as QQC2
import "."

/**
 * Tracks over time (Kante 1.81, for Klappe): a ruler with second ticks, one row per track,
 * segments as bars, markers as diamonds and a playhead over all rows. Presentational: the app
 * owns `position` and `tracks` and answers the signals.
 *
 *   duration    length of the time axis in seconds
 *   position    the playhead in seconds
 *   tracks      [{ name, label, shown, segments: [{ from, to, text, selected }],
 *               markers: [{ at, text }] }]; seconds. label: a colour name of
 *               KanteStyle.labels, or "text", "muted", "accent", "focus", "warning";
 *               "" or an unknown name gives the text colour. shown false: the row is dimmed
 *   trackHeight height of a row, labelWidth width of the name column (KanteStyle units)
 *   hideText, showText   the app's words for the toggle: "<hideText>: <name>" while shown,
 *               "<showText>: <name>" while hidden (tooltip and name for screen readers)
 *   seek(seconds)               click or drag on the ruler or a row; Left / Right move one
 *                               second, Home / End go to the start or end (the item takes Tab)
 *   toggleShown(track)          the eye button of a row
 *   segmentClicked(track, segment)
 *
 * Information never by colour alone: a segment carries its text, a selected one is framed
 * wider and in bold, a marker is a diamond with its text as a tooltip, a hidden row shows
 * the crossed eye.
 *
 * Functions for the app and its tests: timeAt(x), xAt(seconds) (in the item's coordinates),
 * segmentAt(x, y) and markerAt(x, y), each {track, index} or null.
 */
FocusScope {
    id: root

    property real duration: 10
    property real position: 0
    property var tracks: []
    property real trackHeight: KanteStyle.unit(34)
    property real labelWidth: KanteStyle.unit(150)
    property string hideText: "Hide"
    property string showText: "Show"
    /** Name for screen readers. */
    property string accessibleName: "Timeline"

    signal seek(real seconds)
    signal toggleShown(int track)
    signal segmentClicked(int track, int segment)

    readonly property real rulerHeight: KanteStyle.unit(24)
    readonly property real laneWidth: Math.max(1, width - labelWidth)
    readonly property real pixelsPerSecond: laneWidth / Math.max(0.001, duration)
    // The ruler's step in seconds: the smallest of these that leaves a label its room.
    readonly property var steps: [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600]
    readonly property real tickStep: {
        const room = KanteStyle.unit(56)
        for (const s of steps) {
            if (s * pixelsPerSecond >= room) {
                return s
            }
        }
        return steps[steps.length - 1]
    }
    readonly property int tickCount: Math.floor(Math.max(0, duration) / tickStep) + 1
    readonly property real markerSize: KanteStyle.unit(12)
    property var hovered: null
    onTracksChanged: hovered = null

    implicitWidth: KanteStyle.unit(640)
    implicitHeight: rulerHeight + tracks.length * trackHeight
    activeFocusOnTab: true
    clip: true

    Accessible.role: Accessible.Slider
    Accessible.name: accessibleName
    Accessible.description: Math.round(position) + " / " + Math.round(duration) + " s"

    function _clampTime(t) {
        return Math.max(0, Math.min(duration, t))
    }

    /** Seconds at an x of this item. */
    function timeAt(x) {
        return _clampTime((x - labelWidth) / pixelsPerSecond)
    }

    /** The x of this item at `seconds`. */
    function xAt(seconds) {
        return labelWidth + seconds * pixelsPerSecond
    }

    function _trackAt(y) {
        const row = Math.floor((y - rulerHeight) / trackHeight)
        return row >= 0 && row < tracks.length ? row : -1
    }

    /** The segment under a point of this item: {track, index}, or null. */
    function segmentAt(x, y) {
        const t = _trackAt(y)
        if (t < 0) {
            return null
        }

        const secs = (x - labelWidth) / pixelsPerSecond
        const segs = tracks[t].segments || []
        // Topmost (last drawn) first.
        for (let i = segs.length - 1; i >= 0; i--) {
            if (secs >= segs[i].from && secs <= segs[i].to) {
                return { track: t, index: i }
            }
        }
        return null
    }

    /** The marker under a point of this item: {track, index}, or null. */
    function markerAt(x, y) {
        const t = _trackAt(y)
        if (t < 0) {
            return null
        }

        const marks = tracks[t].markers || []
        const reach = markerSize / 2
        for (let i = marks.length - 1; i >= 0; i--) {
            if (Math.abs(xAt(marks[i].at) - x) <= reach) {
                return { track: t, index: i }
            }
        }
        return null
    }

    // Colour of a label name; the same names as KanteShapeCanvas.
    function _labelColor(name) {
        switch (name) {
        case "muted": return KanteStyle.mutedTextColor
        case "accent": return KanteStyle.accentColor
        case "focus": return KanteStyle.focusColor
        case "warning": return KanteStyle.warningColor
        }
        const c = KanteStyle.labelColor(name)
        return c.a > 0 ? c : KanteStyle.textColor
    }

    function _clock(s) {
        const m = Math.floor(s / 60)
        const r = s % 60
        return m > 0 ? m + ":" + (r < 10 ? "0" : "") + r : String(r)
    }

    Keys.onLeftPressed: root.seek(_clampTime(position - 1))
    Keys.onRightPressed: root.seek(_clampTime(position + 1))
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Home) {
            root.seek(0)
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            root.seek(duration)
            event.accepted = true
        }
    }

    // Ruler: a tick and a label per step.
    Item {
        id: ruler
        x: root.labelWidth
        width: root.laneWidth
        height: root.rulerHeight

        Repeater {
            model: root.tickCount
            delegate: Item {
                required property int index
                readonly property real secs: index * root.tickStep
                x: secs * root.pixelsPerSecond
                height: ruler.height

                Rectangle {
                    y: parent.height - height
                    width: 1
                    height: KanteStyle.unit(6)
                    color: KanteStyle.mutedTextColor
                }
                QQC2.Label {
                    x: KanteStyle.unit(3)
                    width: Math.max(0, root.tickStep * root.pixelsPerSecond - KanteStyle.unit(3))
                    text: root._clock(parent.secs)
                    font: KanteStyle.smallFont
                    color: KanteStyle.mutedTextColor
                    elide: Text.ElideRight
                }
            }
        }
    }

    Rectangle {
        y: root.rulerHeight - 1
        width: root.width
        height: 1
        color: KanteStyle.ruleColor
    }

    // Rows.
    Repeater {
        model: root.tracks
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool shown: modelData.shown !== false

            y: root.rulerHeight + index * root.trackHeight
            width: root.width
            height: root.trackHeight

            Rectangle {
                y: parent.height - 1
                width: parent.width
                height: 1
                color: KanteStyle.ruleColor
            }

            Row {
                x: KanteStyle.unit(4)
                width: root.labelWidth - KanteStyle.unit(8)
                height: parent.height
                spacing: KanteStyle.unit(4)

                KanteToolButton {
                    id: toggle
                    objectName: "toggle" + row.index
                    anchors.verticalCenter: parent.verticalCenter
                    icon.name: row.shown ? "view-visible" : "view-hidden"
                    display: KanteToolButton.IconOnly
                    focusPolicy: Qt.TabFocus
                    text: (row.shown ? root.hideText : root.showText) + ": " + row.modelData.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    Accessible.name: text
                    onClicked: root.toggleShown(row.index)
                }
                QQC2.Label {
                    width: parent.width - toggle.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.name
                    font: KanteStyle.smallFont
                    color: KanteStyle.textColor
                    opacity: row.shown ? 1 : 0.4
                    elide: Text.ElideRight
                }
            }

            // Lane content, dimmed as a whole when hidden.
            Item {
                x: root.labelWidth
                width: root.laneWidth
                height: parent.height
                opacity: row.shown ? 1 : 0.35
                clip: true

                Repeater {
                    model: row.modelData.segments || []
                    delegate: Rectangle {
                        id: seg
                        required property var modelData
                        readonly property color fill: root._labelColor(row.modelData.label)
                        readonly property bool picked: modelData.selected === true

                        x: modelData.from * root.pixelsPerSecond
                        y: KanteStyle.unit(4)
                        width: Math.max(1, (modelData.to - modelData.from) * root.pixelsPerSecond)
                        height: row.height - KanteStyle.unit(8)
                        color: fill
                        border.width: picked ? KanteStyle.unit(3) : 0
                        border.color: KanteStyle.focusColor

                        QQC2.Label {
                            anchors.fill: parent
                            anchors.leftMargin: KanteStyle.unit(5)
                            anchors.rightMargin: KanteStyle.unit(3)
                            verticalAlignment: Text.AlignVCenter
                            text: seg.modelData.text || ""
                            font.family: KanteStyle.smallFont.family
                            font.pixelSize: KanteStyle.fontPixels(KanteStyle.smallFont)
                            font.bold: seg.picked
                            color: KanteStyle.inkOn(seg.fill)
                            elide: Text.ElideRight
                        }
                    }
                }

                Repeater {
                    model: row.modelData.markers || []
                    delegate: Rectangle {
                        id: mark
                        required property var modelData
                        // A diamond: a square turned by 45 degrees, its diagonal as wide as markerSize.
                        readonly property real side: root.markerSize / Math.SQRT2

                        x: modelData.at * root.pixelsPerSecond - width / 2
                        y: (row.height - height) / 2
                        width: side
                        height: side
                        rotation: 45
                        color: KanteStyle.backgroundColor
                        border.width: KanteStyle.unit(2)
                        border.color: root._labelColor(row.modelData.label)
                    }
                }
            }
        }
    }

    // Playhead over ruler and rows.
    Rectangle {
        x: root.xAt(root._clampTime(root.position)) - width / 2
        width: KanteStyle.unit(2)
        height: root.height
        color: KanteStyle.focusColor
        visible: root.width > root.labelWidth
    }

    MouseArea {
        id: area
        x: root.labelWidth
        width: root.laneWidth
        height: root.height
        hoverEnabled: true

        function _seekTo(mx) {
            root.forceActiveFocus()
            root.seek(root.timeAt(mx + root.labelWidth))
        }

        onPressed: function (mouse) { _seekTo(mouse.x) }
        onPositionChanged: function (mouse) {
            if (pressed) {
                _seekTo(mouse.x)
                return
            }
            root.hovered = root.markerAt(mouse.x + root.labelWidth, mouse.y)
        }
        onExited: root.hovered = null
        onClicked: function (mouse) {
            const hit = root.segmentAt(mouse.x + root.labelWidth, mouse.y)
            if (hit) {
                root.segmentClicked(hit.track, hit.index)
            }
        }

        QQC2.ToolTip {
            visible: root.hovered !== null && area.containsMouse
            text: {
                const track = root.hovered ? root.tracks[root.hovered.track] : null
                const mark = track && track.markers ? track.markers[root.hovered.index] : null
                return mark && mark.text ? mark.text : ""
            }
        }
    }

    // Focus frame.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: root.activeFocus ? KanteStyle.unit(2) : 0
        border.color: KanteStyle.focusColor
    }
}
