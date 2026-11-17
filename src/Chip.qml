import QtQuick
import QtQuick.Controls.Material

// A small toggle-like choice: one of a row of options.
Rectangle {
    id: chip
    property string text
    property bool checked: false
    signal clicked()
    implicitWidth: label.implicitWidth + 24
    implicitHeight: 28
    radius: Math.min(14, theme.radius > 0 ? 14 : 3)
    color: checked ? Qt.alpha(theme.accent, 0.18) : (area.containsMouse ? "#1d1d21" : "transparent")
    border.width: 1
    border.color: checked ? theme.accent : "#2a2a30"
    opacity: enabled ? 1 : 0.4

    Label {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: chip.checked ? "white" : "#a9a9b2"
        font.pixelSize: 12
        font.weight: chip.checked ? Font.Medium : Font.Normal
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
