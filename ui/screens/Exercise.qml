import QtQuick
import Atc

// Active communication (04 section 5c, D-113). View only: what Assessed hides is hidden in ExerciseController.
Item {
    id: s
    property LabController lab
    property ExerciseController ex
    property bool askStop: false
    property bool canStart: true   // still ready: something may have changed while the Start dialog is open
    readonly property int gap: Theme.s12
    readonly property int leftW: Theme.px(268)
    readonly property int rightW: Theme.px(292)
    readonly property int topH: height - bar.height - 3 * gap
    readonly property bool complete: ex.phase === ExerciseController.Complete

    // ---- aircraft
    Card {
        id: left
        x: s.gap; y: s.gap; width: s.leftW; height: s.topH
        title: qsTr("AIRCRAFT")
        Label { x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: Theme.px(40); mono: true
                text: s.ex.context.callsign || "-"; font { pixelSize: Theme.display; weight: Font.Bold } }
        Grid {
            x: Theme.s12; y: Theme.px(56)
            columns: 2
            rowSpacing: Theme.s8; columnSpacing: Theme.s8
            Repeater {
                model: [[qsTr("Runway"), s.ex.context.runway, true], [qsTr("Taxiway"), s.ex.context.taxiway, true],
                        [qsTr("Type"), s.ex.context.type, false], [qsTr("Half-duplex"), s.ex.context.rule, false]]
                Column {
                    width: (s.leftW - 2 * Theme.s12 - Theme.s8) / 2
                    Label { text: modelData[0]; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    Label { width: parent.width; text: modelData[1] || "-"; mono: true
                            font { pixelSize: modelData[2] ? Theme.h1 : Theme.h2; weight: Font.Bold } }
                }
            }
        }
        SectionHeader { id: clr; y: Theme.px(166); width: parent.width; text: qsTr("CLEARANCES ISSUED") }
        Label { x: Theme.s12; y: clr.y + clr.height + Theme.s4; width: parent.width - 2 * x; height: Theme.px(24)
                visible: text !== ""; text: s.ex.clearancesNote; color: Theme.textSecondary; font.pixelSize: Theme.small }
        ListView {
            x: Theme.s12; y: clr.y + clr.height + Theme.s4; width: parent.width - 2 * x; height: audio.y - y
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.ex.clearances
            delegate: Row {
                height: Theme.px(24)
                spacing: Theme.s8
                StateIcon { level: LabController.Ok; anchors.verticalCenter: parent.verticalCenter }
                Label { text: modelData; font.pixelSize: Theme.small; height: parent.height; width: s.leftW - 2 * Theme.s12 - Theme.px(22) }
            }
        }
        SectionHeader { id: audio; y: parent.height - Theme.px(168); width: parent.width; text: qsTr("EXERCISE AUDIO") }
        StateIcon { x: Theme.s12; y: audio.y + audio.height + Theme.px(14); level: s.ex.audioSaved ? LabController.Ok : LabController.Caution }
        Label { x: Theme.px(32); y: audio.y + audio.height; width: parent.width - x - Theme.s8; height: Theme.px(40)
                text: s.ex.audioText; wrapMode: Text.WordWrap; maximumLineCount: 2; font.pixelSize: Theme.small }
        SectionHeader { id: model; y: parent.height - Theme.px(92); width: parent.width; text: qsTr("SPEECH MODEL") }
        Label { x: Theme.s12; y: model.y + model.height + Theme.s4; width: parent.width - 2 * x; height: Theme.px(22)
                text: s.ex.context.model || "-"; font.pixelSize: Theme.small }
        Label { x: Theme.s12; y: model.y + model.height + Theme.px(26); width: parent.width - 2 * x; height: Theme.px(20)
                text: qsTr("Locked for this exercise"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
    }

    // ---- transcript
    Card {
        x: left.x + left.width + s.gap; y: s.gap
        width: s.width - s.leftW - s.rightW - 4 * s.gap; height: s.topH
        title: qsTr("TRANSCRIPT")
        headerItems: [
            Row {
                spacing: Theme.s8
                anchors.verticalCenter: parent.verticalCenter
                Tag { visible: s.ex.placeholder; text: qsTr("PLACEHOLDER") }
                ParamChip { value: s.ex.modeText }
            },
            Label { anchors.right: parent.right; height: parent.height; text: qsTr("text appears after release")
                    color: Theme.textSecondary; font.pixelSize: Theme.caption }
        ]
        ListView {
            id: list
            property bool follow: true      // false after the user scrolled up
            anchors { fill: parent; margins: Theme.s12 }
            clip: true
            spacing: Theme.s8
            verticalLayoutDirection: ListView.BottomToTop   // row 0 is the newest
            boundsBehavior: Flickable.StopAtBounds
            model: s.ex.transcript
            onMovementEnded: follow = atYEnd
            onCountChanged: { if (count === 0) follow = true; if (follow) positionViewAtBeginning() }
            delegate: TranscriptEntry {
                required property var model
                width: ListView.view.width; height: implicitHeight
                kind: model.kind; time: model.time; text: model.text; intent: model.intent
                chips: model.chips; cls: model.cls; level: model.level; delay: model.delay
            }
        }
        SecondaryButton {
            anchors { right: parent.right; bottom: parent.bottom; margins: Theme.s12 }
            visible: !list.follow
            text: qsTr("Latest")
            focusPolicy: Qt.NoFocus
            onClicked: { list.follow = true; list.positionViewAtBeginning() }
        }
    }

    // ---- procedure
    Card {
        x: s.width - s.rightW - s.gap; y: s.gap; width: s.rightW; height: s.topH
        title: qsTr("PROCEDURE")
        headerItems: Label { anchors.right: parent.right; height: parent.height; text: s.ex.progressText
                             color: Theme.textSecondary; font.pixelSize: Theme.caption }
        ListView {
            id: steps
            width: parent.width
            height: Math.min(contentHeight, stats.y)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.ex.steps
            delegate: StepRow { width: ListView.view.width; step: modelData }
        }
        Column {
            id: stats
            x: Theme.s12; width: parent.width - 2 * x
            y: stop.y - height - Theme.s12
            Repeater {
                model: [[qsTr("Missed steps"), s.ex.missedText, false], [qsTr("Waiting for ATC"), s.ex.waitingText, true]]
                Item {
                    width: stats.width; height: Theme.px(27)
                    Label { height: parent.height; text: modelData[0]; color: Theme.textSecondary; font.pixelSize: Theme.small }
                    Label { anchors.right: parent.right; height: parent.height; text: modelData[1]; mono: modelData[2]
                            font { pixelSize: Theme.small; weight: Font.DemiBold } }
                }
            }
        }
        Item {
            id: stop
            x: Theme.s12; width: parent.width - 2 * x; height: Theme.touch
            y: parent.height - height - Theme.s12
            PrimaryButton { anchors.fill: parent; visible: s.complete; text: qsTr("Show result"); focusPolicy: Qt.NoFocus
                            onClicked: s.ex.showResult() }
            DangerButton { anchors.fill: parent; visible: !s.complete; text: qsTr("Stop exercise"); focusPolicy: Qt.NoFocus
                           enabled: s.ex.pttLive; onClicked: s.askStop = true }
        }
    }

    RadioBar {
        id: bar
        x: s.gap; y: s.height - height - s.gap; width: s.width - 2 * s.gap
        lab: s.lab
        // The exercise's own notice comes first; PTT and microphone notices otherwise.
        noticeLevel: s.ex.notice ? s.ex.noticeLevel : s.lab.noticeLevel
        notice: s.ex.notice || s.lab.notice
    }

    // ---- dialogs
    // The instructor's choice leaves the radio bar free: a transmission may end the waiting (D-125).
    Loader {
        x: 0; y: 0; width: s.width; height: bar.y
        active: s.ex.choice && !s.askStop
        sourceComponent: ModalPanel {
            title: qsTr("Instructor: no ATC call")
            Label { width: parent.width; wrapMode: Text.WordWrap; text: s.ex.choiceText }
            Label { width: parent.width; wrapMode: Text.WordWrap; color: Theme.textSecondary
                    text: qsTr("Continue: the pilot calls again, same step. A transmission also continues the exercise.") }
            Row {
                spacing: Theme.s12
                PrimaryButton { text: qsTr("Continue"); focusPolicy: Qt.NoFocus; onClicked: s.ex.continueExercise() }
                DangerButton { text: qsTr("End exercise"); focusPolicy: Qt.NoFocus; onClicked: s.askStop = true }
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.askStop || s.ex.phase === ExerciseController.Starting || s.ex.phase === ExerciseController.Paused
        sourceComponent: s.askStop ? stopDialog : s.ex.phase === ExerciseController.Starting ? startDialog : lostDialog
    }
    Component {
        id: stopDialog
        ConfirmDialog {
            title: qsTr("Stop the exercise?")
            text: qsTr("The exercise ends now and cannot be continued.")
            confirmText: qsTr("Stop exercise")
            onConfirmed: { s.askStop = false; s.ex.stop() }
            onClosed: s.askStop = false
        }
    }
    Component {
        id: startDialog
        ModalPanel {
            id: sd
            property bool assessed: false
            title: qsTr("Start %1").arg(s.ex.context.scenario)
            Label { text: qsTr("Mode (cannot change during the exercise)"); color: Theme.textSecondary }
            OptionButton { width: parent.width; selected: !sd.assessed
                           title: selected ? qsTr("Practice, selected") : qsTr("Practice")
                           sub: qsTr("The verdict is shown during the exercise."); onClicked: sd.assessed = false }
            OptionButton { width: parent.width; enabled: s.ex.assessedAvailable; selected: sd.assessed
                           title: !enabled ? qsTr("Assessed, not available") : selected ? qsTr("Assessed, selected") : qsTr("Assessed")
                           sub: s.ex.assessedNote; onClicked: sd.assessed = true }
            Row {
                spacing: Theme.s8
                StateIcon { level: s.ex.audioSaved ? LabController.Ok : LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
                Label { text: s.ex.audioSaved ? qsTr("Consent on record: exercise audio is saved on this machine only") : s.ex.audioText }
            }
            Label { text: qsTr("Speech model: %1, locked at Start").arg(s.ex.context.model); color: Theme.textSecondary }
            Row {
                visible: !s.canStart || s.ex.notice !== ""
                spacing: Theme.s8
                StateIcon { level: LabController.Fault; anchors.verticalCenter: parent.verticalCenter }
                Label { width: Theme.px(480); wrapMode: Text.WordWrap
                        text: s.ex.notice || qsTr("Cannot start now. The footer says why.") }
            }
            Row {
                spacing: Theme.s12
                PrimaryButton { text: qsTr("Start exercise"); focusPolicy: Qt.NoFocus; enabled: s.canStart; onClicked: s.ex.start(sd.assessed) }
                SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: s.ex.cancelStart() }
            }
        }
    }
    Component {
        id: lostDialog
        ModalPanel {
            title: qsTr("PTT disconnected")
            level: LabController.Fault
            Label { width: parent.width; wrapMode: Text.WordWrap
                    text: qsTr("The exercise is paused and its timers are stopped. Reconnect the PTT device and make sure it is released.") }
            Label { text: qsTr("Continues when the PTT is seen released."); color: Theme.textSecondary }
            Row {
                spacing: Theme.s12
                SecondaryButton { text: qsTr("Use Screen PTT"); focusPolicy: Qt.NoFocus; onClicked: s.ex.useScreenPtt() }
                DangerButton { text: qsTr("Stop exercise"); focusPolicy: Qt.NoFocus; onClicked: s.askStop = true }
            }
        }
    }
}
