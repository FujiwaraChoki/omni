import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Actuation and Rapid Trigger for the selected keys.
ColumnLayout {
    id: panel
    spacing: 6
    readonly property var sel: backend.selected
    readonly property var caps: backend.capabilities
    readonly property bool analogSelected: sel.analogCount > 0

    SettingRow {
        label: qsTr("Actuation")
        enabled: panel.analogSelected
        value: !panel.analogSelected ? "–" : panel.sel.actuationMixed ? qsTr("Mixed") : win.mm(panel.sel.actuation)
        Slider {
            from: panel.caps.actuationMin ?? 0.1
            to: panel.caps.actuationMax ?? 4.0
            stepSize: panel.caps.actuationStep ?? 0.1
            value: panel.sel.actuation ?? from
            Layout.fillWidth: true
            onMoved: backend.setActuation(value)
        }
    }

    SettingRow {
        label: qsTr("Rapid Trigger")
        enabled: panel.analogSelected
        value: !panel.analogSelected || !panel.sel.rapidTrigger ? "–" : panel.sel.pressMixed ? qsTr("Mixed") : win.mm(panel.sel.press)
        Switch {
            checked: panel.sel.rapidTrigger === true && !panel.sel.rapidTriggerMixed
            onToggled: backend.setRapidTrigger(checked)
        }
        Slider {
            enabled: panel.sel.rapidTrigger === true
            from: panel.caps.sensitivityMin ?? 0.1
            to: panel.caps.sensitivityMax ?? 1.0
            stepSize: panel.caps.sensitivityStep ?? 0.1
            value: panel.sel.press ?? from
            Layout.fillWidth: true
            onMoved: backend.setPress(value)
        }
    }

    SettingRow {
        visible: panel.caps.separateRelease === true
        label: qsTr("Release")
        enabled: panel.analogSelected && panel.sel.rapidTrigger === true
        value: !enabled ? "–" : panel.sel.releaseMixed ? qsTr("Mixed") : win.mm(panel.sel.release)
        Slider {
            from: panel.caps.sensitivityMin ?? 0.1
            to: panel.caps.sensitivityMax ?? 1.0
            stepSize: panel.caps.sensitivityStep ?? 0.1
            value: panel.sel.release ?? from
            Layout.fillWidth: true
            onMoved: backend.setRelease(value)
        }
    }
}
