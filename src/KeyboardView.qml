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
    function format(mm) { return Number(mm).toLocaleString(Qt.locale(), 'f', 1) }

    Item {
        id: board
        width: view.unitsWide * view.unit
        height: view.unitsHigh * view.unit
        anchors.centerIn: parent

        MouseArea {
            id: band
            anchors.fill: parent
            preventStealing: true
            property point from
            property bool dragging: false
            onPressed: mouse => { from = Qt.point(mouse.x, mouse.y); dragging = false }
            onPositionChanged: mouse => {
                if (!dragging && Math.hypot(mouse.x - from.x, mouse.y - from.y) > 4)
                    dragging = true
                if (dragging) {
                    rubber.x = Math.min(from.x, mouse.x); rubber.y = Math.min(from.y, mouse.y)
                    rubber.width = Math.abs(mouse.x - from.x); rubber.height = Math.abs(mouse.y - from.y)
                }
            }
            onReleased: mouse => {
                const extend = mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)
                if (!dragging) {
                    if (!extend) backend.selectNone()
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
                color: isSelected ? Qt.alpha(theme.accent, 0.16) : (hover.hovered ? "#1d1d21" : "#17171a")
                border.width: isSelected ? 2 : 1
                border.color: isSelected ? theme.accent : "#26262b"
                opacity: modelData.analog || modelData.color !== "" || isSelected ? 1 : 0.72

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    acceptedModifiers: Qt.KeyboardModifierMask
                    onTapped: (point, button) => backend.select(cap.modelData.code,
                        (point.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                }

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
            visible: band.dragging
            color: Qt.alpha(theme.accent, 0.1)
            border.color: theme.accent
            border.width: 1
            radius: Math.min(4, theme.radius)
        }
    }
}
