import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    color: Qt.alpha("#0e0e10", 0.92)

    readonly property var shortcuts: [
        [qsTr("Click"), qsTr("Select a key")],
        [qsTr("Shift+Click"), qsTr("Add or remove a key")],
        [qsTr("Drag"), qsTr("Select every key in the box")],
        ["Ctrl+A", qsTr("Select all keys")],
        ["Esc", qsTr("Clear the selection")],
        ["↑ / ↓", qsTr("Actuation deeper or shallower")],
        [qsTr("Space"), qsTr("Rapid Trigger on or off")],
        ["[ / ]", qsTr("Rapid Trigger sensitivity")],
        ["Tab", qsTr("Next keyboard")],
        ["Ctrl+Z", qsTr("Undo")],
        ["Ctrl+Shift+Z", qsTr("Redo")],
        ["?", qsTr("Show or hide shortcuts")],
        ["Q", qsTr("Quit")],
    ]

    MouseArea { anchors.fill: parent; onClicked: overlay.visible = false }

    GridLayout {
        anchors.centerIn: parent
        columns: 2
        columnSpacing: 32
        rowSpacing: 10
        Repeater {
            model: overlay.shortcuts
            delegate: Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 0
                Layout.alignment: Qt.AlignRight
                text: modelData[0]
                color: theme.accent
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }
        Repeater {
            model: overlay.shortcuts
            delegate: Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 1
                text: modelData[1]
                color: "#d8d8de"
                font.pixelSize: 14
            }
        }
    }
}
