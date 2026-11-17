import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The lighting effect, its colours and pace, and how keys react to presses.
ColumnLayout {
    id: panel
    spacing: 6
    readonly property var l: backend.lighting
    readonly property var sel: backend.selected
    readonly property bool moving: l.effect !== "own" && l.effect !== "static"
    readonly property bool usesPalette: moving || l.reactive !== "none"

    readonly property var effects: [
        { id: "own", label: qsTr("Keyboard's own") }, { id: "static", label: qsTr("Static") },
        { id: "breathing", label: qsTr("Breathing") }, { id: "spectrum", label: qsTr("Spectrum") },
        { id: "wave", label: qsTr("Wave") }, { id: "aurora", label: qsTr("Aurora") },
        { id: "starlight", label: qsTr("Starlight") }, { id: "rain", label: qsTr("Rain") },
    ]
    readonly property var reactions: [
        { id: "none", label: qsTr("None") }, { id: "fade", label: qsTr("Glow") },
        { id: "ripple", label: qsTr("Ripple") }, { id: "heatmap", label: qsTr("Heatmap") },
    ]
    readonly property var directions: [
        { id: "right", label: "→" }, { id: "left", label: "←" }, { id: "up", label: "↑" },
        { id: "down", label: "↓" }, { id: "out", label: "◎" },
    ]

    SettingRow {
        label: qsTr("Effect")
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: panel.effects
                delegate: Chip {
                    required property var modelData
                    text: modelData.label
                    checked: panel.l.effect === modelData.id
                    onClicked: backend.setEffect(modelData.id)
                }
            }
        }
    }

    // Static: the selected keys' own colour.
    SettingRow {
        visible: panel.l.effect === "static"
        label: qsTr("Key colour")
        enabled: panel.sel.count > 0
        value: panel.sel.count === 0 ? qsTr("Select keys") : panel.sel.colorMixed ? qsTr("Mixed") : panel.sel.color === "" ? qsTr("Off") : panel.sel.color.toUpperCase()
        Repeater {
            model: ["", "#ffffff", "#ff3b30", "#ff9f0a", "#ffd60a", "#30d158", "#64d2ff", "#0a84ff", "#bf5af2", "#ff375f"]
            delegate: Rectangle {
                required property string modelData
                readonly property bool current: !panel.sel.colorMixed && panel.sel.color === modelData && panel.sel.count > 0
                width: 22; height: 22; radius: 11
                color: modelData === "" ? "transparent" : modelData
                border.width: current ? 2 : 1
                border.color: current ? "white" : (modelData === "" ? "#6e6e76" : Qt.darker(modelData, 1.4))
                opacity: parent.enabled ? 1 : 0.35
                Rectangle {
                    visible: modelData === ""
                    anchors.centerIn: parent
                    width: 16; height: 1.5; rotation: -45
                    color: "#6e6e76"
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.setColor(parent.modelData) }
            }
        }
        Label {
            text: qsTr("More…")
            color: more.containsMouse ? "white" : "#8a8a93"
            font.pixelSize: 12
            MouseArea { id: more; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: keyPicker.open() }
            ColorPicker { id: keyPicker; y: -height - 8; onPicked: color => backend.setColor(color) }
        }
        Item { Layout.fillWidth: true }
    }

    SettingRow {
        visible: panel.usesPalette
        label: qsTr("Colours")
        Chip { text: qsTr("Theme"); checked: panel.l.palette === "theme"; onClicked: backend.setPalette("theme") }
        Chip { text: qsTr("Rainbow"); checked: panel.l.palette === "rainbow"; onClicked: backend.setPalette("rainbow") }
        Chip { text: qsTr("Custom"); checked: panel.l.palette === "custom"; onClicked: backend.setPalette("custom") }
        Item { width: 8 }
        // The colours in play: the theme's, or your own (click to change, + to add).
        Repeater {
            model: panel.l.palette === "custom" ? panel.l.colors : panel.l.palette === "theme" ? panel.l.themeColors : []
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: 22; height: 22; radius: 11
                color: modelData
                border.color: Qt.darker(modelData, 1.4)
                MouseArea {
                    anchors.fill: parent
                    enabled: panel.l.palette === "custom"
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: swatchPicker.open()
                }
                ColorPicker {
                    id: swatchPicker
                    y: -height - 8
                    removable: panel.l.colors.length > 1
                    onPicked: color => { const c = panel.l.colors.slice(); c[index] = color; backend.setPaletteColors(c) }
                    onRemoved: { const c = panel.l.colors.slice(); c.splice(index, 1); backend.setPaletteColors(c) }
                }
            }
        }
        Rectangle {
            visible: panel.l.palette === "custom" && panel.l.colors.length < 4
            width: 22; height: 22; radius: 11
            color: "transparent"
            border.color: "#55555c"
            Label { anchors.centerIn: parent; text: "+"; color: "#a9a9b2"; font.pixelSize: 14 }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: addPicker.open() }
            ColorPicker { id: addPicker; y: -height - 8; onPicked: color => backend.setPaletteColors(panel.l.colors.concat([color])) }
        }
        Item { Layout.fillWidth: true }
    }

    SettingRow {
        visible: panel.l.effect === "wave"
        label: qsTr("Direction")
        Repeater {
            model: panel.directions
            delegate: Chip {
                required property var modelData
                text: modelData.label
                implicitWidth: 36
                checked: panel.l.direction === modelData.id
                onClicked: backend.setDirection(modelData.id)
            }
        }
        Item { Layout.fillWidth: true }
    }

    SettingRow {
        visible: panel.moving || panel.l.reactive !== "none"
        label: qsTr("Speed")
        value: Number(panel.l.speed ?? 1).toLocaleString(Qt.locale(), 'f', 2).replace(/0$/, "") + "×"
        Slider {
            from: 0.25; to: 3; stepSize: 0.05
            value: panel.l.speed ?? 1
            Layout.fillWidth: true
            onMoved: backend.setSpeed(value)
        }
    }

    SettingRow {
        visible: panel.l.drawn === true
        label: qsTr("Brightness")
        value: Math.round((panel.l.brightness ?? 1) * 100) + " %"
        Slider {
            from: 0; to: 1; stepSize: 0.01
            value: panel.l.brightness ?? 1
            Layout.fillWidth: true
            onMoved: backend.setBrightness(value)
        }
    }

    SettingRow {
        label: qsTr("On key press")
        Repeater {
            model: panel.reactions
            delegate: Chip {
                required property var modelData
                text: modelData.label
                checked: panel.l.reactive === modelData.id
                onClicked: backend.setReactive(modelData.id)
            }
        }
        Item { Layout.fillWidth: true }
    }
}
