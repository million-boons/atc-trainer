import QtQuick
import Atc

// Result of one exercise (04 section 5c, D-113): facts only, no pass mark. View only.
Item {
    id: s
    property ExerciseController ex
    readonly property var r: ex.result
    readonly property int gap: Theme.s12
    readonly property int rightW: Theme.px(372)
    readonly property int midW: width - rightW - 3 * gap
    readonly property int midY: head.y + head.height + gap
    readonly property int midH: foot.y - gap - midY

    Card {
        id: head
        x: s.gap; y: s.gap; width: s.width - 2 * s.gap; height: Theme.px(92)
        Row {
            x: Theme.px(20)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(28)
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Row {
                    spacing: Theme.s8
                    StateIcon { level: s.r.level || 0; size: Theme.px(20); anchors.verticalCenter: parent.verticalCenter }
                    Label { text: s.r.title || ""; font { pixelSize: Theme.h1; weight: Font.Bold } }
                }
                Label { text: qsTr("%1   %2").arg(s.ex.modeText).arg(s.r.sub || ""); color: Theme.textSecondary; font.pixelSize: Theme.caption }
            }
            Repeater {
                model: [[s.r.stepsDone, qsTr("steps completed")], [s.r.missed, qsTr("missed responses")], [s.r.duration, qsTr("duration")]]
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Label { mono: true; text: modelData[0] || "-"; font { pixelSize: Theme.h1; weight: Font.Bold } }
                    Label { text: modelData[1]; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                }
            }
        }
        Column {
            anchors { right: parent.right; rightMargin: Theme.px(20); verticalCenter: parent.verticalCenter }
            visible: s.ex.placeholder
            spacing: Theme.s4
            Tag { anchors.right: parent.right; text: qsTr("PLACEHOLDER") }
            Label { text: qsTr("No pass mark: the content is not validated"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
        }
    }

    // ---- steps
    Card {
        id: steps
        x: s.gap; y: s.midY; width: s.midW
        height: Math.round(Theme.px(36 + 30) + Math.min(4, (s.r.steps || []).length) * Theme.px(44) + 2 * Theme.line)
        title: qsTr("STEPS")
        readonly property var cols: [0.15, 0.18, 0.10, 0.22, 0.14, 0.11, 0.10]
        Row {
            x: Theme.s12; width: parent.width - 2 * x; height: Theme.px(30)
            Repeater {
                model: [qsTr("Step"), qsTr("Outcome"), qsTr("Attempts"), qsTr("Trainee errors"), qsTr("Missed responses"), qsTr("Response"), qsTr("Say again")]
                Label { objectName: "stepHead"; width: parent.width * steps.cols[index]; height: parent.height; text: modelData
                        rightPadding: Theme.s8; wrapMode: Text.WordWrap; maximumLineCount: 2; lineHeight: 0.9
                        color: Theme.textSecondary; font.pixelSize: Theme.caption }
            }
        }
        ListView {
            y: Theme.px(30); width: parent.width; height: parent.height - y
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.r.steps || []
            delegate: Item {
                id: line
                readonly property var d: modelData
                width: ListView.view.width; height: Theme.px(44)
                Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
                Row {
                    x: Theme.s12; width: parent.width - 2 * x; height: parent.height
                    Label { width: parent.width * steps.cols[0]; height: parent.height; text: line.d.name
                            font { pixelSize: Theme.small; weight: Font.DemiBold } }
                    Row {
                        width: parent.width * steps.cols[1]; height: parent.height
                        spacing: Theme.s8
                        StateIcon { level: line.d.level; anchors.verticalCenter: parent.verticalCenter }
                        Label { text: line.d.outcome; height: parent.height; font.pixelSize: Theme.small }
                    }
                    Label { width: parent.width * steps.cols[2]; height: parent.height; mono: true; text: line.d.attempts; font.pixelSize: Theme.small }
                    Label { width: parent.width * steps.cols[3]; height: parent.height; text: line.d.errors; font.pixelSize: Theme.small; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                    Label { width: parent.width * steps.cols[4]; height: parent.height; mono: true; text: line.d.missed; font.pixelSize: Theme.small }
                    Label { width: parent.width * steps.cols[5]; height: parent.height; mono: true; text: line.d.response; font.pixelSize: Theme.small }
                    Label { width: parent.width * steps.cols[6]; height: parent.height; mono: true; text: line.d.sayAgain; font.pixelSize: Theme.small }
                }
            }
        }
    }

    component Rows: Column {
        id: rows
        property var model: []
        Repeater {
            model: rows.model
            Item {
                width: rows.width; height: Theme.px(27)
                Label { height: parent.height; width: parent.width - v.width - Theme.s8; text: modelData.name
                        color: Theme.textSecondary; font.pixelSize: Theme.small }
                Label { id: v; anchors.right: parent.right; height: parent.height; text: modelData.value
                        font { pixelSize: Theme.small; weight: Font.DemiBold } }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
            }
        }
    }
    Card {
        id: ptt
        x: s.gap; y: steps.y + steps.height + s.gap
        width: Math.round((s.midW - s.gap) / 2); height: s.midY + s.midH - y
        title: qsTr("PTT DISCIPLINE")
        Rows { x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; model: s.r.ptt || [] }
    }
    Card {
        x: ptt.x + ptt.width + s.gap; y: ptt.y
        width: s.midW - ptt.width - s.gap; height: ptt.height
        title: qsTr("SUSPECTED SPEECH RECOGNITION ERRORS")
        Row {
            x: Theme.s12; y: Theme.s8; height: Theme.px(22)
            spacing: Theme.s8
            StateIcon { level: s.r.suspectedLevel || 0; anchors.verticalCenter: parent.verticalCenter }
            Label { text: s.r.suspectedTitle || ""; height: parent.height; font { pixelSize: Theme.small; weight: Font.DemiBold } }
        }
        Label { x: Theme.s12; y: Theme.px(34); width: parent.width - 2 * x; height: speech.y - y
                text: s.r.suspectedText || ""; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignTop
                color: Theme.textSecondary; font.pixelSize: Theme.small }
        Rows { id: speech; x: Theme.s12; width: parent.width - 2 * x; y: parent.height - height - Theme.s4; model: s.r.speech || [] }
    }

    // ---- timeline
    Card {
        x: s.width - s.rightW - s.gap; y: s.midY; width: s.rightW; height: s.midH
        title: qsTr("TIMELINE")
        headerItems: Row {
            anchors.right: parent.right
            height: parent.height
            spacing: Theme.s4
            StateIcon { level: s.ex.audioSaved ? 0 : LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
            Label { text: s.ex.audioSaved ? qsTr("tap to play") : qsTr("no audio saved"); height: parent.height
                    color: Theme.textSecondary; font.pixelSize: Theme.caption }
        }
        ListView {
            id: tl
            anchors.fill: parent
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.ex.timeline
            delegate: Item {
                id: row
                required property int index
                required property var model
                width: tl.width; height: Theme.px(63)
                Label { x: Theme.s12; width: Theme.px(62); height: parent.height; mono: true; text: row.model.time
                        font.pixelSize: Theme.caption }
                Label { x: Theme.px(78); y: Theme.s4; width: parent.width - x - Theme.px(60); height: Theme.px(34); text: row.model.text
                        wrapMode: Text.Wrap; maximumLineCount: 2; lineHeight: 0.95; font.pixelSize: Theme.small }
                Row {
                    x: Theme.px(78); y: Theme.px(40); height: Theme.px(18)
                    spacing: Theme.s4
                    StateIcon { level: row.model.level; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; text: row.model.outcome ? qsTr("%1   %2").arg(row.model.who).arg(row.model.outcome) : row.model.who
                            color: Theme.textSecondary; font.pixelSize: Theme.caption }
                }
                BaseButton {
                    anchors { right: parent.right; rightMargin: Theme.s4; verticalCenter: parent.verticalCenter }
                    width: Theme.touch
                    enabled: s.ex.audioSaved
                    focusPolicy: Qt.NoFocus
                    onClicked: s.ex.play(row.index)
                    Accessible.name: qsTr("Play %1").arg(row.model.time)
                    IconItem { anchors.centerIn: parent; width: Theme.px(18); height: width; box: 12; stroke: 1.6
                               path: Theme.iconNow; color: parent.enabled ? Theme.accent : Theme.textMuted }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
            }
        }
    }

    // ---- actions
    Item {
        id: foot
        x: s.gap; y: s.height - height - s.gap; width: s.width - 2 * s.gap; height: Theme.px(64)
        Row {
            id: actions
            anchors.verticalCenter: parent.verticalCenter
            spacing: s.gap
            PrimaryButton { text: qsTr("Back to Dashboard"); onClicked: s.ex.leaveResult() }
            SecondaryButton { text: qsTr("Run again"); onClicked: s.ex.runAgain() }
            SecondaryButton { text: qsTr("Model settings"); onClicked: s.ex.screenRequested("lab") }
        }
        Label { anchors.right: parent.right; width: parent.width - actions.width - s.gap; height: parent.height
                horizontalAlignment: Text.AlignRight
                text: s.ex.audioSaved ? qsTr("Log and audio: this machine only") : qsTr("Log: this machine only. %1").arg(s.ex.audioText)
                color: Theme.textSecondary; font.pixelSize: Theme.caption }
    }
}
