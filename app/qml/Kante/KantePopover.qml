import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Shapes
import "."

/**
 * Popover (Kante 1.43, from lmix): a card that opens at a button and holds a few settings
 * (segments, switches), such as a "View" button over a list. Lighter than a dialog: no
 * title bar and no buttons, every change applies at once; Esc or a click outside closes it.
 * Call `openAt(item)`: it opens below the item, or above when there is no room below, and
 * stays inside the window.
 * `openAtPoint(x, y, item)` (Kante 1.81, for Klappe) opens it beside a point instead, x and y in
 * the coordinates of `item` (a plan canvas, say): to the right of the point, to the left when
 * there is no room, its top near the point and inside the window, with a small pointer on the
 * side facing the point and a clear space around the point itself. `pointerSide` ("left" or
 * "right": the side of the card that bears the pointer; "" after `openAt`) says where it landed.
 *   title    an optional heading (KanteSectionLabel) over the content
 *   closeOnOutside   a press outside closes it (default true); false keeps the card open
 *            beside an element while the user works on the canvas (Esc still closes it)
 * The content goes inside as children (a ColumnLayout is the usual one). Kante: the dialog
 * colour with the focus bar on top and the cut corner (`KantePopupSkin`); Kante Light and
 * System: the platform's popup background.
 */
QQC2.Popup {
    id: popover

    property string title: ""
    property Item anchorItem: null
    property bool closeOnOutside: true
    /** Side of the card that bears the pointer, set by `openAtPoint`: "left", "right" or "" (none). */
    property string pointerSide: ""
    /** Height of the pointer's tip on that side, from the card's top. */
    property real pointerY: 0
    default property alias content: column.data

    /** When it last closed: a press on its own button closes it, the click then must not reopen it. */
    property real closedAt: 0
    onAboutToHide: closedAt = Date.now()

    /** Clear space between the point and the pointer's tip. */
    readonly property real pointerClear: KanteStyle.unit(6)
    readonly property real pointerLength: KanteStyle.unit(8)
    readonly property real pointerWidth: KanteStyle.unit(14)

    // Opens beside the point (x, y) of `item`: right of it, else left, top near the point.
    function openAtPoint(x, y, item) {
        anchorItem = item
        column.forceLayout()
        const win = item.Window.window
        const sceneW = win ? win.width : item.width
        const sceneH = win ? win.height : item.height
        const margin = KanteStyle.unit(4)
        const reach = pointerClear + pointerLength
        const p = item.mapToItem(null, x, y)
        const w = implicitWidth
        const h = implicitHeight
        const right = p.x + reach + w + margin <= sceneW
        const left = !right && p.x - reach - w >= margin
        const sx = right ? p.x + reach : (left ? p.x - reach - w : margin)
        // The pointer sits this far under the card's top, so the card starts a bit above the point.
        const inset = KanteStyle.unit(18)
        const sy = Math.max(margin, Math.min(p.y - inset, sceneH - margin - h))
        // No room on either side: the card covers the point and shows no pointer.
        pointerSide = right ? "left" : (left ? "right" : "")
        // The pointer stays clear of the chamfered top right corner.
        const top = Math.max(inset / 2, KanteStyle.chamferSmall + pointerWidth / 2)
        pointerY = Math.max(top, Math.min(h - inset / 2, p.y - sy))
        parent = item
        const local = item.mapFromItem(null, sx, sy)
        // The parameters x and y hide the popover's own.
        popover.x = local.x
        popover.y = local.y
        open()
    }

    function openAt(item) {
        if (item === anchorItem && Date.now() - closedAt < Qt.styleHints.mouseDoubleClickInterval) {
            return
        }
        pointerSide = ""
        anchorItem = item
        // Content changed just before (another item's settings, rows shown or hidden): the
        // column lays it out only on the next frame, measured now it has the size before.
        column.forceLayout()
        // In scene coordinates, against the window: below the item, above when there is no
        // room below and more above, shifted to stay inside.
        const win = item.Window.window
        const sceneW = win ? win.width : item.width
        const sceneH = win ? win.height : item.height
        const gap = KanteStyle.unit(2)
        const margin = KanteStyle.unit(4)
        const p = item.mapToItem(null, 0, 0)
        const w = implicitWidth
        const h = implicitHeight
        const below = sceneH - p.y - item.height - gap - margin
        const above = p.y - gap - margin
        const up = h > below && above > below
        const sx = Math.max(margin, Math.min(p.x, sceneW - margin - w))
        const sy = up ? Math.max(margin, p.y - gap - h) : p.y + item.height + gap
        // Positioned relative to the item: the overlay need not sit at the scene's origin yet.
        parent = item
        const local = item.mapFromItem(null, sx, sy)
        x = local.x
        y = local.y
        open()
    }

    padding: KanteStyle.unit(14)
    topPadding: KanteStyle.unit(14) + (KanteStyle.themed ? KanteStyle.unit(3) : 0)
    focus: true
    closePolicy: closeOnOutside ? QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
                                : QQC2.Popup.CloseOnEscape
    // Qt keeps the popover this far inside the window.
    margins: KanteStyle.unit(4)

    // A property, not a child: children go into the content column.
    readonly property Item skin: KantePopupSkin {
        popup: popover
    }

    // The pointer: a notch in the card's colours that points at the point. A property, not a child.
    readonly property Item pointer: Shape {
        id: notch
        parent: popover.contentItem ? popover.contentItem.parent : null
        visible: popover.pointerSide !== ""
        antialiasing: true
        readonly property bool onLeft: popover.pointerSide === "left"
        // Beyond the card's edge; its tip points away from the card.
        x: onLeft ? -popover.pointerLength : parent ? parent.width : 0
        y: popover.pointerY - popover.pointerWidth / 2
        width: popover.pointerLength
        height: popover.pointerWidth
        readonly property real tipX: onLeft ? 0 : width
        readonly property real baseX: onLeft ? width : 0

        // The fill reaches one pixel into the card, over its frame.
        ShapePath {
            fillColor: KanteStyle.dialogColor
            strokeWidth: -1
            startX: notch.baseX + (notch.onLeft ? 1 : -1); startY: 0
            PathLine { x: notch.baseX; y: 0 }
            PathLine { x: notch.tipX; y: notch.height / 2 }
            PathLine { x: notch.baseX; y: notch.height }
            PathLine { x: notch.baseX + (notch.onLeft ? 1 : -1); y: notch.height }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: KanteStyle.frameColor
            strokeWidth: 1
            startX: notch.baseX; startY: 0
            PathLine { x: notch.tipX; y: notch.height / 2 }
            PathLine { x: notch.baseX; y: notch.height }
        }
    }

    // As wide as the title too: the Column counts its width, which follows the Column.
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            Math.max(column.implicitWidth, titleLabel.visible ? titleLabel.implicitWidth : 0)
                            + leftPadding + rightPadding)
    contentItem: Column {
        id: column
        spacing: KanteStyle.unit(10)

        KanteSectionLabel {
            id: titleLabel
            visible: popover.title.length > 0
            text: popover.title
            width: parent.width
        }
    }
}
