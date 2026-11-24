import QtQuick
import QtQuick.Templates as T
import Atc

// entries: list of {id, name, icon, enabled, why}. current: id. activated(id).
// why: for a disabled entry, the reason and when it unlocks. Shown while the pointer is over the entry,
// and for 5 s after a tap, because a touch panel has no pointer.
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
            Item {
              id: slot
              readonly property string why: modelData.enabled ? "" : modelData.why || ""
              width: parent.width; height: Theme.px(64)
              function showTip() { tip.owner = slot; tip.y = slot.mapToItem(rail, 0, 0).y; tip.text = slot.why }
              HoverHandler { enabled: slot.why !== ""
                             onHoveredChanged: if (hovered) { tipEnd.stop(); slot.showTip() }
                                               else if (tip.owner === slot && !tipEnd.running) tip.text = "" }
              TapHandler { enabled: slot.why !== ""; onTapped: { slot.showTip(); tipEnd.restart() } }
              T.Button {
                id: nb
                readonly property bool on: modelData.id === rail.current
                anchors.fill: parent
                enabled: modelData.enabled
                Accessible.name: modelData.name
                Accessible.description: slot.why
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
                            text: qsTr("not yet")
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
    Timer { id: tipEnd; interval: 5000; onTriggered: tip.text = "" }
    onEntriesChanged: tip.text = ""
    Rectangle {
        id: tip
        property Item owner: null
        property string text
        visible: text !== ""
        x: rail.width + Theme.s8
        width: Theme.px(300); height: Math.max(Theme.px(64), tipText.implicitHeight + 2 * Theme.s12)
        radius: Theme.radius
        color: Theme.raised
        border { color: Theme.border; width: Theme.line }
        Label { id: tipText; x: Theme.s12; width: parent.width - 2 * x; height: parent.height
                text: tip.text; wrapMode: Text.WordWrap; font.pixelSize: Theme.small }
    }
}
