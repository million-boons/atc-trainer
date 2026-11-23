import QtQuick
import Atc

// One utterance check: icon + title + detail.
Item {
    property int level: 0
    property string title
    property string detail
    StateIcon { id: ic; level: parent.level; x: Theme.s8; y: Theme.px(10) }
    Label {
        x: level > 0 ? ic.x + ic.width + Theme.s4 : Theme.s8
        y: Theme.px(6)
        width: parent.width - x - Theme.s4
        text: title
        color: Theme.levelColor(level)
        font { pixelSize: Theme.small; weight: Font.DemiBold }
    }
    Label { x: Theme.s8; y: Theme.px(27); width: parent.width - x - Theme.s4; text: detail
            color: Theme.textSecondary; font.pixelSize: Theme.caption }
}
