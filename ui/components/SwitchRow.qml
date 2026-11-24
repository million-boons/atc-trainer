import QtQuick
import QtQuick.Templates as T
import Atc

// Setting that is on or off, as a word. toggled(bool on): the wish; "on" follows the controller.
Item {
    id: r
    property string title
    property string sub
    property bool on: false
    signal toggled(bool on)
    implicitHeight: Theme.px(76)
    Label { y: Theme.px(8); width: parent.width - b.width - Theme.s12; height: Theme.px(24); text: r.title
            color: r.enabled ? Theme.textPrimary : Theme.textSecondary; font.weight: Font.DemiBold }
    Label { y: Theme.px(32); width: parent.width - b.width - Theme.s12; height: Theme.px(38); text: r.sub; wrapMode: Text.WordWrap
            maximumLineCount: 2; verticalAlignment: Text.AlignTop; color: Theme.textSecondary; font.pixelSize: Theme.caption }
    ChoiceButton {
        id: b
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: Theme.px(92)
        selected: r.on
        text: r.on ? qsTr("On") : qsTr("Off")
        onClicked: r.toggled(!r.on)
        Accessible.role: Accessible.CheckBox
        Accessible.name: r.title
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
}
