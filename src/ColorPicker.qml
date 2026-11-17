import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Preset swatches and a hex field in a small popup.
Popup {
    id: picker
    property bool removable: false
    signal picked(string color)
    signal removed()
    padding: 14
    modal: false
    focus: true
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181b"; radius: theme.radius; border.color: "#2a2a30" }

    readonly property var presets: ["#ffffff", "#ff3b30", "#ff9f0a", "#ffd60a", "#30d158", "#64d2ff",
                                    "#0a84ff", "#5e5ce6", "#bf5af2", "#ff375f", "#ac8e68", "#8e8e93"]

    ColumnLayout {
        spacing: 12
        GridLayout {
            columns: 6
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: picker.presets
                delegate: Rectangle {
                    required property string modelData
                    width: 22; height: 22; radius: 11
                    color: modelData
                    border.color: Qt.darker(modelData, 1.4)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { picker.picked(parent.modelData); picker.close() } }
                }
            }
        }
        RowLayout {
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 96
                implicitHeight: 28
                radius: Math.min(4, theme.radius)
                color: "#0e0e10"
                border.color: hex.activeFocus ? theme.accent : "#2a2a30"
                TextInput {
                    id: hex
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: "white"
                    font.pixelSize: 13
                    selectByMouse: true
                    validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                    onAccepted: if (text.replace("#", "").length === 6) { picker.picked(text.startsWith("#") ? text : "#" + text); text = ""; picker.close() }
                    Text {
                        visible: !hex.text && !hex.activeFocus
                        text: "#RRGGBB"
                        color: "#55555c"
                        font: hex.font
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: picker.removable
                text: qsTr("Remove")
                color: removeArea.containsMouse ? "white" : "#8a8a93"
                font.pixelSize: 12
                MouseArea { id: removeArea; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { picker.removed(); picker.close() } }
            }
        }
    }
}
