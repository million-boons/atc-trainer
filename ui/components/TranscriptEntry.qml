import QtQuick
import Atc

// One transcript entry. Heights are fixed per kind, with room for two lines of text, so that a runway or
// callsign is never cut off and nothing moves when text arrives.
Item {
    id: e
    property int kind: ExerciseController.Atc
    property string time
    property string text
    property string intent
    property var chips: []
    property string cls
    property int level: 0
    property string delay
    readonly property bool atc: kind === ExerciseController.Atc
    readonly property bool pilot: kind === ExerciseController.Pilot
    readonly property bool note: kind === ExerciseController.Note
    implicitHeight: Theme.px(atc ? 94 : pilot ? 66 : 30)

    Rectangle {
        x: e.pilot ? parent.width - width : 0
        width: e.note ? parent.width : Math.round(parent.width * 0.86); height: parent.height
        radius: Theme.radius
        color: e.atc ? Theme.laneAtc : e.pilot ? Theme.lanePilot : "transparent"
        border { width: e.note ? Theme.line : 0; color: Theme.border }
        Rectangle { visible: !e.note; x: e.pilot ? parent.width - width : 0; width: 3 * Theme.line; height: parent.height
                    color: e.pilot ? Theme.rx : Theme.accent }

        // ATC and pilot
        Row {
            visible: !e.note
            x: Theme.s12; y: Theme.s4; height: Theme.px(20)
            spacing: Theme.s8
            Label { mono: true; text: e.time; color: Theme.textSecondary; font.pixelSize: Theme.caption; height: parent.height }
            Label { text: e.pilot ? qsTr("PILOT") : qsTr("ATC"); font { pixelSize: Theme.caption; weight: Font.Bold } height: parent.height }
        }
        Row {
            visible: !e.note
            anchors { right: parent.right; rightMargin: Theme.s12 }
            y: Theme.s4; height: Theme.px(20)
            spacing: Theme.s4
            StateIcon { level: e.atc ? e.level : 0; anchors.verticalCenter: parent.verticalCenter }
            Label { text: e.atc ? e.cls : qsTr("PLACEHOLDER"); color: Theme.textSecondary; font.pixelSize: Theme.caption; height: parent.height }
            Label { visible: e.atc; leftPadding: Theme.s4; mono: true; text: e.delay; color: Theme.textSecondary
                    font.pixelSize: Theme.caption; height: parent.height }
        }
        Label {
            visible: !e.note
            x: Theme.s12; y: Theme.px(24); width: parent.width - 2 * x; height: Theme.px(38)
            mono: true
            text: e.text
            wrapMode: Text.Wrap
            maximumLineCount: 2
            verticalAlignment: Text.AlignTop
            lineHeight: 0.95
            font.pixelSize: Theme.px(14)
        }
        Row {
            visible: e.atc
            x: Theme.s12; y: Theme.px(66)
            spacing: Theme.s4
            ParamChip { visible: value !== ""; value: e.intent }
            Repeater { model: e.chips
                       ParamChip { value: modelData.value; uncertain: modelData.uncertain } }
        }

        // note
        Row {
            visible: e.note
            x: Theme.s12; height: parent.height
            spacing: Theme.s8
            StateIcon { level: e.level; anchors.verticalCenter: parent.verticalCenter }
            Label { text: e.text; font.pixelSize: Theme.small; height: parent.height }
        }
    }
    Accessible.role: Accessible.ListItem
    Accessible.name: e.note ? e.text : qsTr("%1 %2: %3").arg(e.time).arg(e.pilot ? qsTr("Pilot") : qsTr("ATC")).arg(e.text)
}
