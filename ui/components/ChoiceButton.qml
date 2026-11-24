import QtQuick
import Atc

// Small choice, e.g. an ID or yes/no. selected: icon and border, not colour alone.
BaseButton {
    id: c
    property bool selected: false
    tint: selected ? Theme.accent : Theme.border
    leftPadding: selected ? Theme.px(22) : 0
    implicitWidth: Math.max(Theme.px(72), contentItem.implicitWidth + Theme.px(48))
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.RadioButton
    Accessible.checked: selected
    StateIcon { x: Theme.s12; anchors.verticalCenter: parent.verticalCenter; level: c.selected ? LabController.Ok : 0 }
}
