import QtQuick
import Atc

// Dashboard tile. info: {title, level, state, line1, line2, effect, action, actionEnabled, action2, action2Enabled}.
// activated(), activated2(): its buttons. A second button shares the row, so the tile never changes size.
Rectangle {
    id: tile
    property var info: ({})
    readonly property int level: info.level || 0
    readonly property bool two: (info.action2 || "") !== ""
    signal activated()
    signal activated2()
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
        width: tile.two ? (parent.width - 2 * x - Theme.s8) / 2 : parent.width - 2 * x
        visible: (tile.info.action || "") !== ""
        enabled: tile.info.actionEnabled === true
        text: tile.info.action || ""
        font.pixelSize: Theme.small
        focusPolicy: Qt.NoFocus
        onClicked: tile.activated()
    }
    SecondaryButton {
        anchors { right: parent.right; rightMargin: Theme.s12; bottom: parent.bottom; bottomMargin: Theme.s12 }
        width: (parent.width - 2 * Theme.s12 - Theme.s8) / 2
        visible: tile.two
        enabled: tile.info.action2Enabled === true
        tint: enabled ? Theme.accent : Theme.border
        text: tile.info.action2 || ""
        font.pixelSize: Theme.small
        focusPolicy: Qt.NoFocus
        onClicked: tile.activated2()
    }
}
