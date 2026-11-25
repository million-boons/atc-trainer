import QtQuick
import Atc

// Dashboard (D-103, D-106). View only.
Item {
    id: s
    property DashboardController dash
    property bool askStop: false
    property bool scenarios: false             // Scenario Selection is there
    property bool results: false               // Assessment is there
    signal scenariosAsked()
    signal resultsAsked()
    property PlannerController planner: null   // null: no plans in this build
    property bool showClips: false
    property string dialog_                    // "", plans, briefing
    signal settingsAsked()
    readonly property int gap: Theme.s12
    readonly property int fullW: width - 2 * gap
    readonly property var scn: dash.scenario

    readonly property bool planned: scn.plan !== undefined && scn.plan !== ""
    Component.onCompleted: if (planner) planner.enter()
    Connections { target: s.planner; function onChosen() { if (s.dialog_ === "plans") s.dialog_ = "" } }

    // ---- readiness
    Card {
        id: ready
        x: s.gap; y: s.gap; width: s.fullW; height: Theme.px(304)
        title: qsTr("SYSTEM READINESS")
        headerItems: [
            Label { anchors { right: blocks.left; rightMargin: Theme.s24 } height: parent.height
                    text: s.dash.readyText; font { pixelSize: Theme.small; weight: Font.DemiBold } },
            Label { id: blocks; anchors.right: parent.right; height: parent.height
                    text: s.dash.blockText; font { pixelSize: Theme.small; weight: Font.DemiBold } }
        ]
        Row {
            x: Theme.s12; y: Theme.s12
            spacing: Theme.s8
            Repeater {
                model: 6
                StatusTile {
                    width: (ready.width - 2 * Theme.s12 - 5 * Theme.s8) / 6
                    height: Theme.px(240)
                    info: s.dash.tiles[index] || ({})
                    onActivated: if (s.dash.tileAction(index)) s.showClips = true
                    onActivated2: s.dash.toneHeard()   // only the Speaker tile has a second button
                }
            }
        }
    }

    // ---- scenario
    Card {
        id: scenario
        x: s.gap; y: ready.y + ready.height + s.gap
        width: Math.round((s.fullW - s.gap) * 0.52)
        height: s.height - y - start.height - 2 * s.gap
        title: qsTr("SCENARIO")
        headerItems: Tag { visible: s.scn.placeholder === true; text: qsTr("PLACEHOLDER")
                           anchors.verticalCenter: parent.verticalCenter }
        Label { x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: Theme.px(26)
                text: s.scn.name || qsTr("No scenario"); font { pixelSize: Theme.h2; weight: Font.DemiBold } }
        Label { x: Theme.s12; y: Theme.px(34); width: parent.width - 2 * x; height: Theme.px(22)
                text: s.planned ? qsTr("%1: %2").arg(s.scn.planNote).arg(s.scn.summary || "") : s.scn.summary || ""; color: Theme.textSecondary }
        Row {
            x: Theme.s12; y: Theme.px(64)
            spacing: Theme.s24
            Repeater {
                model: [[qsTr("Callsign"), s.scn.callsign], [qsTr("Type"), s.scn.type],
                        [qsTr("Runway"), s.scn.runway], [qsTr("Taxiway"), s.scn.taxiway]]
                Column {
                    Label { text: modelData[0]; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    Label { text: modelData[1] || "-"; mono: true; font { pixelSize: Theme.h1; weight: Font.Bold } }
                }
            }
        }
        Label { x: Theme.s12; y: Theme.px(126); width: parent.width - 2 * x; height: Theme.px(22); font.pixelSize: Theme.small
                objectName: "planLine"
                text: s.planned ? s.scn.planLine : qsTr("Half-duplex: %1").arg(s.scn.rule || "-") }
        // The instructor's note, plain guidance (D-143); the whole of it in the dialog.
        Label { x: Theme.s12; y: Theme.px(150); width: parent.width - 2 * x; height: Theme.px(40); visible: s.planned
                font.pixelSize: Theme.small; wrapMode: Text.WordWrap; maximumLineCount: 2; verticalAlignment: Text.AlignTop; text: s.scn.briefing || "" }
        Label { visible: !s.planned; x: Theme.s12; y: Theme.px(148); width: parent.width - 2 * x; height: Theme.px(22); font.pixelSize: Theme.small
                text: qsTr("Steps %1   SOP reference: %2").arg(s.scn.steps || 0).arg(s.scn.sop || qsTr("none")) }
        Row {
            x: Theme.s12
            anchors { bottom: parent.bottom; bottomMargin: Theme.s12 }
            spacing: Theme.s8
            SecondaryButton { visible: s.planner !== null; text: qsTr("Choose plan"); focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small
                              enabled: !s.dash.running; onClicked: { s.planner.clearMessage(); s.dialog_ = "plans" } }
            SecondaryButton { enabled: s.scenarios && !s.dash.running; text: s.scenarios ? qsTr("Choose scenario") : qsTr("Choose scenario (not built)"); focusPolicy: Qt.NoFocus
                              font.pixelSize: Theme.small; onClicked: s.scenariosAsked() }
            SecondaryButton { visible: s.planned && (s.scn.briefing || "") !== ""; text: qsTr("Briefing"); focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small
                              onClicked: s.dialog_ = "briefing" }
        }
    }

    // ---- exercise
    Card {
        id: exercise
        x: scenario.x + scenario.width + s.gap; y: scenario.y
        width: s.fullW - scenario.width - s.gap; height: scenario.height
        title: qsTr("EXERCISE")
        Label { x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: Theme.px(28)
                text: s.dash.running ? qsTr("RUNNING") : qsTr("NOT RUNNING")
                font { pixelSize: Theme.h2; weight: Font.DemiBold } }
        Label { x: Theme.s12; y: Theme.px(38); width: parent.width - 2 * x; height: Theme.px(22); font.pixelSize: Theme.small
                text: qsTr("Model: %1").arg(s.dash.modelName) }
        Label { x: Theme.s12; y: Theme.px(58); width: parent.width - 2 * x; height: Theme.px(18)
                color: Theme.textSecondary; font.pixelSize: Theme.caption
                text: s.dash.running ? qsTr("Locked for this exercise") : qsTr("Locked at Start") }
        Label { x: Theme.s12; y: Theme.px(82); width: parent.width - 2 * x; height: Theme.px(20)
                visible: !s.dash.running && s.dash.reasons.length > 0
                text: qsTr("Cannot start:"); font { pixelSize: Theme.small; weight: Font.DemiBold } }
        Column {
            x: Theme.s12; y: Theme.px(104); width: parent.width - 2 * x
            visible: !s.dash.running
            Repeater {
                model: s.dash.reasons
                Row {
                    spacing: Theme.s8
                    height: Theme.px(20)
                    StateIcon { level: LabController.Fault; anchors.verticalCenter: parent.verticalCenter }
                    Label { text: modelData; height: parent.height; font.pixelSize: Theme.small }
                }
            }
        }
    }

    // ---- actions
    Row {
        id: start
        x: s.gap; y: s.height - height - s.gap
        height: Theme.px(64)
        spacing: s.gap
        readonly property int small: Math.round((s.fullW - 3 * spacing) * 0.19)
        PrimaryButton { width: s.fullW - 3 * start.spacing - 3 * start.small; height: parent.height
                        enabled: s.dash.startEnabled; focusPolicy: Qt.NoFocus
                        onClicked: s.dash.startExercise()
                        text: s.dash.startEnabled ? qsTr("START EXERCISE") : qsTr("START EXERCISE (disabled)")
                        font { pixelSize: Theme.h2; weight: Font.DemiBold } }
        DangerButton { width: start.small; height: parent.height; text: qsTr("STOP"); enabled: s.dash.running
                       focusPolicy: Qt.NoFocus; onClicked: s.askStop = true }
        SecondaryButton { width: start.small; height: parent.height; text: qsTr("Settings"); focusPolicy: Qt.NoFocus; onClicked: s.settingsAsked()
                          font.pixelSize: Theme.small }
        SecondaryButton { width: start.small; height: parent.height; text: s.results ? qsTr("Results") : qsTr("Last result (not built)"); enabled: s.results && !s.dash.running
                          focusPolicy: Qt.NoFocus; onClicked: s.resultsAsked(); font.pixelSize: Theme.small }
    }

    Loader {
        anchors.fill: parent
        active: s.askStop || s.showClips || s.dialog_ !== ""
        sourceComponent: s.askStop ? stopDialog : s.showClips ? clipDialog : s.dialog_ === "plans" ? planDialog : briefingDialog
    }
    Component {
        id: planDialog
        ModalPanel {
            title: qsTr("Choose a plan")
            Label { width: parent.width; visible: s.planner.choices.length === 0; color: Theme.textSecondary; text: qsTr("No plan for you. Plans for anyone need no login.") }
            ListView {
                width: parent.width; height: Math.min(contentHeight, Theme.px(320))
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: Theme.s8
                model: s.planner.choices
                delegate: OptionButton { width: ListView.view.width; title: modelData.text; sub: modelData.note; enabled: !modelData.off
                                         selected: modelData.id === s.scn.plan; onClicked: s.planner.choose(modelData.id) }
            }
            Row {
                width: parent.width; height: Theme.px(22)
                spacing: Theme.s8
                StateIcon { level: s.planner.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                Label { height: parent.height; text: s.planner.message; font.pixelSize: Theme.small }
            }
            SecondaryButton { text: qsTr("Close"); focusPolicy: Qt.NoFocus; onClicked: s.dialog_ = "" }
        }
    }
    Component {
        id: briefingDialog
        ConfirmDialog {
            title: qsTr("Briefing")
            text: s.scn.briefing || ""
            closeText: qsTr("Close")
            onClosed: s.dialog_ = ""
        }
    }
    Component {
        id: stopDialog
        ConfirmDialog {
            title: qsTr("Stop the exercise?")
            text: qsTr("The exercise ends now and cannot be continued.")
            confirmText: qsTr("Stop exercise")
            onConfirmed: { s.askStop = false; s.dash.stopExercise() }
            onClosed: s.askStop = false
        }
    }
    Component {
        id: clipDialog
        ConfirmDialog {
            title: qsTr("Voice bank: %1").arg((s.dash.tiles[4] || {}).state || "")   // missing clips, or why the check failed
            rows: s.dash.missingClips
            closeText: qsTr("Close")
            onClosed: s.showClips = false
        }
    }
}
