import QtQuick
import QtQuick.Templates as T
import Atc

// One choice of several: title, sub-text, and "selected" as a word, an icon and a border.
T.Button {
    id: o
    property string title
    property string sub
    property bool selected: false
    implicitHeight: Theme.px(64)
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.checked: selected
    contentItem: Item {
        StateIcon { id: ic; x: Theme.s12; y: Theme.px(14)
                    level: !o.enabled ? LabController.Fault : o.selected ? LabController.Ok : 0 }
        Label { x: ic.visible ? ic.x + ic.width + Theme.s8 : Theme.s12; y: Theme.px(8); height: Theme.px(24)
                text: o.title; font.weight: Font.DemiBold; color: o.enabled ? Theme.textPrimary : Theme.textSecondary }
        Label { x: Theme.s12; y: Theme.px(32); width: parent.width - 2 * x; height: Theme.px(24)
                text: o.sub; color: Theme.textSecondary; font.pixelSize: Theme.small }
    }
    background: Rectangle {
        radius: Theme.radius
        color: o.down ? Theme.border : Theme.raised
        border { width: (o.selected ? 2 : 1) * Theme.line; color: o.selected ? Theme.accent : o.enabled ? Theme.border : Theme.divider }
    }
}
