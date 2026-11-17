import QtQuick
import QtQuick.Controls.Material

// The keyboard drawn to scale. Click selects a key, Shift- or Ctrl-click adds
// or removes one, and dragging over empty space selects everything it covers.
Item {
    id: view

    property var keys: []
    property var selection: []
    readonly property real unitsWide: keys.reduce((m, k) => Math.max(m, k.x + k.w), 1)
    readonly property real unitsHigh: keys.reduce((m, k) => Math.max(m, k.y + k.h), 1)
    readonly property real unit: Math.min(width / unitsWide, height / unitsHigh)
    readonly property real gap: Math.max(2, unit * 0.08)
    readonly property real radius: Math.min(Math.max(2, unit * 0.12), theme.radius > 0 ? unit * 0.2 : 2)

    function selected(code) { return selection.indexOf(code) >= 0 }
    function format(mm) {
        const decimals = Math.abs(mm * 10 - Math.round(mm * 10)) > 0.001 ? 2 : 1
        return Number(mm).toLocaleString(Qt.locale(), 'f', decimals)
    }

    Item {
        id: board
        width: view.unitsWide * view.unit
        height: view.unitsHigh * view.unit
        anchors.centerIn: parent

        Repeater {
            model: view.keys
            delegate: Rectangle {
                id: cap
                required property var modelData
                readonly property bool isSelected: view.selected(modelData.code)
                x: modelData.x * view.unit + view.gap / 2
                y: modelData.y * view.unit + view.gap / 2
                width: modelData.w * view.unit - view.gap
                height: modelData.h * view.unit - view.gap
                radius: view.radius
                color: isSelected ? Qt.alpha(theme.accent, 0.16) : (hovered ? "#1d1d21" : "#17171a")
                border.width: isSelected ? 2 : 1
                border.color: isSelected ? theme.accent : "#26262b"
                opacity: modelData.analog || modelData.color !== "" || isSelected ? 1 : 0.72

                readonly property bool hovered: pointer.containsMouse && !pointer.dragging
                    && pointer.hoverX >= x && pointer.hoverX < x + width && pointer.hoverY >= y && pointer.hoverY < y + height

                // The key's own colour, as a strip along the bottom.
                Rectangle {
                    visible: cap.modelData.color !== ""
                    color: cap.modelData.color || "transparent"
                    height: Math.max(2, view.unit * 0.07)
                    radius: height / 2
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: view.unit * 0.12 }
                }

                Text {
                    text: cap.modelData.label
                    color: "#8a8a93"
                    font.pixelSize: Math.max(8, Math.round(view.unit * (text.length > 2 ? 0.17 : 0.2)))
                    anchors { left: parent.left; top: parent.top; leftMargin: view.unit * 0.12; topMargin: view.unit * 0.08 }
                }

                Text {
                    visible: cap.modelData.analog
                    text: view.format(cap.modelData.actuation)
                    color: cap.isSelected ? "white" : "#d8d8de"
                    font.pixelSize: Math.max(9, Math.round(view.unit * 0.26))
                    font.weight: Font.Medium
                    font.features: { "tnum": 1 }
                    anchors { right: parent.right; bottom: parent.bottom; rightMargin: view.unit * 0.12; bottomMargin: view.unit * 0.14 }
                }

                // Rapid Trigger is on for this key.
                Rectangle {
                    visible: cap.modelData.rapidTrigger
                    width: Math.max(4, view.unit * 0.1); height: width; radius: width / 2
                    color: theme.accent
                    anchors { right: parent.right; top: parent.top; margins: view.unit * 0.12 }
                }
            }
        }

        Rectangle {
            id: rubber
            z: 1
            visible: pointer.dragging
            color: Qt.alpha(theme.accent, 0.1)
            border.color: theme.accent
            border.width: 1
            radius: Math.min(4, theme.radius)
        }
    }

    // One pointer area over the whole view, above the keys: a click selects the
    // key under it (Shift or Ctrl adds or removes it), a drag that starts
    // anywhere, on a key or not, selects every key the box touches.
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: keyAt(mouseX, mouseY) ? Qt.PointingHandCursor : Qt.ArrowCursor
        property point from
        property bool dragging: false
        property real hoverX: -1
        property real hoverY: -1

        function boardPoint(x, y) { return mapToItem(board, x, y) }
        function keyAt(x, y) {
            const p = boardPoint(x, y), u = view.unit
            for (const k of view.keys)
                if (p.x >= k.x * u && p.x < (k.x + k.w) * u && p.y >= k.y * u && p.y < (k.y + k.h) * u)
                    return k
            return null
        }
        onPositionChanged: mouse => {
            const p = boardPoint(mouse.x, mouse.y)
            hoverX = p.x; hoverY = p.y
            if (!pressed)
                return
            if (!dragging && Math.hypot(mouse.x - from.x, mouse.y - from.y) > 6)
                dragging = true
            if (dragging) {
                const a = boardPoint(from.x, from.y)
                rubber.x = Math.min(a.x, p.x); rubber.y = Math.min(a.y, p.y)
                rubber.width = Math.abs(p.x - a.x); rubber.height = Math.abs(p.y - a.y)
            }
        }
        onExited: { hoverX = -1; hoverY = -1 }
        onPressed: mouse => { from = Qt.point(mouse.x, mouse.y); dragging = false }
        onReleased: mouse => {
            const extend = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
            if (!dragging) {
                const key = keyAt(mouse.x, mouse.y)
                if (key)
                    backend.select(key.code, extend)
                else if (!extend)
                    backend.selectNone()
                return
            }
            const u = view.unit, codes = []
            for (const k of view.keys) {
                const kx = k.x * u, ky = k.y * u, kw = k.w * u, kh = k.h * u
                if (kx < rubber.x + rubber.width && kx + kw > rubber.x && ky < rubber.y + rubber.height && ky + kh > rubber.y)
                    codes.push(k.code)
            }
            backend.setSelection(codes, extend)
            dragging = false
        }
    }
}
