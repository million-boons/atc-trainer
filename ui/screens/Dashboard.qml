import QtQuick
import Atc

// Dashboard (D-103, D-106). View only.
Item {
    id: s
    property DashboardController dash
    property bool askStop: false
    property bool showClips: false
    readonly property int gap: Theme.s12
    readonly property int fullW: width - 2 * gap
    readonly property var scn: dash.scenario

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
                text: s.scn.summary || ""; color: Theme.textSecondary }
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
                text: qsTr("Half-duplex: %1").arg(s.scn.rule || "-") }
        Label { x: Theme.s12; y: Theme.px(148); width: parent.width - 2 * x; height: Theme.px(22); font.pixelSize: Theme.small
                text: qsTr("Steps %1   SOP reference: %2").arg(s.scn.steps || 0).arg(s.scn.sop || qsTr("none")) }
        SecondaryButton { x: Theme.s12; anchors { bottom: parent.bottom; bottomMargin: Theme.s12 }
                          enabled: false; text: qsTr("Choose scenario (not built)") }
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
                        text: s.dash.startEnabled ? qsTr("START EXERCISE") : qsTr("START EXERCISE (disabled)")
                        font { pixelSize: Theme.h2; weight: Font.DemiBold } }
        DangerButton { width: start.small; height: parent.height; text: qsTr("STOP"); enabled: s.dash.running
                       focusPolicy: Qt.NoFocus; onClicked: s.askStop = true }
        SecondaryButton { width: start.small; height: parent.height; text: qsTr("Settings (not built)"); enabled: false
                          font.pixelSize: Theme.small }
        SecondaryButton { width: start.small; height: parent.height; text: qsTr("Last result (not built)"); enabled: false
                          font.pixelSize: Theme.small }
    }

    Loader {
        anchors.fill: parent
        active: s.askStop || s.showClips
        sourceComponent: s.askStop ? stopDialog : clipDialog
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
            title: qsTr("Missing clips (%1)").arg(s.dash.missingClips.length)
            rows: s.dash.missingClips
            closeText: qsTr("Close")
            onClosed: s.showClips = false
        }
    }
}
