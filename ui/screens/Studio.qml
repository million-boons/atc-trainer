import QtQuick
import QtQuick.Templates as T
import Atc

// Training Content, tab Phrases: the Phraseology Studio (04 section 5, D-326). Instructor only; not during an exercise.
// Space: start/stop, Enter: accept as primary, R: re-record, P: play trimmed. A text field keeps its own keys.
Item {
    id: s
    property StudioController studio
    property string dialog            // "", session, changes, add, delete, export, unused, differing, scenario, filter
    signal loginAsked()
    signal keyboardFree()
    readonly property int gap: Theme.s8
    readonly property bool on: studio.ready
    onDialogChanged: if (dialog === "") keyboardFree()
    Component.onCompleted: { studio.enter(); fields.take() }
    Component.onDestruction: studio.leave()
    onOnChanged: if (!on) dialog = ""

    // Called by the shell for keys that no text field took.
    function handleKey(key, repeat) {
        if (!on || dialog !== "") return false
        if (key === Qt.Key_Space || key === Qt.Key_R) { if (!repeat && (key === Qt.Key_Space || !studio.recording)) studio.toggleRecord(); return true }
        if (key === Qt.Key_Return || key === Qt.Key_Enter) { if (!repeat) studio.accept(true); return true }
        if (key === Qt.Key_P) { if (!repeat) studio.play("trimmed"); return true }
        return false
    }
    function free() { edit.leave(); keyboardFree() }
    function save() { if (studio.changeRows.length > 0) dialog = "changes"; else studio.save() }

    QtObject {
        id: fields
        // The fields follow the controller only when another text was opened, never while somebody types.
        function take() { edit.text = s.studio.text; sop.text = s.studio.sopRef }
    }
    Connections {
        target: s.studio
        function onEntryLoaded() { fields.take() }
        function onAdded() { if (s.dialog === "add") s.dialog = "" }
    }
    MouseArea { anchors.fill: parent; onPressed: s.free() }

    // ---- tabs of Training Content, recording session
    Rectangle {
        id: tabs
        x: s.gap; y: s.gap; width: s.width - 2 * s.gap; height: Theme.px(56)
        color: Theme.surface
        radius: Theme.radius
        border { color: Theme.border; width: Theme.line }
        Row {
            x: Theme.s12; height: parent.height
            spacing: Theme.s8
            Label { height: parent.height; text: qsTr("TRAINING CONTENT"); color: Theme.textSecondary; font { pixelSize: Theme.caption; weight: Font.DemiBold; letterSpacing: Theme.px(1) } }
            ChoiceButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Phrases"); selected: true; font.pixelSize: Theme.small }
            ChoiceButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Scenarios: later"); enabled: false; font.pixelSize: Theme.small }
            ChoiceButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Exercises: next"); enabled: false; font.pixelSize: Theme.small }
        }
        Row {
            anchors { right: parent.right; rightMargin: Theme.s8 }
            height: parent.height
            spacing: Theme.s8
            visible: s.on
            Label { height: parent.height; text: s.studio.changedHere ? qsTr("changed on this machine") : ""; font.pixelSize: Theme.caption; color: Theme.caution }
            SecondaryButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Export..."); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                              enabled: s.studio.changedHere; onClicked: s.dialog = "export" }
            Label { height: parent.height; mono: true; font.pixelSize: Theme.small; text: s.studio.session ? qsTr("Speaker %1").arg(s.studio.speaker) : "" }
            BaseButton { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(148); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                         text: s.studio.session ? qsTr("End session") : s.studio.starting ? qsTr("Cancel start") : qsTr("Start session...")
                         tint: s.studio.session ? Theme.caution : Theme.accent
                         enabled: s.studio.session || s.studio.starting || (!s.studio.busy && !s.studio.trainerBusy)
                         onClicked: { s.free(); if (s.studio.session || s.studio.starting) s.studio.endSession(); else s.dialog = "session" } }
        }
    }

    Column {
        anchors.centerIn: parent
        visible: !s.on
        spacing: Theme.s12
        Label { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.textSecondary
                text: s.studio.instructor ? s.studio.message || qsTr("Loading") : qsTr("Instructor only. Log in to see or change phrases.") }
        SecondaryButton { anchors.horizontalCenter: parent.horizontalCenter; visible: !s.studio.instructor; text: qsTr("Log in"); focusPolicy: Qt.NoFocus; onClicked: s.loginAsked() }
    }

    Item {
        id: work
        visible: s.on
        anchors { top: tabs.bottom; topMargin: s.gap; left: parent.left; leftMargin: s.gap; right: parent.right; rightMargin: s.gap; bottom: bar.top; bottomMargin: s.gap }

        // ---- list
        Card {
            id: left_
            width: Math.round(parent.width * 0.197); height: parent.height
            Row {
                id: kinds
                property bool shown: true   // the tab the search belongs to
                x: Theme.s8; y: Theme.s8; width: parent.width - 2 * x
                spacing: Theme.s4
                ChoiceButton { width: (parent.width - Theme.s4) / 2; text: qsTr("ATC %1").arg(s.studio.intentCount); font.pixelSize: Theme.caption; selected: !s.studio.replies; onClicked: { s.free(); s.studio.showReplies(false) } }
                ChoiceButton { width: (parent.width - Theme.s4) / 2; text: qsTr("Pilot %1").arg(s.studio.replyCount); font.pixelSize: Theme.caption; selected: s.studio.replies; onClicked: { s.free(); s.studio.showReplies(true) } }
            }
            TextEntry { id: find; anchors { top: kinds.bottom; topMargin: Theme.s4 } x: Theme.s8; width: parent.width - 2 * x; label: qsTr("Search"); maximumLength: 40
                        onTextChanged: s.studio.search(text); onAccepted: s.keyboardFree()
                        Keys.onReturnPressed: {} Keys.onEnterPressed: {} }   // Enter here never accepts a take
            Connections { target: s.studio; function onListChanged() { if (kinds.shown !== s.studio.replies) { kinds.shown = s.studio.replies; find.text = "" } } }
            ListView {
                anchors { top: find.bottom; topMargin: Theme.s8; bottom: add.visible ? add.top : parent.bottom; bottomMargin: Theme.s4 }
                width: parent.width
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.studio.entries
                delegate: T.Button {
                    id: row
                    readonly property bool on: modelData.id === s.studio.entryId
                    width: ListView.view.width; height: Theme.px(50)
                    focusPolicy: Qt.NoFocus
                    onClicked: { s.free(); s.studio.select(modelData.id) }
                    Accessible.name: modelData.id
                    background: Rectangle {
                        color: row.on ? Theme.raised : "transparent"
                        Rectangle { visible: row.on; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                    contentItem: Item {
                        Label { objectName: "phraseId"; x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: Theme.px(20); text: modelData.id; mono: true; font.pixelSize: Theme.caption }
                        StateIcon { id: st; x: Theme.s12; y: Theme.px(30); level: modelData.validated ? LabController.Ok : 0 }
                        Label { x: st.visible ? st.x + st.width + Theme.s4 : Theme.s12; y: Theme.px(26); height: Theme.px(18); text: modelData.status; font.pixelSize: Theme.caption; color: Theme.textSecondary }
                        Label { anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(26); height: Theme.px(18); text: modelData.coverage; font.pixelSize: Theme.caption
                                color: modelData.complete || modelData.coverage === "" ? Theme.textSecondary : Theme.caution }
                    }
                }
            }
            SecondaryButton { id: add; visible: s.studio.replies; anchors { bottom: parent.bottom; bottomMargin: Theme.s8 } x: Theme.s8; width: parent.width - 2 * x
                              text: qsTr("+ Add reply"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.dialog = "add" } }
        }

        // ---- editor
        Card {
            id: mid
            x: left_.width + s.gap; width: parent.width - x - s.gap - rec.width; height: parent.height
            title: s.studio.entryId || qsTr("PHRASE")
            headerItems: Label { anchors.right: parent.right; height: parent.height; color: Theme.textSecondary; font.pixelSize: Theme.caption
                                 text: s.studio.replies ? qsTr("pilot reply") : qsTr("ATC wording") }
            Item {
                x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: parent.height - 2 * y
                Row {
                    id: status
                    width: parent.width; height: Theme.px(70)
                    spacing: Theme.s8
                    ChoiceButton { anchors.bottom: parent.bottom; width: Theme.px(92); text: qsTr("Draft"); selected: !s.studio.validated; onClicked: { s.free(); s.studio.setValidated(false) } }
                    ChoiceButton { anchors.bottom: parent.bottom; width: Theme.px(120); text: qsTr("Validated"); selected: s.studio.validated; onClicked: { s.free(); s.studio.setValidated(true) } }
                    TextEntry { id: sop; anchors.bottom: parent.bottom; width: parent.width - x; label: qsTr("SOP reference (required for Validated)"); maximumLength: 80
                                onTextChanged: s.studio.setSopRef(text); onAccepted: s.keyboardFree()
                                Keys.onReturnPressed: {} Keys.onEnterPressed: {} }
                }
                Label { id: used; anchors.top: status.bottom; width: parent.width; height: Theme.px(24); font.pixelSize: Theme.caption; color: Theme.textSecondary
                        text: qsTr("Used by: %1").arg(s.studio.usedBy) + (s.studio.statusNote ? "   " + s.studio.statusNote : "") }
                PhraseEdit { id: edit; anchors.top: used.bottom; width: parent.width; height: Theme.px(64); onEdited: (t) => s.studio.setText(t); onLeaving: s.free() }
                SegmentStrip { id: strip; anchors { top: edit.bottom; topMargin: Theme.s8 } width: parent.width; height: Theme.px(112); studio: s.studio; onTouched: s.free() }
                Row {
                    id: lintRow
                    anchors.top: strip.bottom
                    height: Theme.px(24)
                    spacing: Theme.s4
                    StateIcon { level: s.studio.lint.length > 0 ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; font.pixelSize: Theme.caption; text: s.studio.lint.length > 0 ? s.studio.lint.join("; ") : qsTr("\",\" = 180 ms pause, no clip"); color: s.studio.lint.length > 0 ? Theme.textPrimary : Theme.textMuted }
                }
                Rectangle { id: line1; anchors { top: lintRow.bottom; topMargin: Theme.s4 } width: parent.width; height: Theme.line; color: Theme.divider }
                // Reply: the values of the pool. ATC intent: its examples.
                Label { id: poolT; anchors { top: line1.bottom; topMargin: Theme.s4 } width: parent.width; height: Theme.px(20); color: Theme.textSecondary; font.pixelSize: Theme.caption
                        text: !s.studio.replies ? qsTr("Examples do not change recognition on this machine") : s.studio.poolTitle || qsTr("Static words: one clip") }
                Flickable {
                    anchors { top: poolT.bottom; bottom: line2.top; bottomMargin: Theme.s4 }
                    width: parent.width
                    contentHeight: pool.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Flow {
                        id: pool
                        width: parent.width
                        spacing: Theme.s8
                        Repeater {
                            model: s.studio.replies ? s.studio.poolClips : []
                            BaseButton { id: pb; width: Theme.px(224); text: modelData.id; font { family: Theme.mono; pixelSize: Theme.caption } focusPolicy: Qt.NoFocus
                                         tint: modelData.selected ? Theme.accent : Theme.border; rightPadding: Theme.px(24)
                                         onClicked: { s.free(); s.studio.selectClip(modelData.id) }
                                         ClipDot { state_: modelData.state; anchors { right: parent.right; rightMargin: Theme.s4; verticalCenter: parent.verticalCenter } }
                                         Rectangle { visible: modelData.selected; anchors.bottom: parent.bottom; width: parent.width; height: 3 * Theme.line; color: Theme.accent } }
                        }
                        Repeater {
                            model: s.studio.replies ? 0 : s.studio.exampleCount
                            ChoiceButton { text: index + 1; selected: index === s.studio.example; onClicked: { s.free(); s.studio.selectExample(index) } }
                        }
                        SecondaryButton { visible: !s.studio.replies; text: qsTr("+ Example"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.studio.addExample() } }
                    }
                }
                Rectangle { id: line2; anchors { bottom: saveRow.top; bottomMargin: Theme.s8 } width: parent.width; height: Theme.line; color: Theme.divider }
                Item {
                    id: saveRow
                    anchors.bottom: parent.bottom
                    width: parent.width; height: Theme.touch
                    Row {
                        anchors.right: parent.right
                        spacing: Theme.s8
                        id: saveBtns
                        PrimaryButton { text: qsTr("Save"); enabled: s.studio.canSave; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.save() } }
                        SecondaryButton { text: qsTr("Undo"); enabled: s.studio.dirty && !s.studio.busy; focusPolicy: Qt.NoFocus; onClicked: { s.free(); s.studio.undo() } }
                    }
                    Column {
                        width: parent.width - saveBtns.width - Theme.s8
                        anchors.verticalCenter: parent.verticalCenter
                        Label { width: parent.width; text: s.studio.changeText; font.pixelSize: Theme.small; color: s.studio.dirty ? Theme.textPrimary : Theme.textSecondary }
                        Row {
                            width: parent.width; height: Theme.px(20)
                            spacing: Theme.s4
                            StateIcon { level: s.studio.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                            Label { objectName: "studioMessage"; height: parent.height; width: parent.width - Theme.px(18); text: s.studio.message || (s.studio.starting ? qsTr("Measuring the room tone: please be quiet") : s.studio.notice); font.pixelSize: Theme.caption }
                        }
                    }
                }
            }
        }

        RecorderPanel {
            id: rec
            anchors.right: parent.right
            width: Math.round(parent.width * 0.265); height: parent.height
            studio: s.studio
            onTouched: s.free()
            onDeleteAsked: s.dialog = "delete"
        }
    }
    CoverageBar {
        id: bar
        visible: s.on
        x: s.gap; width: s.width - 2 * s.gap; height: Theme.px(64)
        anchors { bottom: parent.bottom; bottomMargin: s.gap }
        studio: s.studio
        onTouched: s.free()
        onScenarioAsked: s.dialog = "scenario"
        onFilterAsked: s.dialog = "filter"
        onUnusedAsked: s.dialog = "unused"
        onDifferingAsked: s.dialog = "differing"
    }

    // ---- dialogs
    component Pick: ModalPanel {
        id: pick
        property var options: []      // {id, text, note, off}
        property string chosenId
        signal picked(string id)
        signal closed()
        ListView {
            width: parent.width; height: Math.min(contentHeight, Theme.px(320))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.s4
            model: pick.options
            delegate: ChoiceButton { width: ListView.view.width; text: modelData.note ? modelData.text + "   " + modelData.note : modelData.text
                                     enabled: !modelData.off; selected: modelData.id === pick.chosenId; onClicked: pick.picked(modelData.id) }
        }
        SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: pick.closed() }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "session"
        sourceComponent: Component {
            Pick { title: qsTr("Who records?")
                   options: s.studio.speakers.map(p => ({ id: p.id, text: p.id, note: p.why, off: !p.may }))
                   onPicked: (id) => { s.dialog = ""; s.studio.startSession(id) }
                   onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "scenario"
        sourceComponent: Component {
            Pick { title: qsTr("Coverage of"); chosenId: s.studio.scenario
                   options: s.studio.scenarios.map(x => ({ id: x, text: x }))
                   onPicked: (id) => { s.dialog = ""; s.studio.setScenario(id) }
                   onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "filter"
        sourceComponent: Component {
            Pick { title: qsTr("Missing clips of"); chosenId: s.studio.filter
                   options: ["all", "this", "pools", "initial", "medial", "final"].map(x => ({ id: x, text: bar.filterNames[x] }))
                   onPicked: (id) => { s.dialog = ""; s.studio.setFilter(id) }
                   onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "changes" || s.dialog === "unused" || s.dialog === "differing" || s.dialog === "delete"
        sourceComponent: Component {
            ConfirmDialog {
                title: s.dialog === "changes" ? qsTr("Save changed wording?") : s.dialog === "delete" ? qsTr("Delete this take?") : s.dialog === "differing" ? qsTr("Words differ") : qsTr("Unused clips")
                text: s.dialog === "changes" ? qsTr("These clips change. A missing clip blocks Start until it is recorded.")
                    : s.dialog === "delete" ? qsTr("The file is deleted from this machine. The deletion is logged.")
                    : s.dialog === "differing" ? qsTr("These clips say other words than the text. They block Start until they are recorded again.")
                    : qsTr("No reply of %1 uses these clips. They stay in the bank.").arg(s.studio.scenario)
                rows: s.dialog === "changes" ? s.studio.changeRows : s.dialog === "unused" ? s.studio.unused : s.dialog === "differing" ? s.studio.differing : []
                confirmText: s.dialog === "changes" ? qsTr("Save wording") : s.dialog === "delete" ? qsTr("Delete take") : ""
                closeText: s.dialog === "unused" || s.dialog === "differing" ? qsTr("Close") : qsTr("Cancel")
                onConfirmed: { const d = s.dialog; s.dialog = ""; if (d === "changes") s.studio.save(); else s.studio.deleteTake() }
                onClosed: s.dialog = ""
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "add" || s.dialog === "export"
        sourceComponent: Component {
            ModalPanel {
                id: form
                readonly property bool add: s.dialog === "add"
                title: add ? qsTr("Add a pilot reply") : qsTr("Export changes")
                Component.onCompleted: { s.studio.clearMessage(); if (add) newId.take(); else file.take() }
                TextEntry { id: newId; visible: form.add; width: parent.width; label: qsTr("ID (required)"); mono: true; maximumLength: 40
                            validator: RegularExpressionValidator { regularExpression: /[A-Z0-9_]*/ } }
                TextEntry { id: newText; visible: form.add; width: parent.width; label: qsTr("Text, with {slot}"); maximumLength: 200 }
                TextEntry { id: file; visible: !form.add; width: parent.width; label: qsTr("File on this machine"); mono: true; maximumLength: 300 }
                Label { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: Theme.small; color: Theme.textSecondary
                        text: form.add ? qsTr("A new reply is a Draft. A scenario must name it before the pilot can say it.")
                                       : qsTr("Texts only, no audio. The changes are for review into the project by the user.") }
                Row {
                    height: Theme.px(22)
                    spacing: Theme.s8
                    StateIcon { level: s.studio.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; text: s.studio.message; font.pixelSize: Theme.small }
                }
                Row {
                    spacing: Theme.s12
                    PrimaryButton { text: form.add ? qsTr("Add") : qsTr("Export"); focusPolicy: Qt.NoFocus; enabled: !s.studio.busy && (form.add ? newId.text !== "" && newText.text !== "" : file.text !== "")
                                    onClicked: form.add ? s.studio.addReply(newId.text, newText.text) : s.studio.exportChanges(file.text) }
                    SecondaryButton { text: qsTr("Close"); focusPolicy: Qt.NoFocus; onClicked: s.dialog = "" }
                }
            }
        }
    }
}
