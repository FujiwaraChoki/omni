import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// GIPHY search: a field, then a strip of looping results. Clicking one puts it
// on the keyboard's screen.
ColumnLayout {
    id: strip
    spacing: 10

    // Text fields here keep their keys: letters, Space and digits are app shortcuts elsewhere.
    component Field: Rectangle {
        property alias text: input.text
        property alias placeholder: hint.text
        property alias input: input
        signal accepted()
        implicitHeight: 30
        radius: Math.min(4, theme.radius)
        color: "#0e0e10"
        border.color: input.activeFocus ? theme.accent : "#2a2a30"
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            color: "white"
            font.pixelSize: 13
            selectByMouse: true
            clip: true
            onAccepted: parent.accepted()
            Keys.onEscapePressed: focus = false
        }
        Label {
            id: hint
            visible: !input.text && !input.activeFocus
            anchors.verticalCenter: parent.verticalCenter
            x: 10
            color: "#55555c"
            font.pixelSize: 13
        }
    }

    // No key yet: say why one is needed and take it.
    RowLayout {
        visible: !gifs.hasKey
        spacing: 10
        Label {
            text: qsTr("Search GIFs with your own free GIPHY key:")
            color: "#8a8a93"
            font.pixelSize: 12
        }
        Label {
            text: qsTr("get one")
            color: getKey.containsMouse ? "white" : theme.accent
            font.pixelSize: 12
            font.underline: true
            MouseArea { id: getKey; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://developers.giphy.com/dashboard/?create=true") }
        }
        Field {
            id: keyField
            Layout.preferredWidth: 300
            placeholder: qsTr("Paste the API key, then Enter")
            onAccepted: if (gifs.setKey(text)) text = ""
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        visible: gifs.hasKey
        spacing: 12
        Field {
            id: query
            Layout.preferredWidth: 320
            placeholder: qsTr("Search GIPHY, then Enter")
            onAccepted: { gifs.search(text); input.focus = false }
        }
        Chip {
            text: qsTr("Wide only")
            checked: gifs.wideOnly
            onClicked: gifs.wideOnly = !gifs.wideOnly
        }
        Label {
            text: gifs.busy ? qsTr("Searching…")
                : gifs.wideOnly && gifs.scanned > 0 ? qsTr("%1 wide GIFs out of %2").arg(gifs.results.length).arg(gifs.scanned)
                : ""
            color: "#6e6e76"
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        // GIPHY's terms ask apps to credit it next to results.
        Label {
            text: "Powered by GIPHY"
            color: "#6e6e76"
            font.pixelSize: 11
            font.weight: Font.Medium
        }
        Label {
            text: qsTr("Forget key")
            color: forget.containsMouse ? "white" : "#55555c"
            font.pixelSize: 11
            MouseArea { id: forget; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: gifs.forgetKey() }
        }
    }

    Label {
        visible: gifs.error !== ""
        text: gifs.error
        color: "#ff6b6b"
        font.pixelSize: 12
    }

    ListView {
        id: results
        visible: gifs.hasKey && count > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: gifs.results
        boundsBehavior: Flickable.StopAtBounds
        // Scroll the strip with the mouse wheel, too.
        WheelHandler { onWheel: event => results.flick(event.angleDelta.y * 6, 0) }
        onAtXEndChanged: if (atXEnd && count > 0) gifs.loadMore()
        delegate: Rectangle {
            required property var modelData
            required property int index
            height: 72
            width: Math.max(48, (modelData.width || 100) * 72 / 100)
            radius: Math.min(4, theme.radius)
            color: "#0e0e10"
            border.width: hover.hovered ? 2 : 0
            border.color: theme.accent
            clip: true
            AnimatedImage {
                anchors.fill: parent
                anchors.margins: parent.border.width
                source: modelData.preview
                fillMode: Image.PreserveAspectCrop
                playing: visible
                asynchronous: true
            }
            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: gifs.use(index) }
            ToolTip.visible: hover.hovered && modelData.title !== ""
            ToolTip.text: modelData.title
            ToolTip.delay: 600
        }
    }
}
