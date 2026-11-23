import QtQuick
import Atc

// Small outlined warning tag, e.g. PLACEHOLDER. solid: own background and primary text,
// for use on a coloured fill.
Rectangle {
    property alias text: t.text
    property bool solid: false
    implicitWidth: t.implicitWidth + 2 * Theme.px(6)
    implicitHeight: Theme.px(22)
    radius: Theme.radiusChip
    color: solid ? Theme.surface : "transparent"
    border { color: Theme.caution; width: Theme.line }
    Label { id: t; anchors.centerIn: parent; color: parent.solid ? Theme.textPrimary : Theme.caution
            font { pixelSize: Theme.caption; weight: Font.DemiBold } }
}
