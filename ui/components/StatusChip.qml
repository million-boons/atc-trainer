import QtQuick
import Atc

// name + value + status icon.
Rectangle {
    property string name
    property string value
    property int level: 0
    implicitWidth: row.implicitWidth + 2 * Theme.s8
    implicitHeight: Theme.px(28)
    radius: Theme.radiusChip
    color: Theme.raised
    border { color: Theme.border; width: Theme.line }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.px(5)
        StateIcon { level: parent.parent.level; anchors.verticalCenter: parent.verticalCenter }
        Label { text: name; font.pixelSize: Theme.caption; anchors.verticalCenter: parent.verticalCenter }
        Label { text: value; font { pixelSize: Theme.caption; weight: Font.DemiBold }
                anchors.verticalCenter: parent.verticalCenter }
    }
}
