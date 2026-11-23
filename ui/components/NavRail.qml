import QtQuick
import QtQuick.Templates as T
import Atc

// entries: list of {id, name, icon, enabled}. current: id. activated(id).
Rectangle {
    id: rail
    property var entries: []
    property string current
    signal activated(string id)
    color: Theme.surface
    Rectangle { anchors.right: parent.right; width: Theme.line; height: parent.height; color: Theme.border }
    Column {
        anchors { fill: parent; margins: Theme.s8 }
        spacing: Theme.s4
        Repeater {
            model: rail.entries
            T.Button {
                id: nb
                readonly property bool on: modelData.id === rail.current
                width: parent.width; height: Theme.px(64)
                enabled: modelData.enabled
                focusPolicy: Qt.TabFocus
                onClicked: rail.activated(modelData.id)
                background: Rectangle {
                    radius: Theme.radius
                    color: nb.on ? Theme.raised : "transparent"
                    border { width: nb.on || nb.visualFocus ? Theme.line : 0
                             color: nb.visualFocus ? Theme.accent : Theme.border }
                    Rectangle { visible: nb.on; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                }
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: Theme.px(3)
                        IconItem {
                            width: Theme.px(20); height: width
                            anchors.horizontalCenter: parent.horizontalCenter
                            path: modelData.icon
                            color: nb.on ? Theme.textPrimary : nb.enabled ? Theme.textSecondary : Theme.textMuted
                        }
                        Label {
                            width: parent.width
                            text: modelData.name
                            wrapMode: nb.enabled ? Text.WordWrap : Text.NoWrap
                            leftPadding: nb.enabled ? Theme.s4 : 0; rightPadding: leftPadding
                            font.pixelSize: Theme.caption
                            horizontalAlignment: Text.AlignHCenter
                            color: nb.on ? Theme.textPrimary : nb.enabled ? Theme.textSecondary : Theme.textMuted
                        }
                        Label {
                            width: parent.width
                            visible: !nb.enabled
                            text: qsTr("not built")
                            font.pixelSize: Theme.caption
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.textMuted
                        }
                    }
                }
            }
        }
    }
}
