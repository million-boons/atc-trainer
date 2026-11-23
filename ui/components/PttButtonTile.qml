import QtQuick
import Atc

// Live state of one pedal button.
Rectangle {
    property string name
    property bool down: false
    radius: Theme.radius
    color: Theme.raised
    border { color: down ? Theme.tx : Theme.border; width: (down ? 2 : 1) * Theme.line }
    Label { x: Theme.s8; y: Theme.s8; width: parent.width - 2 * x; text: name; color: Theme.textSecondary
            font.pixelSize: Theme.caption }
    Label { x: Theme.s8; y: Theme.px(28); text: down ? qsTr("DOWN") : qsTr("UP")
            font { pixelSize: Theme.h2; weight: Font.Bold } }
}
