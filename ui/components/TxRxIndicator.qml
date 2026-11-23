import QtQuick
import Atc

// Large radio state block. tone: LabController.Tone. accent: border colour; fill: active fill.
// The word is never in a status colour: fault shows an icon and a coloured border (04-ui section 4).
Rectangle {
    id: ind
    property int tone: LabController.Neutral
    property string label
    property string value
    property string sub
    property int subLevel: 0     // LabController.Level: icon and colour of the sub-text
    property color accent: Theme.tx
    property color fill: Theme.txFill
    readonly property bool active: tone === LabController.Active
    readonly property bool bad: tone === LabController.Bad
    readonly property color ink: active ? Theme.onFill : Theme.textPrimary
    radius: Theme.radius
    color: active ? fill : Theme.surface
    border { width: (bad ? 3 : 2) * Theme.line
             color: active || tone === LabController.Busy ? accent : bad ? Theme.fault : Theme.border }
    Behavior on color { ColorAnimation { duration: Theme.motion } }

    StateIcon {
        id: icon
        level: ind.bad ? LabController.Fault : LabController.None
        size: Theme.px(24)
        x: Theme.s12
        anchors.verticalCenter: word.verticalCenter
    }
    Label {
        id: word
        x: ind.bad ? icon.x + icon.width + Theme.s8 : Theme.px(14)
        y: (parent.height - height - subText.height) / 2
        text: ind.label
        color: ind.ink
        font { pixelSize: ind.bad ? Theme.px(26) : Theme.display; weight: Font.Bold }
    }
    Label {
        anchors { right: parent.right; rightMargin: Theme.px(14); verticalCenter: word.verticalCenter }
        mono: true
        text: ind.value
        color: ind.ink
        font { pixelSize: ind.bad ? Theme.px(20) : Theme.h1; weight: Font.Bold }
    }
    StateIcon {
        id: subIcon
        level: ind.active ? 0 : ind.subLevel
        x: Theme.px(14)
        anchors.verticalCenter: subText.verticalCenter
    }
    Label {
        id: subText
        x: subIcon.visible ? subIcon.x + subIcon.width + Theme.px(6) : Theme.px(14)
        width: parent.width - x - Theme.px(8)
        anchors.top: word.bottom
        text: ind.sub
        color: ind.active ? Theme.onFill : ind.subLevel > 0 ? Theme.levelColor(ind.subLevel) : Theme.textSecondary
        font.pixelSize: Theme.small
    }
}
