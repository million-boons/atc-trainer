import QtQuick
import QtQuick.Templates as T
import Atc

// Takes of the selected clip, one line each: number, speaker, state as a word and an icon, length. The level is not on
// the line (it would cut the speaker at 1280x800); a take that is too quiet or clips says so in its warnings.
// Only whole rows are shown (the list is as high as the rows that fit); more takes scroll, with a bar at the right.
Item {
    id: box
    property StudioController studio
    readonly property int rows: Math.max(1, Math.floor(height / Theme.touch))

    ListView {
        id: list
        objectName: "takeList"
        width: parent.width; height: box.rows * Theme.touch
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: box.studio.takes
        delegate: T.Button {
            id: t
            width: list.width; height: Theme.touch
            focusPolicy: Qt.NoFocus
            onClicked: box.studio.selectTake(index)
            Accessible.name: qsTr("Take %1 %2").arg(modelData.number).arg(modelData.state)
            background: Rectangle {
                color: modelData.selected ? Theme.raised : "transparent"
                Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
            }
            contentItem: Item {
                Label { id: info; anchors { right: parent.right; rightMargin: Theme.s12 } height: parent.height; mono: true
                        visible: !modelData.withdrawn  // it is never played: its D-133 words need the room
                        text: modelData.info; font.pixelSize: Theme.caption; color: Theme.textSecondary }
                Item {
                    id: state
                    anchors { right: info.visible ? info.left : parent.right; rightMargin: info.visible ? Theme.s8 : Theme.s12 }
                    width: info.visible ? Theme.px(84) : parent.width - Theme.px(104) - Theme.s12; height: parent.height
                    StateIcon { id: ic; level: modelData.level; anchors.verticalCenter: parent.verticalCenter }
                    Label { objectName: "takeState"; x: ic.width + Theme.s4; width: parent.width - x; height: parent.height
                            wrapMode: Text.WordWrap; maximumLineCount: 2  // one line, except the withdrawn words
                            text: modelData.state; font.pixelSize: Theme.caption; color: Theme.textSecondary }
                }
                Label { x: Theme.s8; width: state.x - x - Theme.s4; height: parent.height; font.pixelSize: Theme.small
                        text: qsTr("Take %1  %2").arg(modelData.number).arg(modelData.who) }
            }
        }
    }
    Rectangle {  // where the visible rows are, when there are more
        visible: list.contentHeight > list.height + 1
        anchors.right: list.right
        width: Theme.px(3); radius: width / 2
        y: list.visibleArea.yPosition * list.height
        height: Math.max(Theme.s8, list.visibleArea.heightRatio * list.height)
        color: Theme.textMuted
    }
}
