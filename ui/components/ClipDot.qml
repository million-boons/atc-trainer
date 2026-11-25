import QtQuick
import Atc

// Recording state of a clip: letter and dot. M missing, A alternate only, P primary.
Row {
    id: d
    property string state_
    spacing: Theme.s4
    visible: state_ !== ""
    Rectangle { width: Theme.px(8); height: width; radius: width / 2; anchors.verticalCenter: parent.verticalCenter
                color: d.state_ === "P" ? Theme.ok : d.state_ === "A" ? Theme.caution : "transparent"
                border { width: Theme.line; color: d.state_ === "M" ? Theme.textMuted : color } }
    Label { text: d.state_; mono: true; font.pixelSize: Theme.caption; anchors.verticalCenter: parent.verticalCenter }
}
