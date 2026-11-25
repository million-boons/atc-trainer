import QtQuick
import Atc

// One number of the settings: value with its range, - and +. stepped(n): the wish; the value follows the controller.
Item {
    id: r
    property var info: ({})           // {title, text, byUser, applies, range}
    property bool may: false
    signal stepped(int n)
    implicitHeight: Theme.px(60)
    Label { y: Theme.px(8); width: parent.width - st.width - Theme.s8; height: Theme.px(22); text: r.info.title || ""; font { pixelSize: Theme.small; weight: Font.DemiBold } }
    Label { y: Theme.px(30); width: parent.width - st.width - Theme.s8; height: Theme.px(20); color: Theme.textSecondary; font.pixelSize: Theme.caption
            text: (r.info.byUser ? qsTr("set by user") : qsTr("starting value")) + "   " + (r.info.range || "") }
    Row {
        id: st
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: Theme.s4
        BaseButton { width: Theme.touch; text: "−"; enabled: r.may; focusPolicy: Qt.NoFocus; Accessible.name: qsTr("%1 less").arg(r.info.title); onClicked: r.stepped(-1) }
        Label { objectName: "setupValue"; width: Theme.px(92); height: Theme.touch; horizontalAlignment: Text.AlignHCenter; mono: true; text: r.info.text || ""; font.pixelSize: Theme.small }
        BaseButton { width: Theme.touch; text: "+"; enabled: r.may; focusPolicy: Qt.NoFocus; Accessible.name: qsTr("%1 more").arg(r.info.title); onClicked: r.stepped(1) }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
}
