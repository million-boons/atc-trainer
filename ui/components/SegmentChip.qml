import QtQuick
import QtQuick.Templates as T
import Atc

// One segment of a template. Static: S, solid, proportional. Dynamic: D, dashed, mono. Never by colour alone.
T.Button {
    id: c
    property string words
    property bool dynamic: false
    property string position
    property string clipState
    property bool selected: false
    implicitHeight: Theme.px(52)
    implicitWidth: Math.max(Theme.px(64), Math.max(upper.implicitWidth, lower.implicitWidth) + 2 * Theme.s8)
    focusPolicy: Qt.NoFocus
    Accessible.name: qsTr("%1 %2").arg(dynamic ? qsTr("dynamic") : qsTr("static")).arg(words)
    background: Rectangle {
        radius: Theme.radiusChip
        color: c.down ? Theme.border : c.dynamic ? Theme.surface : Theme.raised
        border { width: c.dynamic ? 0 : Theme.line; color: Theme.border }
        DashFrame { anchors.fill: parent; visible: c.dynamic; color: Theme.accent; line: Theme.line; radius: Theme.radiusChip }
        Rectangle { visible: c.selected; anchors.bottom: parent.bottom; width: parent.width; height: 3 * Theme.line; color: Theme.accent }
    }
    contentItem: Item {
        Row {
            id: upper
            x: Theme.s8; y: Theme.s4; height: Theme.px(22)
            spacing: Theme.px(6)
            Label { text: c.dynamic ? "D" : "S"; mono: true; font { pixelSize: Theme.caption; weight: Font.Bold } color: Theme.textSecondary; height: parent.height }
            Label { text: c.words; mono: c.dynamic; font.pixelSize: Theme.small; height: parent.height }
        }
        Row {
            id: lower
            x: Theme.s8; y: Theme.px(28); height: Theme.px(18)
            spacing: Theme.s8
            Label { text: c.position; visible: text !== ""; mono: true; font.pixelSize: Theme.caption; color: Theme.textSecondary; height: parent.height }
            ClipDot { state_: c.clipState; height: parent.height }
            Label { text: qsTr("selected"); visible: c.selected; font.pixelSize: Theme.caption; color: Theme.accent; height: parent.height }
        }
    }
}
