import QtQuick
import Atc

// Modal panel with free content. level: LabController.Level; Fault gives a thick fault border and an icon.
Item {
    id: dlg
    property string title
    property int level: 0
    default property alias content: inner.data
    readonly property bool bad: level === LabController.Fault
    Rectangle { anchors.fill: parent; color: Theme.bg; opacity: 0.8 }
    MouseArea { anchors.fill: parent }
    Rectangle {
        anchors.centerIn: parent
        width: Theme.px(560); height: col.height + 2 * Theme.s24
        radius: Theme.radius
        color: Theme.surface
        border { color: dlg.bad ? Theme.fault : Theme.border; width: (dlg.bad ? 3 : 1) * Theme.line }
        Column {
            id: col
            x: Theme.s24; y: Theme.s24; width: parent.width - 2 * x
            spacing: Theme.s12
            Row {
                spacing: Theme.s8
                StateIcon { level: dlg.bad ? dlg.level : 0; size: Theme.px(24); anchors.verticalCenter: parent.verticalCenter }
                Label { text: dlg.title; font { pixelSize: Theme.h1; weight: Font.DemiBold } }
            }
            Column { id: inner; width: parent.width; spacing: Theme.s12 }
        }
    }
}
