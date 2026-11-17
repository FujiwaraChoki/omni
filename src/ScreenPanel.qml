import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A picture or GIF on the keyboard's screen, with a pixel-exact preview.
RowLayout {
    id: panel
    spacing: 28
    readonly property var s: backend.screen

    // The screen, three times its size, every pixel square.
    Rectangle {
        Layout.alignment: Qt.AlignTop
        implicitWidth: (panel.s.width ?? 128) * 3 + 16
        implicitHeight: (panel.s.height ?? 40) * 3 + 16
        radius: Math.min(6, theme.radius)
        color: "#050506"
        border.color: "#26262b"
        Image {
            anchors.centerIn: parent
            visible: panel.s.active === true
            width: (panel.s.width ?? 128) * 3
            height: (panel.s.height ?? 40) * 3
            smooth: false
            cache: false
            source: panel.s.active ? "image://omni/screen/" + backend.screenFrame + "/" + panel.s.name + panel.s.fit + panel.s.dither + panel.s.invert : ""
        }
        Label {
            anchors.centerIn: parent
            visible: panel.s.active !== true
            text: qsTr("The keyboard's own screen")
            color: "#55555c"
            font.pixelSize: 12
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        RowLayout {
            spacing: 10
            Chip {
                text: panel.s.active ? qsTr("Change picture…") : qsTr("Choose picture…")
                checked: true
                onClicked: backend.chooseScreenPicture()
            }
            Chip {
                visible: panel.s.active === true
                text: qsTr("Give it back")
                onClicked: backend.clearScreen()
            }
            Label {
                visible: panel.s.active === true
                text: panel.s.frames > 1 ? qsTr("%1 · %n frames", "", panel.s.frames).arg(panel.s.name) : (panel.s.name ?? "")
                color: "#6e6e76"
                font.pixelSize: 12
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }
        RowLayout {
            visible: panel.s.active === true
            spacing: 6
            Chip { text: qsTr("Fit"); checked: panel.s.fit === "fit"; onClicked: backend.setScreenFit("fit") }
            Chip { text: qsTr("Fill"); checked: panel.s.fit === "fill"; onClicked: backend.setScreenFit("fill") }
            Item { width: 12 }
            Chip { text: qsTr("Dither"); checked: panel.s.dither === true; onClicked: backend.setScreenDither(!panel.s.dither) }
            Chip { text: qsTr("Invert"); checked: panel.s.invert === true; onClicked: backend.setScreenInvert(!panel.s.invert) }
        }
        Label {
            text: qsTr("GIFs play on a loop. Dither suits photos; turn it off for line art and text.")
            color: "#55555c"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
