import QtQuick
import Atc

// One procedure step. step: {name, state, note}. Done, Now, Later and Hidden differ in icon and word.
Rectangle {
    property var step: ({})
    readonly property bool now: step.state === ExerciseController.Now
    height: Theme.px(60)
    color: now ? Theme.raised : "transparent"
    Rectangle { visible: now; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
    IconItem {
        x: Theme.s12; width: Theme.px(16); height: width
        anchors.verticalCenter: parent.verticalCenter
        box: 12; stroke: 1.6
        path: step.state === ExerciseController.Done ? Theme.levelIcon[1] : now ? Theme.iconNow : Theme.iconPending
        color: step.state === ExerciseController.Done ? Theme.ok : now ? Theme.accent : Theme.textMuted
    }
    Label { x: Theme.px(40); y: Theme.px(9); width: parent.width - x - Theme.s8; height: Theme.px(22)
            text: step.name || ""; font.weight: Font.DemiBold }
    Label { x: Theme.px(40); y: Theme.px(31); width: parent.width - x - Theme.s8; height: Theme.px(20)
            text: step.note || ""; color: Theme.textSecondary; font.pixelSize: Theme.caption }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1, %2").arg(step.name || "").arg(step.note || "")
}
