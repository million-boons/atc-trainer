import QtQuick
import Atc

// Tabs of Training Content (D-326). current: phrases or exercises. picked(id): another tab. items: right side.
Rectangle {
    id: t
    property string current
    property bool phrasesOn: true
    property bool exercisesOn: false
    property alias items: right_.data
    signal picked(string id)
    implicitHeight: Theme.px(56)
    color: Theme.surface
    radius: Theme.radius
    border { color: Theme.border; width: Theme.line }
    Row {
        x: Theme.s12; height: parent.height
        spacing: Theme.s8
        Label { height: parent.height; text: qsTr("TRAINING CONTENT"); color: Theme.textSecondary; font { pixelSize: Theme.caption; weight: Font.DemiBold; letterSpacing: Theme.px(1) } }
        Repeater {
            model: [["phrases", qsTr("Phrases"), t.phrasesOn], ["scenarios", qsTr("Scenarios: later"), false], ["exercises", qsTr("Exercises"), t.exercisesOn]]
            ChoiceButton { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: Theme.small; text: modelData[1]; enabled: modelData[2]
                           selected: t.current === modelData[0]; onClicked: if (!selected) t.picked(modelData[0]) }
        }
    }
    Row {
        id: right_
        anchors { right: parent.right; rightMargin: Theme.s8 }
        height: parent.height
        spacing: Theme.s8
    }
}
