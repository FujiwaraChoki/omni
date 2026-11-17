import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 520
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
    property int tab: 0 // 0 keys, 1 lighting, 2 screen

    function mm(value) {
        // Re-evaluated on a language switch so separators follow it.
        translator.language
        return qsTr("%1 mm").arg(Number(value).toLocaleString(Qt.locale(), 'f', decimals(value)))
    }
    // One decimal unless the value sits between tenths (Wooting's 0.05 mm steps).
    function decimals(value) { return Math.abs(value * 10 - Math.round(value * 10)) > 0.001 ? 2 : 1 }
    function nudgeActuation(direction) {
        if (!analogSelected)
            return
        const current = sel.actuation ?? caps.actuationMin
        // Keyboards with a fixed set of depths step to the next one of those.
        const stops = (caps.actuationStops || []).filter(s => direction > 0 ? s > current + 0.001 : s < current - 0.001)
        if (stops.length)
            backend.setActuation(direction > 0 ? Math.min(...stops) : Math.max(...stops))
        else if (!(caps.actuationStops || []).length)
            backend.setActuation(current + direction * caps.actuationStep)
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
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: win.tab = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: win.tab = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: if (win.caps.hasScreen) win.tab = 2 }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; onActivated: if (backend.devices.length > 1) backend.current = (backend.current + 1) % backend.devices.length }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }

    // A keyboard without a screen has no Screen tab.
    onCapsChanged: if (tab === 2 && !caps.hasScreen) tab = 0

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 32
            spacing: 18
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
                        implicitWidth: deviceLabel.implicitWidth
                        implicitHeight: deviceLabel.implicitHeight + 8
                        Label {
                            id: deviceLabel
                            text: modelData.model
                            font.pixelSize: 15
                            font.weight: active ? Font.DemiBold : Font.Normal
                            color: active ? "white" : "#6e6e76"
                        }
                        Rectangle {
                            visible: active
                            width: deviceLabel.width; height: 2
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
                ledColors: backend.ledColors
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            RowLayout {
                spacing: 16
                Label {
                    text: sel.count > 0 ? qsTr("%n keys selected", "", sel.count)
                                        : qsTr("Click keys, or drag across them, to change them")
                    color: "#6e6e76"
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [
                        { label: qsTr("All"), action: () => backend.selectAll() },
                        { label: qsTr("None"), action: () => backend.selectNone() },
                    ]
                    delegate: Label {
                        required property var modelData
                        text: modelData.label
                        color: link.containsMouse ? "white" : "#8a8a93"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        MouseArea {
                            id: link
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: parent.modelData.action()
                        }
                    }
                }
            }

            Rectangle {
                color: "#141417"
                radius: theme.radius
                border.color: "#202024"
                Layout.fillWidth: true
                implicitHeight: panelColumn.implicitHeight + 36

                ColumnLayout {
                    id: panelColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    // What the panel edits.
                    RowLayout {
                        spacing: 22
                        Repeater {
                            model: [
                                { label: qsTr("Keys"), shown: true },
                                { label: qsTr("Lighting"), shown: true },
                                { label: qsTr("Screen"), shown: win.caps.hasScreen === true },
                            ]
                            delegate: Item {
                                required property var modelData
                                required property int index
                                visible: modelData.shown
                                implicitWidth: tabLabel.implicitWidth
                                implicitHeight: tabLabel.implicitHeight + 6
                                Label {
                                    id: tabLabel
                                    text: modelData.label
                                    font.pixelSize: 13
                                    font.weight: win.tab === index ? Font.DemiBold : Font.Normal
                                    color: win.tab === index ? "white" : "#6e6e76"
                                }
                                Rectangle {
                                    visible: win.tab === index
                                    width: tabLabel.width; height: 2
                                    anchors.bottom: parent.bottom
                                    color: theme.accent
                                }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tab = index }
                            }
                        }
                    }

                    StackLayout {
                        id: stack
                        currentIndex: win.tab
                        Layout.fillWidth: true
                        Layout.preferredHeight: [keysPanel, lightingPanel, screenPanel][win.tab].implicitHeight
                        KeysPanel { id: keysPanel }
                        LightingPanel { id: lightingPanel }
                        ScreenPanel { id: screenPanel }
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
