import QtQuick
import QtQuick.Templates as T
import Atc

// Assessment and Debrief (04 section 5e): stored exercises, and one opened. Facts only, no score. View only.
Item {
    id: s
    property HistoryController history
    property string dialog            // "", trainee, scenario, days, verdict, delete
    signal loginAsked()
    signal keyboardFree()
    readonly property int gap: Theme.s8
    readonly property var h: history.head
    readonly property var e: history.entry
    onDialogChanged: if (dialog === "") keyboardFree()
    Component.onCompleted: history.enter()
    Component.onDestruction: history.leave()
    Connections { target: s.history; function onDetailChanged() { if (!s.history.open && s.dialog === "verdict") s.dialog = "" } }
    MouseArea { anchors.fill: parent; onPressed: s.keyboardFree() }

    component F: BaseButton { anchors.verticalCenter: parent.verticalCenter; focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small }

    Column {
        anchors.centerIn: parent
        visible: !s.history.allowed
        spacing: Theme.s12
        Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Log in to see results. Guest results are in no history."); color: Theme.textSecondary }
        SecondaryButton { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Log in"); focusPolicy: Qt.NoFocus; onClicked: s.loginAsked() }
    }

    // ---- history
    Item {
        visible: s.history.allowed && !s.history.open
        anchors { fill: parent; margins: s.gap }
        Card {
            id: bar
            width: parent.width; height: Theme.px(64)
            Row {
                x: Theme.s8; height: parent.height
                spacing: Theme.s8
                F { width: Theme.px(148); enabled: s.history.instructor; text: s.history.trainee ? qsTr("Trainee %1").arg(s.history.trainee) : s.history.instructor ? qsTr("All trainees") : qsTr("My results"); onClicked: s.dialog = "trainee" }
                Repeater {
                    model: [["practice", qsTr("Practice")], ["assessed", qsTr("Assessed")], ["all", qsTr("All")]]
                    ChoiceButton { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: Theme.small; text: modelData[1]; selected: s.history.mode === modelData[0]; onClicked: s.history.setMode(modelData[0]) }
                }
                F { width: Theme.px(176); text: s.history.scenario || qsTr("All scenarios"); onClicked: s.dialog = "scenario" }
                F { width: Theme.px(132); text: s.history.days > 0 ? qsTr("Last %1 days").arg(s.history.days) : qsTr("All time"); onClicked: s.dialog = "days" }
            }
            Label { anchors { right: parent.right; rightMargin: Theme.s12 } height: parent.height; text: s.history.countText; font.pixelSize: Theme.small; color: Theme.textSecondary }
        }
        Card {
            id: tbl
            anchors { top: bar.bottom; topMargin: s.gap; bottom: trend.top; bottomMargin: s.gap }
            width: parent.width
            readonly property var cols: [0.15, 0.07, 0.19, 0.10, 0.11, 0.07, 0.07, 0.07, 0.08, 0.05, 0.04]
            Row {
                id: headRow
                x: Theme.s12; width: parent.width - 2 * x; height: Theme.px(36)
                Repeater {
                    model: [qsTr("Date"), qsTr("Trainee"), qsTr("Scenario"), qsTr("Mode"), qsTr("End"), qsTr("Steps"), qsTr("Errors"), qsTr("Missed"), qsTr("Response"), qsTr("STT"), qsTr("PTT")]
                    Label { objectName: "historyHead"; width: headRow.width * tbl.cols[index]; height: parent.height; text: modelData; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                }
            }
            ListView {
                y: Theme.px(36); width: parent.width; height: parent.height - y
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.history.rows
                delegate: T.Button {
                    id: r
                    width: ListView.view.width; height: Theme.touch
                    focusPolicy: Qt.NoFocus
                    onClicked: s.history.select(modelData.id)
                    onDoubleClicked: { s.history.select(modelData.id); s.history.openSelected() }
                    Accessible.name: modelData.date + " " + modelData.scenario
                    background: Rectangle {
                        color: modelData.selected ? Theme.raised : "transparent"
                        Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                    contentItem: Row {
                        leftPadding: Theme.s12
                        readonly property int w: r.width - 2 * Theme.s12
                        Label { width: parent.w * tbl.cols[0]; height: parent.height; text: modelData.date; mono: true; font.pixelSize: Theme.caption }
                        Label { width: parent.w * tbl.cols[1]; height: parent.height; text: modelData.trainee; mono: true; font.pixelSize: Theme.small }
                        Label { width: parent.w * tbl.cols[2]; height: parent.height; rightPadding: Theme.s8; text: modelData.scenario; font.pixelSize: Theme.small }
                        Label { width: parent.w * tbl.cols[3]; height: parent.height; text: modelData.mode; font.pixelSize: Theme.small }
                        Row {
                            width: parent.w * tbl.cols[4]; height: parent.height
                            spacing: Theme.s4
                            StateIcon { level: modelData.level; anchors.verticalCenter: parent.verticalCenter }
                            Label { height: parent.height; text: modelData.end; font.pixelSize: Theme.small }
                        }
                        Repeater {
                            model: [modelData.steps, modelData.reviewed > 0 ? qsTr("%1 *").arg(modelData.errors) : modelData.errors, modelData.missed, modelData.response, modelData.suspected, modelData.ptt]
                            Label { width: parent.w * tbl.cols[5 + index]; height: parent.height; text: modelData; mono: true; font.pixelSize: Theme.small }
                        }
                    }
                }
            }
            Label { anchors.centerIn: parent; visible: s.history.loaded && s.history.rows.length === 0; text: qsTr("No results."); color: Theme.textSecondary }
        }
        Card {
            id: trend
            anchors.bottom: acts.top
            anchors.bottomMargin: s.gap
            width: parent.width; height: Theme.px(176)
            title: qsTr("OVER TIME (one trainee, one scenario, at most 20)")
            headerItems: Label { anchors.right: parent.right; height: parent.height; text: qsTr("Suspected STT is never added to errors. * reclassified"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
            Label { anchors.centerIn: parent; visible: s.history.trendNote !== ""; text: s.history.trendNote; color: Theme.textSecondary }
            Column {
                x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x
                Repeater {
                    model: s.history.trend
                    Row {
                        id: tr_
                        readonly property var v: modelData.values
                        height: Theme.px(30)
                        Label { width: Theme.px(210); height: parent.height; text: modelData.name; font.pixelSize: Theme.small }
                        Repeater { model: tr_.v
                                   Label { width: Theme.px(44); height: parent.height; text: modelData; mono: true; font.pixelSize: Theme.small; horizontalAlignment: Text.AlignRight } }
                    }
                }
            }
        }
        Item {
            id: acts
            anchors.bottom: parent.bottom
            width: parent.width; height: Theme.touch
            Row {
                spacing: Theme.s8
                PrimaryButton { text: qsTr("Open debrief"); enabled: s.history.selected !== "" && !s.history.busy; focusPolicy: Qt.NoFocus; onClicked: s.history.openSelected() }
                DangerButton { visible: s.history.instructor; text: qsTr("Delete result"); enabled: s.history.selected !== "" && !s.history.busy; focusPolicy: Qt.NoFocus; onClicked: s.dialog = "delete" }
                StateIcon { level: s.history.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                Label { height: Theme.touch; text: s.history.message; font.pixelSize: Theme.small }
            }
            Row {
                anchors.right: parent.right
                height: parent.height
                spacing: Theme.s8
                Tag { anchors.verticalCenter: parent.verticalCenter; text: qsTr("PLACEHOLDER") }
                Label { height: parent.height; text: qsTr("No pass mark"); color: Theme.textSecondary; font.pixelSize: Theme.small }
            }
        }
    }

    // ---- debrief
    Item {
        visible: s.history.open
        anchors { fill: parent; margins: s.gap }
        Card {
            id: top_
            width: parent.width; height: Theme.px(72)
            SecondaryButton { id: back; x: Theme.s8; anchors.verticalCenter: parent.verticalCenter; text: qsTr("< History"); focusPolicy: Qt.NoFocus; onClicked: s.history.close() }
            Label { x: back.x + back.width + Theme.s12; y: Theme.s8; height: Theme.px(26); font.weight: Font.DemiBold
                    text: qsTr("%1   %2   %3   %4   %5").arg(s.h.trainee || "").arg(s.h.scenario || "").arg(s.h.callsign || "").arg(s.h.mode || "").arg(s.h.when || "") }
            Label { x: back.x + back.width + Theme.s12; y: Theme.px(38); height: Theme.px(22); text: (s.h.audio || "") === "" ? (s.h.sub || "") : qsTr("%1   %2").arg(s.h.sub || "").arg(s.h.audio); color: Theme.textSecondary; font.pixelSize: Theme.small }
            Row {
                anchors { right: parent.right; rightMargin: Theme.s12 }
                y: Theme.s8
                spacing: Theme.s8
                StateIcon { level: s.h.level || 0; size: Theme.px(16); anchors.verticalCenter: parent.verticalCenter }
                Label { text: s.h.end || ""; font { pixelSize: Theme.h2; weight: Font.Bold } }
                Tag { visible: (s.h.changed || "") !== ""; text: s.h.changed || ""; anchors.verticalCenter: parent.verticalCenter }
                Tag { visible: s.h.placeholder === true; text: qsTr("PLACEHOLDER"); anchors.verticalCenter: parent.verticalCenter }
            }
        }
        Card {
            id: facts
            anchors { top: top_.bottom; topMargin: s.gap; bottom: parent.bottom }
            width: Theme.px(288)
            title: qsTr("SCORECARDS (facts)")
            Column {
                id: fc
                x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x
                Repeater {
                    model: s.history.cards
                    Item {
                        width: fc.width; height: Theme.px(32)
                        Label { height: parent.height; text: modelData.name; color: Theme.textSecondary; font.pixelSize: Theme.small }
                        Label { anchors.right: parent.right; width: Theme.px(150); height: parent.height; horizontalAlignment: Text.AlignRight; text: modelData.value; font { pixelSize: Theme.small; weight: Font.DemiBold } }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                }
                Repeater { model: s.history.pttNotes
                           Label { width: fc.width; height: Theme.px(22); text: modelData; font.pixelSize: Theme.caption; color: Theme.textSecondary } }
            }
            SectionHeader { id: sttH; y: Math.round(parent.height * 0.62); width: parent.width; text: qsTr("SUSPECTED STT ERRORS   %1").arg(s.history.suspected.length) }
            Column {
                x: Theme.s12; width: parent.width - 2 * x
                anchors { top: sttH.bottom; topMargin: Theme.s4 }
                Label { width: parent.width; height: Theme.px(22); text: qsTr("not counted as trainee errors"); color: Theme.textSecondary; font.pixelSize: Theme.small }
                Repeater { model: s.history.suspected
                           Label { width: parent.width; height: Theme.px(40); text: modelData; wrapMode: Text.WordWrap; maximumLineCount: 2; font.pixelSize: Theme.small } }
            }
        }
        Card {
            id: line
            x: facts.width + s.gap
            anchors { top: facts.top; bottom: parent.bottom }
            width: parent.width - x - s.gap - one.width
            title: qsTr("TIMELINE")
            Row {
                id: filt
                x: Theme.s8; y: Theme.s4
                spacing: Theme.s4
                Repeater {
                    model: [["all", qsTr("All")], ["errors", qsTr("Errors")], ["ptt", qsTr("PTT")], ["stt", qsTr("STT")]]
                    ChoiceButton { font.pixelSize: Theme.small; text: modelData[1]; selected: s.history.entryFilter === modelData[0]; onClicked: s.history.setEntryFilter(modelData[0]) }
                }
            }
            ListView {
                anchors { top: filt.bottom; topMargin: Theme.s4; bottom: parent.bottom }
                width: parent.width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.history.entries
                delegate: T.Button {
                    id: en
                    width: ListView.view.width; height: Theme.px(60)
                    focusPolicy: Qt.NoFocus
                    onClicked: { s.keyboardFree(); s.history.selectEntry(modelData.no) }
                    Accessible.name: modelData.time + " " + modelData.who
                    background: Rectangle {
                        color: modelData.selected ? Theme.raised : "transparent"
                        Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                    contentItem: Item {
                        Label { x: Theme.s12; y: Theme.s4; width: Theme.px(76); height: Theme.px(24); text: modelData.time; mono: true; font.pixelSize: Theme.small }
                        Label { x: Theme.px(92); y: Theme.s4; width: Theme.px(52); height: Theme.px(24); text: modelData.who; font { pixelSize: Theme.caption; weight: Font.DemiBold } color: Theme.textSecondary }
                        Label { x: Theme.px(148); y: Theme.s4; width: parent.width - x - Theme.s8; height: Theme.px(24); text: modelData.text; font.pixelSize: Theme.small }
                        StateIcon { id: oi; x: Theme.px(148); y: Theme.px(36); level: modelData.outcome ? modelData.level : 0 }
                        Label { x: oi.visible ? Theme.px(164) : Theme.px(148); y: Theme.px(30); width: parent.width - x - Theme.s8; height: Theme.px(24); text: modelData.outcome; font.pixelSize: Theme.small }
                    }
                }
            }
        }
        Card {
            id: one
            anchors { right: parent.right; top: facts.top; bottom: parent.bottom }
            width: Theme.px(304)
            title: qsTr("ENTRY")
            Label { anchors { fill: parent; margins: Theme.s12 } visible: s.e.title === undefined; text: qsTr("Choose an entry."); color: Theme.textSecondary; verticalAlignment: Text.AlignTop }
            Item {
                visible: s.e.title !== undefined
                x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: parent.height - 2 * y
                Column {
                    id: ec
                    width: parent.width
                    Label { width: parent.width; height: Theme.px(28); text: s.e.title || ""; font.weight: Font.DemiBold }
                    Label { visible: s.e.atc !== true; width: parent.width; height: Theme.px(60); text: s.e.text || ""; wrapMode: Text.WordWrap; maximumLineCount: 3; font.pixelSize: Theme.small }
                    Label { visible: s.e.atc === true; width: parent.width; height: Theme.px(18); text: qsTr("Heard (as recognised)"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    Label { visible: s.e.atc === true; width: parent.width; height: Theme.px(54); text: s.e.heard || ""; wrapMode: Text.WordWrap; maximumLineCount: 3; verticalAlignment: Text.AlignTop; mono: true; font.pixelSize: Theme.small }
                    Repeater {
                        model: s.e.atc === true ? [[qsTr("Parsed"), s.e.parsed], [qsTr("Expected"), s.e.expected], [qsTr("Outcome"), s.e.outcome], [qsTr("Class"), s.e.cls], [qsTr("Delay"), s.e.delay]] : []
                        Item {
                            width: ec.width; height: Theme.px(24)
                            Label { width: Theme.px(80); height: parent.height; text: modelData[0]; color: Theme.textSecondary; font.pixelSize: Theme.small }
                            Label { x: Theme.px(80); width: parent.width - x; height: parent.height; text: modelData[1] || "-"; mono: true; font.pixelSize: Theme.small }
                        }
                    }
                    Row {
                        visible: s.e.atc === true
                        height: Theme.touch
                        spacing: Theme.s4
                        SecondaryButton { text: qsTr("Play"); width: Theme.px(92); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.e.audio === true && !s.history.busy; onClicked: s.history.play(false) }
                        SecondaryButton { text: qsTr("Keyed part"); width: Theme.px(108); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.e.audio === true && !s.history.busy; onClicked: s.history.play(true) }
                        SecondaryButton { visible: s.history.playing; text: qsTr("Stop"); width: Theme.px(80); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: s.history.stopPlay() }
                    }
                    Label { visible: s.e.atc === true; width: parent.width; height: Theme.px(20); text: s.e.audioNote || ""; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                }
                Rectangle { id: rl; anchors { top: ec.bottom; topMargin: Theme.s4 } width: parent.width; height: Theme.line; color: Theme.divider }
                Label { id: rt; anchors.top: rl.bottom; width: parent.width; height: Theme.px(26); text: s.history.reviews.length > 0 ? qsTr("RECLASSIFIED (the original is kept)") : s.e.atc === true ? qsTr("Not reclassified") : ""; color: Theme.textSecondary; font { pixelSize: Theme.caption; weight: Font.DemiBold } }
                ListView {
                    anchors { top: rt.bottom; bottom: rb.top }
                    width: parent.width
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: s.history.reviews
                    delegate: Column {
                        width: ListView.view.width
                        Label { width: parent.width; height: Theme.px(22); text: modelData.verdict; font { pixelSize: Theme.small; weight: Font.DemiBold } }
                        Label { width: parent.width; height: text ? Theme.px(34) : 0; text: modelData.note; wrapMode: Text.WordWrap; maximumLineCount: 2; font.pixelSize: Theme.small }
                        Label { width: parent.width; height: Theme.px(20); text: modelData.by; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    }
                }
                Column {
                    id: rb
                    anchors.bottom: parent.bottom
                    width: parent.width
                    Row {
                        width: parent.width; height: Theme.px(22)
                        spacing: Theme.s4
                        StateIcon { level: s.history.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                        Label { height: parent.height; width: ec.width - Theme.px(20); text: s.history.message; font.pixelSize: Theme.caption }
                    }
                    SecondaryButton { width: parent.width; text: s.history.instructor ? qsTr("Reclassify...") : qsTr("Reclassify: instructor only"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                                      enabled: s.e.mayReclassify === true && !s.history.busy; onClicked: s.dialog = "verdict" }
                }
            }
        }
    }

    // ---- dialogs
    Loader {
        anchors.fill: parent
        active: ["trainee", "scenario", "days"].indexOf(s.dialog) >= 0
        sourceComponent: Component {
            PickDialog {
                title: ({ trainee: qsTr("Trainee"), scenario: qsTr("Scenario"), days: qsTr("Time") })[s.dialog] || ""
                chosenId: s.dialog === "trainee" ? s.history.trainee : s.dialog === "scenario" ? s.history.scenario : String(s.history.days)
                options: s.dialog === "trainee" ? [{ id: "", text: qsTr("All trainees") }].concat(s.history.trainees.map(x => ({ id: x, text: x })))
                       : s.dialog === "scenario" ? [{ id: "", text: qsTr("All scenarios") }].concat(s.history.scenarios.map(x => ({ id: x, text: x })))
                       : [{ id: "7", text: qsTr("Last 7 days") }, { id: "30", text: qsTr("Last 30 days") }, { id: "90", text: qsTr("Last 90 days") }, { id: "0", text: qsTr("All time") }]
                onPicked: (id) => {
                    const d = s.dialog
                    s.dialog = ""
                    if (d === "trainee") s.history.setTrainee(id)
                    else if (d === "scenario") s.history.setScenario(id)
                    else s.history.setDays(Number(id))
                }
                onClosed: s.dialog = ""
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "delete"
        sourceComponent: Component {
            ConfirmDialog { title: qsTr("Delete this result?"); text: qsTr("The exercise log and its saved audio are deleted from this machine. The deletion is logged first.")
                            confirmText: qsTr("Delete result"); onConfirmed: { s.dialog = ""; s.history.removeSelected() } onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "verdict"
        sourceComponent: Component {
            ModalPanel {
                id: vd
                property string verdict
                title: qsTr("Reclassify")
                Label { width: parent.width; text: qsTr("Original: %1. It is kept; both are shown.").arg(s.e.outcome || "-"); wrapMode: Text.WordWrap }
                Column {
                    width: parent.width
                    spacing: Theme.s4
                    Repeater { model: [qsTr("Trainee error"), qsTr("Speech recognition error"), qsTr("Not an error")]
                               ChoiceButton { width: parent.width; text: modelData; selected: vd.verdict === modelData; onClicked: vd.verdict = modelData } }
                }
                TextEntry { id: noteT; width: parent.width; label: qsTr("Note (optional)"); maximumLength: 200 }
                Row {
                    spacing: Theme.s12
                    PrimaryButton { text: qsTr("Store"); enabled: vd.verdict !== "" && !s.history.busy; focusPolicy: Qt.NoFocus
                                    onClicked: { s.history.reclassify(vd.verdict, noteT.text); s.dialog = "" } }
                    SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: s.dialog = "" }
                }
            }
        }
    }
}
