import QtQuick
import Atc

// Dashboard tile. info: {title, level, state, line1, line2, effect, action, actionEnabled}.
// activated(): its button.
Rectangle {
    id: tile
    property var info: ({})
    readonly property int level: info.level || 0
    signal activated()
    radius: Theme.radius
    color: Theme.raised
    border { color: level > 2 ? Theme.fault : Theme.border; width: Theme.line }
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2, %3").arg(info.title || "").arg(info.state || "").arg(info.effect || "")

    Label { x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: Theme.px(20)
            text: tile.info.title || ""; color: Theme.textSecondary; font.pixelSize: Theme.small }
    StateIcon { x: Theme.s12; y: Theme.px(36); size: Theme.px(16); level: tile.level }
    Label { x: Theme.px(36); y: Theme.px(34); width: parent.width - x - Theme.s8; height: Theme.px(42)
            text: tile.info.state || ""; wrapMode: Text.WordWrap; maximumLineCount: 2; verticalAlignment: Text.AlignTop
            font { pixelSize: Theme.body; weight: Font.DemiBold } }
    Label { x: Theme.s12; y: Theme.px(80); width: parent.width - 2 * x; height: Theme.px(34)
            text: tile.info.line1 || ""; wrapMode: Text.WordWrap; maximumLineCount: 2; verticalAlignment: Text.AlignTop
            font.pixelSize: Theme.small }
    Label { x: Theme.s12; y: Theme.px(116); width: parent.width - 2 * x; height: Theme.px(32)
            text: tile.info.line2 || ""; wrapMode: Text.WordWrap; maximumLineCount: 2; verticalAlignment: Text.AlignTop
            color: Theme.textSecondary; font.pixelSize: Theme.caption }
    Label { x: Theme.s12; y: Theme.px(152); width: parent.width - 2 * x; height: Theme.px(20)
            text: tile.info.effect || ""; color: Theme.levelColor(tile.level)
            font { pixelSize: Theme.caption; weight: Font.DemiBold } }
    SecondaryButton {
        x: Theme.s12; anchors { bottom: parent.bottom; bottomMargin: Theme.s12 }
        width: parent.width - 2 * x
        visible: (tile.info.action || "") !== ""
        enabled: tile.info.actionEnabled === true
        text: tile.info.action || ""
        font.pixelSize: Theme.small
        focusPolicy: Qt.NoFocus
        onClicked: tile.activated()
    }
}
