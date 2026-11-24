import QtQuick
import Atc

// Parsed intent or value. uncertain: shown as heard, with a dashed look: caution border and the "?" in the text.
Rectangle {
    property string value
    property bool uncertain: false
    implicitWidth: t.implicitWidth + 2 * Theme.px(6)
    implicitHeight: Theme.px(22)
    radius: Theme.radiusChip
    color: Theme.surface
    border { color: uncertain ? Theme.caution : Theme.border; width: Theme.line }
    Label { id: t; anchors.centerIn: parent; mono: true; text: parent.value
            color: parent.uncertain ? Theme.caution : Theme.textPrimary; font.pixelSize: Theme.caption }
}
