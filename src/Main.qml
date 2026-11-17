import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 700
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: "Omni"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property var sel: backend.selected
    readonly property var caps: backend.capabilities
    readonly property bool hasDevice: backend.devices.length > 0
    readonly property bool analogSelected: sel.analogCount > 0
    property bool helpVisible: false

    function mm(value) {
        // Re-evaluated on a language switch so separators follow it.
        translator.language
        return qsTr("%1 mm").arg(Number(value).toLocaleString(Qt.locale(), 'f', decimals(value)))
    }
    // One decimal unless the value sits between tenths (Wooting's 0.05 mm steps).
    function decimals(value) { return Math.abs(value * 10 - Math.round(value * 10)) > 0.001 ? 2 : 1 }
    function nudgeActuation(direction) {
        if (analogSelected)
            backend.setActuation((sel.actuation ?? caps.actuationMin) + direction * caps.actuationStep)
    }
    function nudgeSensitivity(direction) {
        if (analogSelected)
            backend.setPress((sel.press ?? caps.sensitivityMin) + direction * caps.sensitivityStep)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.SelectAll]; context: Qt.ApplicationShortcut; onActivated: backend.selectAll() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible ? win.helpVisible = false : backend.selectNone() }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: win.nudgeActuation(1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: win.nudgeActuation(-1) }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: if (win.analogSelected) backend.setRapidTrigger(!(sel.rapidTrigger && !sel.rapidTriggerMixed)) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; onActivated: win.nudgeSensitivity(-1) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; onActivated: win.nudgeSensitivity(1) }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; onActivated: if (backend.devices.length > 1) backend.current = (backend.current + 1) % backend.devices.length }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 32
            spacing: 20
            visible: win.hasDevice

            // Keyboards, as plain tabs.
            RowLayout {
                spacing: 28
                Layout.fillWidth: true
                Repeater {
                    model: backend.devices
                    delegate: Item {
                        required property var modelData
                        required property int index
                        readonly property bool active: index === backend.current
                        implicitWidth: tab.implicitWidth
                        implicitHeight: tab.implicitHeight + 8
                        Label {
                            id: tab
                            text: modelData.model
                            font.pixelSize: 15
                            font.weight: active ? Font.DemiBold : Font.Normal
                            color: active ? "white" : "#6e6e76"
                        }
                        Rectangle {
                            visible: active
                            width: tab.width; height: 2
                            anchors.bottom: parent.bottom
                            color: theme.accent
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.current = index }
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    readonly property string firmware: backend.devices[backend.current]?.firmware ?? ""
                    visible: firmware !== ""
                    text: qsTr("Firmware %1").arg(firmware)
                    color: "#55555c"
                    font.pixelSize: 12
                }
            }

            Label {
                visible: backend.error !== ""
                text: backend.error
                color: "#ff6b6b"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            KeyboardView {
                keys: backend.keys
                selection: backend.selection
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Label {
                text: sel.count > 0 ? qsTr("%n keys selected", "", sel.count)
                                    : qsTr("Click keys, or drag across them, to change them")
                color: "#6e6e76"
                font.pixelSize: 12
            }

            Rectangle {
                color: "#141417"
                radius: theme.radius
                border.color: "#202024"
                Layout.fillWidth: true
                implicitHeight: controls.implicitHeight + 40

                ColumnLayout {
                    id: controls
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 6

                    SettingRow {
                        label: qsTr("Actuation")
                        enabled: win.analogSelected
                        value: !win.analogSelected ? "–" : sel.actuationMixed ? qsTr("Mixed") : win.mm(sel.actuation)
                        Slider {
                            from: caps.actuationMin ?? 0.1
                            to: caps.actuationMax ?? 4.0
                            stepSize: caps.actuationStep ?? 0.1
                                                        value: sel.actuation ?? from
                            Layout.fillWidth: true
                            onMoved: backend.setActuation(value)
                        }
                    }

                    SettingRow {
                        label: qsTr("Rapid Trigger")
                        enabled: win.analogSelected
                        value: !win.analogSelected || !sel.rapidTrigger ? "–" : sel.pressMixed ? qsTr("Mixed") : win.mm(sel.press)
                        Switch {
                            checked: sel.rapidTrigger === true && !sel.rapidTriggerMixed
                            onToggled: backend.setRapidTrigger(checked)
                        }
                        Slider {
                            enabled: sel.rapidTrigger === true
                            from: caps.sensitivityMin ?? 0.1
                            to: caps.sensitivityMax ?? 1.0
                            stepSize: caps.sensitivityStep ?? 0.1
                                                        value: sel.press ?? from
                            Layout.fillWidth: true
                            onMoved: backend.setPress(value)
                        }
                    }

                    SettingRow {
                        visible: caps.separateRelease === true
                        label: qsTr("Release")
                        enabled: win.analogSelected && sel.rapidTrigger === true
                        value: !enabled ? "–" : sel.releaseMixed ? qsTr("Mixed") : win.mm(sel.release)
                        Slider {
                            from: caps.sensitivityMin ?? 0.1
                            to: caps.sensitivityMax ?? 1.0
                            stepSize: caps.sensitivityStep ?? 0.1
                                                        value: sel.release ?? from
                            Layout.fillWidth: true
                            onMoved: backend.setRelease(value)
                        }
                    }

                    SettingRow {
                        visible: caps.perKeyColor === true
                        label: qsTr("Colour")
                        enabled: sel.count > 0
                        value: sel.count === 0 ? "–" : sel.colorMixed ? qsTr("Mixed") : sel.color === "" ? qsTr("Own") : sel.color.toUpperCase()
                        Repeater {
                            model: ["", "#ffffff", "#ff3b30", "#ff9f0a", "#ffd60a", "#30d158", "#64d2ff", "#0a84ff", "#bf5af2", "#ff375f"]
                            delegate: Rectangle {
                                required property string modelData
                                readonly property bool current: !sel.colorMixed && sel.color === modelData && sel.count > 0
                                width: 22; height: 22; radius: 11
                                color: modelData === "" ? "transparent" : modelData
                                border.width: current ? 2 : 1
                                border.color: current ? "white" : (modelData === "" ? "#6e6e76" : Qt.darker(modelData, 1.4))
                                opacity: parent.enabled ? 1 : 0.35
                                // "Own lighting" is an empty ring with a slash through it.
                                Rectangle {
                                    visible: modelData === ""
                                    anchors.centerIn: parent
                                    width: 16; height: 1.5; rotation: -45
                                    color: "#6e6e76"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.setColor(parent.modelData)
                                }
                                ToolTip.visible: hover.hovered && modelData === ""
                                ToolTip.text: qsTr("The keyboard's own lighting")
                                HoverHandler { id: hover }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        // Any other colour, typed as hex.
                        Rectangle {
                            Layout.preferredWidth: 96
                            implicitHeight: 28
                            radius: Math.min(4, theme.radius)
                            color: "#0e0e10"
                            border.color: hex.activeFocus ? theme.accent : "#2a2a30"
                            opacity: parent.enabled ? 1 : 0.35
                            TextInput {
                                id: hex
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: "white"
                                font.pixelSize: 13
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                                onAccepted: { backend.setColor(text.startsWith("#") ? text : "#" + text); text = ""; focus = false }
                                Keys.onEscapePressed: { text = ""; focus = false }
                                Text {
                                    visible: !hex.text && !hex.activeFocus
                                    text: "#RRGGBB"
                                    color: "#55555c"
                                    font: hex.font
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }
        }

        // Nothing plugged in that omni knows.
        ColumnLayout {
            anchors.centerIn: parent
            visible: !win.hasDevice
            spacing: 10
            Label {
                text: qsTr("No supported keyboard found")
                color: "white"
                font.pixelSize: 18
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: backend.error !== "" ? backend.error : qsTr("Plug in a SteelSeries Apex Pro TKL Gen 3 or a Wooting 60HE v2.")
                color: backend.error !== "" ? "#ff6b6b" : "#6e6e76"
                font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onVisibleChanged: if (!visible) win.helpVisible = false
        }
    }
}
