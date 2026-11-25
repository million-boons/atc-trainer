import QtQuick
import QtQuick.Templates as T
import Atc

// Takes of the selected clip: state as a word and an icon.
ListView {
    id: list
    property StudioController studio
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    model: studio.takes
    delegate: T.Button {
        id: t
        width: list.width; height: Theme.touch
        focusPolicy: Qt.NoFocus
        onClicked: list.studio.selectTake(index)
        Accessible.name: qsTr("Take %1 %2").arg(modelData.number).arg(modelData.state)
        background: Rectangle {
            color: modelData.selected ? Theme.raised : "transparent"
            Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
        }
        contentItem: Item {
            Label { x: Theme.s8; y: Theme.px(3); width: parent.width - 2 * x; height: Theme.px(20); font.pixelSize: Theme.small
                    text: qsTr("Take %1  %2").arg(modelData.number).arg(modelData.who) }
            Label { anchors { right: parent.right; rightMargin: Theme.s8 } y: Theme.px(3); height: Theme.px(20); mono: true
                    text: modelData.info; font.pixelSize: Theme.caption; color: Theme.textSecondary }
            StateIcon { id: ic; x: Theme.s8; y: Theme.px(28); level: modelData.level }
            Label { objectName: "takeState"; x: ic.visible ? ic.x + ic.width + Theme.s4 : Theme.s8; y: Theme.px(24); width: parent.width - x - Theme.s4; height: Theme.px(20)
                    text: modelData.state; font.pixelSize: Theme.caption; color: Theme.textSecondary }
        }
    }
}
