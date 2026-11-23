import QtQuick
import Atc

// Status icon. level: LabController.Level. Shape differs per level, not only colour.
IconItem {
    property int level: 0
    property int size: Theme.px(12)
    implicitWidth: size; implicitHeight: size
    box: 12; stroke: 1.6
    path: Theme.levelIcon[level]
    color: Theme.levelColor(level)
    visible: level > 0
}
