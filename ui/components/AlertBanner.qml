import QtQuick
import Atc

// Bottom alert lane. level: LabController.Level.
Rectangle {
    property int level: 0
    property string text
    color: Theme.surface
    Rectangle { width: parent.width; height: Theme.line; color: Theme.border }
    Row {
        id: row
        x: Theme.s12; width: parent.width - 2 * x
        height: parent.height
        spacing: Theme.s8
        StateIcon { level: parent.parent.level; size: Theme.px(14); anchors.verticalCenter: parent.verticalCenter }
        Label { text: parent.parent.text; font.pixelSize: Theme.small; height: parent.height
                width: row.width - Theme.px(14) - Theme.s8; elide: Text.ElideRight }
    }
}
