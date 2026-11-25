import QtQuick
import QtQuick.Templates as T
import Atc

// Training Content, tab Exercises: the Exercise Planner (04 section 5g). Instructor only; not during an exercise.
// View only: what fits, what may be saved and what may start is decided in PlannerController.
Item {
    id: s
    property PlannerController planner
    property bool phrasesOn: true
    property string dialog            // "", scenario, trainee, aircraft, runway, taxiway, presets, delete, filter
    signal tabAsked(string id)
    signal dashboardAsked()
    signal loginAsked()
    signal keyboardFree()
    readonly property int gap: Theme.s8
    readonly property bool on: planner.ready
    readonly property var p: planner.plan
    readonly property var radios: [["off", qsTr("Off")], ["light", qsTr("Light")], ["medium", qsTr("Medium")], ["heavy", qsTr("Heavy")]]
    onDialogChanged: if (dialog === "") keyboardFree()
    onOnChanged: if (!on) dialog = ""
    Component.onCompleted: { planner.enter(); fields.take() }

    function handleKey(key, repeat) { return false }   // no shortcuts here
    function free() { note.leave(); keyboardFree() }
    function ask(d) { free(); dialog = d }

    component Pick: Column {
        id: pk
        property string label
        property string value
        property bool bad: false
        property bool noted: false       // a line below for "does not fit": always there, so that nothing moves
        signal asked()
        Label { height: Theme.px(22); text: pk.label; color: Theme.textSecondary; font.pixelSize: Theme.caption }
        BaseButton { width: pk.width; text: pk.value || qsTr("choose"); font { family: Theme.mono; pixelSize: Theme.small } focusPolicy: Qt.NoFocus
                     tint: pk.bad ? Theme.fault : Theme.border; leftPadding: pk.bad ? Theme.px(22) : 0
                     onClicked: pk.asked()
                     StateIcon { x: Theme.s8; anchors.verticalCenter: parent.verticalCenter; level: pk.bad ? LabController.Fault : 0 } }
        Label { visible: pk.noted; height: Theme.px(18); text: pk.bad ? qsTr("does not fit") : ""; font.pixelSize: Theme.caption }
    }
    component Head: Label { height: Theme.px(22); color: Theme.textSecondary; font { pixelSize: Theme.caption; weight: Font.DemiBold; letterSpacing: Theme.px(1) } }
    component Line: Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
    component Choice: ChoiceButton { font.pixelSize: Theme.small }

    QtObject {
        id: fields
        // The fields follow the controller only when another plan was opened, never while somebody types.
        function take() { name.text = s.p.name || ""; note.text = s.p.briefing || "" }
    }
    Connections {
        target: s.planner
        function onPlanLoaded() { fields.take(); pane.contentY = 0 }
        function onPresetsSaved() { if (s.dialog === "presets") s.dialog = "" }
    }
    MouseArea { anchors.fill: parent; onPressed: s.free() }

    TrainingTabs {
        id: tabs
        x: s.gap; y: s.gap; width: s.width - 2 * s.gap; height: Theme.px(56)
        current: "exercises"
        phrasesOn: s.phrasesOn
        exercisesOn: true
        onPicked: (id) => { s.free(); s.tabAsked(id) }
    }

    Column {
        anchors.centerIn: parent
        visible: !s.on
        spacing: Theme.s12
        Label { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.textSecondary
                text: s.planner.instructor ? s.planner.message || qsTr("Loading") : qsTr("Instructor only. Log in to plan exercises.") }
        SecondaryButton { anchors.horizontalCenter: parent.horizontalCenter; visible: !s.planner.instructor; text: qsTr("Log in"); focusPolicy: Qt.NoFocus; onClicked: s.loginAsked() }
    }

    Item {
        id: work
        visible: s.on
        anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: s.gap }

        // ---- plans
        Card {
            id: list
            width: Theme.px(272); height: parent.height
            title: qsTr("PLANS")
            headerItems: Label { anchors.right: parent.right; height: parent.height; text: s.planner.planCount; mono: true; font.pixelSize: Theme.caption; color: Theme.textSecondary }
            TextEntry { id: find; x: Theme.s8; y: Theme.s4; width: parent.width - 2 * x; label: qsTr("Search"); maximumLength: 40
                        onTextChanged: s.planner.search(text); onAccepted: s.keyboardFree() }
            BaseButton { id: who; x: Theme.s8; anchors { top: find.bottom; topMargin: Theme.s4 } width: parent.width - 2 * x; font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                         text: s.planner.filter === "all" ? qsTr("All plans") : s.planner.filter === "anyone" ? qsTr("Plans for anyone") : qsTr("Plans for %1").arg(s.planner.filter)
                         onClicked: s.ask("filter") }
            ListView {
                anchors { top: who.bottom; topMargin: Theme.s4; bottom: newRow.top; bottomMargin: Theme.s4 }
                width: parent.width
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.planner.plans
                delegate: T.Button {
                    id: row
                    width: ListView.view.width; height: Theme.px(88)
                    focusPolicy: Qt.NoFocus
                    onClicked: { s.free(); s.planner.select(modelData.id) }
                    Accessible.name: modelData.name
                    background: Rectangle {
                        color: modelData.selected ? Theme.raised : "transparent"
                        Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                    contentItem: Item {
                        Label { x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: Theme.px(22); text: modelData.name; font { pixelSize: Theme.small; weight: Font.DemiBold } }
                        Label { x: Theme.s12; y: Theme.px(26); width: parent.width - 2 * x; height: Theme.px(18); text: modelData.who; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                        Label { x: Theme.s12; y: Theme.px(44); width: parent.width - 2 * x; height: Theme.px(18); text: modelData.runs; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                        StateIcon { id: st; x: Theme.s12; y: Theme.px(66); level: modelData.level }
                        Label { objectName: "planState"; x: st.x + st.width + Theme.s4; y: Theme.px(62); width: parent.width - x - Theme.s8; height: Theme.px(20); text: modelData.state; font.pixelSize: Theme.caption }
                    }
                }
            }
            Row {
                id: newRow
                x: Theme.s8; width: parent.width - 2 * x
                anchors { bottom: parent.bottom; bottomMargin: Theme.s8 }
                spacing: Theme.s8
                SecondaryButton { width: (parent.width - Theme.s8) / 2; text: qsTr("+ New"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: !s.planner.busy; onClicked: { s.free(); s.planner.newPlan() } }
                SecondaryButton { width: (parent.width - Theme.s8) / 2; text: qsTr("Copy"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.planner.hasPlan && !s.planner.busy; onClicked: { s.free(); s.planner.copyPlan() } }
            }
        }

        // ---- the plan
        Card {
            id: mid
            x: list.width + s.gap; width: parent.width - x - s.gap - side.width; height: parent.height
            title: s.p.isNew === true ? qsTr("NEW PLAN") : qsTr("PLAN")
            Label { anchors.centerIn: parent; visible: !s.planner.hasPlan; color: Theme.textSecondary; text: qsTr("No plan yet. Press New.") }
            Flickable {
                id: pane
                visible: s.planner.hasPlan
                x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: foot.y - y - Theme.s4
                contentHeight: form.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: form
                    width: pane.width - Theme.s8
                    spacing: Theme.s8
                    TextEntry { id: name; width: parent.width; label: qsTr("Name"); maximumLength: 40
                                onTextChanged: if (text !== (s.p.name || "")) s.planner.setName(text)
                                onAccepted: s.keyboardFree() }
                    Row {
                        width: parent.width
                        spacing: Theme.s8
                        Pick { width: Math.round(parent.width * 0.5); label: qsTr("Scenario"); value: s.p.scenario || ""; onAsked: s.ask("scenario") }
                        Column {
                            y: Theme.px(22)
                            spacing: Theme.s4
                            Tag { visible: s.p.placeholder === true; text: qsTr("PLACEHOLDER") }
                            Label { text: s.p.scenarioNote || ""; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.s8
                        Pick { width: Math.round(parent.width * 0.3); label: qsTr("Trainee"); value: s.p.trainee || ""; onAsked: s.ask("trainee") }
                        Column {
                            Label { height: Theme.px(22); text: qsTr("Mode"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                            Row {
                                spacing: Theme.s8
                                Choice { text: qsTr("Practice"); selected: s.p.assessed !== true; onClicked: { s.free(); s.planner.setAssessed(false) } }
                                Choice { text: qsTr("Assessed"); selected: s.p.assessed === true; enabled: s.p.assessedOn === true; onClicked: { s.free(); s.planner.setAssessed(true) } }
                            }
                            Row {
                                height: Theme.px(18)
                                spacing: Theme.s4
                                StateIcon { level: s.p.assessedNote ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
                                Label { height: parent.height; text: s.p.assessedNote || ""; font.pixelSize: Theme.caption; color: Theme.textSecondary }
                            }
                        }
                    }
                    Item {
                        width: parent.width; height: Theme.px(20)
                        Label { height: parent.height; text: qsTr("Briefing note (optional): guidance, not phraseology"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                        Label { anchors.right: parent.right; height: parent.height; text: s.p.briefingLeft || ""; color: Theme.textSecondary; mono: true; font.pixelSize: Theme.caption }
                    }
                    NoteEdit { id: note; width: parent.width; height: Theme.px(64); limit: 200; name: qsTr("Briefing note"); onEdited: (t) => s.planner.setBriefing(t); onLeaving: s.free() }
                    Line {}
                    Row {
                        spacing: Theme.s8
                        Head { height: Theme.touch; text: qsTr("VALUES") }
                        Choice { text: qsTr("Fixed"); selected: s.p.fixed === true; onClicked: { s.free(); s.planner.setFixed(true) } }
                        Choice { text: qsTr("Random"); selected: s.p.fixed === false; onClicked: { s.free(); s.planner.setFixed(false) } }
                    }
                    Row {
                        width: parent.width
                        visible: s.p.fixed === true
                        spacing: Theme.s8
                        Pick { width: Math.round((parent.width - 2 * Theme.s8) * 0.46); label: qsTr("Callsign and type"); value: s.p.aircraft || ""; bad: s.p.aircraftBad === true; noted: true; onAsked: s.ask("aircraft") }
                        Pick { width: Math.round((parent.width - 2 * Theme.s8) * 0.27); label: qsTr("Runway"); value: s.p.runway || ""; bad: s.p.runwayBad === true; noted: true; onAsked: s.ask("runway") }
                        Pick { width: Math.round((parent.width - 2 * Theme.s8) * 0.27); label: qsTr("Taxiway"); value: s.p.taxiway || ""; bad: s.p.taxiwayBad === true; noted: true; onAsked: s.ask("taxiway") }
                    }
                    Label { visible: s.p.fixed === false; width: parent.width; height: Theme.px(88); wrapMode: Text.WordWrap; font.pixelSize: Theme.small; color: Theme.textSecondary
                            text: qsTr("Drawn when the plan is put on the Dashboard, and shown there. Never exactly the combination of this person's previous run.") }
                    Line {}
                    Item {
                        width: parent.width; height: Theme.touch
                        Head { height: parent.height; text: qsTr("DIFFICULTY") }
                        SecondaryButton { anchors.right: parent.right; text: qsTr("Presets..."); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: !s.planner.busy
                                          onClicked: { s.planner.clearMessage(); s.planner.editPresets(); s.ask("presets") } }
                    }
                    Row {
                        spacing: Theme.s8
                        Repeater {
                            model: [qsTr("Easy"), qsTr("Normal"), qsTr("Hard"), qsTr("Custom")]
                            Choice { width: Theme.px(116); text: modelData; selected: s.p.preset === index; onClicked: { s.free(); s.planner.setPreset(index) } }
                        }
                    }
                    Row {
                        height: Theme.px(18)
                        spacing: Theme.s4
                        StateIcon { level: s.p.presetChanged ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
                        Label { height: parent.height; text: s.p.presetChanged ? qsTr("Preset changed. This plan keeps the values below.") : ""; font.pixelSize: Theme.caption }
                    }
                    Row {
                        spacing: Theme.s8
                        Label { width: Theme.px(84); height: Theme.touch; text: qsTr("Reply after"); font.pixelSize: Theme.small }
                        DelayStep { name: qsTr("Shortest reply delay"); value: s.p.from || ""; onStepped: (n) => { s.free(); s.planner.stepReply(true, n) } }
                        Label { height: Theme.touch; text: qsTr("to"); font.pixelSize: Theme.small }
                        DelayStep { name: qsTr("Longest reply delay"); value: s.p.to || ""; onStepped: (n) => { s.free(); s.planner.stepReply(false, n) } }
                    }
                    Row {
                        spacing: Theme.s8
                        Label { width: Theme.px(52); height: Theme.touch; text: qsTr("Radio"); font.pixelSize: Theme.small }
                        Repeater {
                            model: s.radios
                            Choice { width: Theme.px(106); text: modelData[1]; selected: s.p.radio === modelData[0]; onClicked: { s.free(); s.planner.setRadio(modelData[0]) } }
                        }
                    }
                    Label { width: parent.width; height: Theme.px(24); text: s.p.events || ""; font.pixelSize: Theme.small; color: Theme.textSecondary }
                }
            }
            // More below: a bar, because the pane scrolls.
            Rectangle {
                visible: pane.visible && pane.contentHeight > pane.height
                x: parent.width - width - Theme.s4; width: Theme.px(4); radius: width / 2
                y: pane.y + pane.visibleArea.yPosition * pane.height; height: pane.visibleArea.heightRatio * pane.height
                color: Theme.textMuted
            }
            Item {
                id: foot
                visible: s.planner.hasPlan
                x: Theme.s12; width: parent.width - 2 * x; height: Theme.px(88)
                anchors { bottom: parent.bottom; bottomMargin: Theme.s8 }
                Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
                Row {
                    y: Theme.s8; height: Theme.px(20)
                    spacing: Theme.s4
                    StateIcon { level: s.planner.message ? s.planner.messageLevel : 0; anchors.verticalCenter: parent.verticalCenter }
                    Label { objectName: "planMessage"; height: parent.height; width: foot.width - Theme.px(20); font.pixelSize: Theme.caption; text: s.planner.message || s.planner.saveNote }
                }
                Row {
                    anchors.bottom: parent.bottom
                    spacing: Theme.s8
                    PrimaryButton { text: qsTr("Save"); enabled: s.planner.canSave; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.planner.save() } }
                    SecondaryButton { text: qsTr("Undo"); enabled: s.planner.dirty && !s.planner.busy; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.planner.undo() } }
                }
                DangerButton { anchors { right: parent.right; bottom: parent.bottom } text: qsTr("Delete"); enabled: s.p.isNew === false && !s.planner.busy; focusPolicy: Qt.NoFocus; onClicked: s.ask("delete") }
            }
        }

        // ---- preview and checks
        Card {
            id: side
            anchors.right: parent.right
            width: Theme.px(336); height: parent.height
            title: qsTr("ON THE DASHBOARD")
            readonly property var v: s.planner.preview
            Column {
                x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x
                visible: s.planner.hasPlan
                spacing: Theme.s4
                Label { width: parent.width; height: Theme.px(26); text: side.v.name || qsTr("No name"); font { pixelSize: Theme.h2; weight: Font.DemiBold } }
                Label { width: parent.width; text: side.v.planNote || ""; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                Label { width: parent.width; height: Theme.px(32); mono: true; font { pixelSize: Theme.h1; weight: Font.Bold }
                        text: side.v.drawn ? qsTr("drawn then") : side.v.callsign || "-" }
                Label { width: parent.width; height: Theme.px(24); mono: true; font { pixelSize: Theme.body; weight: Font.Bold }
                        text: side.v.drawn ? "" : qsTr("%1   RWY %2   TWY %3").arg(side.v.type || "-").arg(side.v.runway || "-").arg(side.v.taxiway || "-") }
                Label { width: parent.width; text: side.v.planLine || ""; font.pixelSize: Theme.caption; wrapMode: Text.WordWrap }
                Label { width: parent.width; height: Theme.px(52); text: side.v.briefing || ""; font.pixelSize: Theme.small; wrapMode: Text.WordWrap; maximumLineCount: 3; verticalAlignment: Text.AlignTop }
            }
            SectionHeader { id: chk; y: Theme.px(236); width: parent.width; text: qsTr("CHECKS") }
            Column {
                x: Theme.s12; width: parent.width - 2 * x
                anchors { top: chk.bottom; topMargin: Theme.s4 }
                visible: s.planner.hasPlan
                Repeater {
                    model: s.planner.checks
                    Item {
                        width: parent.width; height: Theme.px(28)
                        StateIcon { level: modelData.level; anchors.verticalCenter: parent.verticalCenter }
                        Label { objectName: "planCheck"; x: Theme.px(20); width: parent.width - x; height: parent.height; text: modelData.text; font.pixelSize: Theme.small }
                    }
                }
            }
            Column {
                x: Theme.s12; width: parent.width - 2 * x
                anchors { bottom: parent.bottom; bottomMargin: Theme.s8 }
                spacing: Theme.s8
                Label { width: parent.width; text: qsTr("Saved plans are on this machine only"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                PrimaryButton { width: parent.width; text: qsTr("Put on the Dashboard"); enabled: s.planner.canPut; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.planner.put() } }
                SecondaryButton { width: parent.width; text: qsTr("Open the Dashboard"); focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.dashboardAsked() } }
            }
        }
    }

    // ---- dialogs
    Loader {
        anchors.fill: parent
        active: ["scenario", "trainee", "aircraft", "runway", "taxiway", "filter"].indexOf(s.dialog) >= 0
        sourceComponent: Component {
            PickDialog {
                readonly property var anyone: [{ id: "", text: qsTr("Anyone") }]
                readonly property var all: s.planner.people.map(x => ({ id: x, text: x }))
                title: ({ scenario: qsTr("Scenario"), trainee: qsTr("Trainee"), aircraft: qsTr("Callsign and type"), runway: qsTr("Runway"),
                          taxiway: qsTr("Taxiway"), filter: qsTr("Show") })[s.dialog] || ""
                chosenId: ({ scenario: s.p.scenarioId, trainee: s.p.traineeId, aircraft: s.p.aircraft, runway: s.p.runway, taxiway: s.p.taxiway,
                             filter: s.planner.filter })[s.dialog] || ""
                options: s.dialog === "scenario" ? s.planner.scenarios : s.dialog === "trainee" ? anyone.concat(all) : s.dialog === "aircraft" ? s.planner.aircraft
                       : s.dialog === "runway" ? s.planner.runways : s.dialog === "taxiway" ? s.planner.taxiways
                       : [{ id: "all", text: qsTr("All plans") }, { id: "anyone", text: qsTr("Plans for anyone") }].concat(all)
                onPicked: (id) => {
                    const d = s.dialog
                    s.dialog = ""
                    if (d === "scenario") s.planner.setScenario(id)
                    else if (d === "trainee") s.planner.setTrainee(id)
                    else if (d === "aircraft") s.planner.setAircraft(id)
                    else if (d === "runway") s.planner.setRunway(id)
                    else if (d === "taxiway") s.planner.setTaxiway(id)
                    else s.planner.setFilter(id)
                }
                onClosed: s.dialog = ""
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "delete"
        sourceComponent: Component {
            ConfirmDialog {
                title: qsTr("Delete this plan?")
                text: qsTr("%1. Results of its exercises are kept, with the plan's name and run number.").arg(s.p.name || "")
                confirmText: qsTr("Delete plan")
                onConfirmed: { s.dialog = ""; s.planner.deletePlan() }
                onClosed: s.dialog = ""
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "presets"
        sourceComponent: Component {
            ModalPanel {
                id: pd
                title: qsTr("Presets")
                Label { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: Theme.small; color: Theme.textSecondary
                        text: qsTr("Saved plans keep the values they were saved with and are marked \"preset changed\".") }
                Repeater {
                    model: s.planner.presets.length   // a number: the buttons stay while a value changes
                    Column {
                        id: one
                        readonly property int no: index
                        readonly property var d: s.planner.presets[index] || ({})
                        width: parent.width
                        Row {
                            spacing: Theme.s8
                            Label { width: Theme.px(64); height: Theme.touch; text: one.d.name || ""; font.weight: Font.DemiBold }
                            DelayStep { name: qsTr("%1, shortest reply delay").arg(one.d.name); value: one.d.from || ""; onStepped: (n) => s.planner.stepPreset(one.no, true, n) }
                            Label { height: Theme.touch; text: qsTr("to"); font.pixelSize: Theme.small }
                            DelayStep { name: qsTr("%1, longest reply delay").arg(one.d.name); value: one.d.to || ""; onStepped: (n) => s.planner.stepPreset(one.no, false, n) }
                        }
                        Row {
                            x: Theme.px(72)
                            spacing: Theme.s4
                            Repeater {
                                model: s.radios
                                ChoiceButton { width: Theme.px(104); font.pixelSize: Theme.small; text: modelData[1]; selected: one.d.radio === modelData[0]; onClicked: s.planner.setPresetRadio(one.no, modelData[0]) }
                            }
                        }
                    }
                }
                Row {
                    height: Theme.px(22)
                    spacing: Theme.s8
                    StateIcon { level: s.planner.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; text: s.planner.message; font.pixelSize: Theme.small }
                }
                Row {
                    spacing: Theme.s8
                    PrimaryButton { text: qsTr("Save presets"); enabled: !s.planner.busy; focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small; onClicked: s.planner.savePresets() }
                    SecondaryButton { text: qsTr("Restore starting values"); enabled: !s.planner.busy; focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small; onClicked: s.planner.startingPresets() }
                    SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small; onClicked: { s.dialog = ""; s.planner.cancelPresets() } }
                }
            }
        }
    }
}
