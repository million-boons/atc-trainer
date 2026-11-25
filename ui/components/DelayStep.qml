import QtQuick
import Atc

// A delay with - and +. stepped(-1 or 1): the wish; value follows the controller.
Row {
    id: r
    property string value
    property string name
    signal stepped(int n)
    spacing: Theme.s4
    component B: BaseButton { width: Theme.touch; focusPolicy: Qt.NoFocus }
    B { text: "−"; Accessible.name: qsTr("%1 shorter").arg(r.name); onClicked: r.stepped(-1) }
    Label { width: Theme.px(52); height: Theme.touch; text: r.value; mono: true; font.pixelSize: Theme.small; horizontalAlignment: Text.AlignHCenter }
    B { text: "+"; Accessible.name: qsTr("%1 longer").arg(r.name); onClicked: r.stepped(1) }
}
