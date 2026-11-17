import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// One labelled control row: name on the left, the control, and its value.
RowLayout {
    id: row
    property string label
    property string value
    default property alias content: holder.data
    spacing: 16
    Layout.fillWidth: true

    Label {
        id: name
        text: row.label
        color: row.enabled ? "#c9c9d0" : "#55555c"
        font.pixelSize: 13
        elide: Text.ElideRight
        Layout.preferredWidth: 120
    }
    RowLayout {
        id: holder
        spacing: 12
        Layout.fillWidth: true
    }
    Label {
        text: row.value
        color: row.enabled ? "white" : "#55555c"
        font.pixelSize: 13
        font.features: { "tnum": 1 }
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 72
    }
}
