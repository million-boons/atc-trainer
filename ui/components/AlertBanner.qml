import QtQuick
import Atc

// Bottom alert lane. level: LabController.Level. tag: optional, at the right end.
Rectangle {
    property int level: 0
    property string text
    property string tag
    color: Theme.surface
    Rectangle { width: parent.width; height: Theme.line; color: Theme.border }
    Tag { id: t; visible: parent.tag !== ""; text: parent.tag; anchors { right: parent.right; rightMargin: Theme.s12; verticalCenter: parent.verticalCenter } }
    Row {
        id: row
        x: Theme.s12; width: parent.width - 2 * x - (t.visible ? t.width + Theme.s12 : 0)
        height: parent.height
        spacing: Theme.s8
        StateIcon { level: parent.parent.level; size: Theme.px(14); anchors.verticalCenter: parent.verticalCenter }
        Label { text: parent.parent.text; font.pixelSize: Theme.small; height: parent.height
                width: row.width - Theme.px(14) - Theme.s8; elide: Text.ElideRight }
    }
}
