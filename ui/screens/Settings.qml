import QtQuick
import QtQuick.Templates as T
import Atc

// Settings, full (04 sections 5b and 5f). Left: pages, right: the page. Everybody may look; only the instructor
// changes, except the General page. setup null: the pages that need it say so. View only.
Item {
    id: s
    property AccessController access
    property SettingsController settings
    property DashboardController dash
    property SetupController setup: null
    property string page: "recordings"
    property string selectedId
    property string dialog            // "", add, change, pilot, choice, learn, pin, guests
    property string choiceKey
    property int guestCount: 0
    signal loginAsked()
    signal keyboardFree()             // a text field has gone: the shell takes the keyboard back
    readonly property int gap: Theme.s12
    readonly property bool instructor: access.instructor
    readonly property bool full: setup !== null
    readonly property var numbers: full ? setup.numbers[page] || [] : []
    readonly property var choices: full ? setup.choices[page] || [] : []
    readonly property var facts: full ? setup.facts[page] || [] : []
    readonly property var pages: [["general", qsTr("General")], ["audio", qsTr("Audio")], ["ptt", qsTr("PTT")], ["speech", qsTr("Speech")], ["voice", qsTr("Voice bank")],
                                  ["recordings", qsTr("Recordings")], ["records", qsTr("Trainee records")], ["instructor", qsTr("Instructor")], ["about", qsTr("About")]]
    readonly property string name: (pages.find(p => p[0] === page) || ["", ""])[1]
    onInstructorChanged: { if (!instructor) { dialog = ""; selectedId = "" } }
    onDialogChanged: if (dialog === "") keyboardFree()
    onPageChanged: { selectedId = ""; if (full) { setup.leave(); setup.clearMessage() } }
    Component.onCompleted: if (full) setup.enter()
    Component.onDestruction: if (full) setup.leave()

    // One answer of a record. again: counts as no until it is given again (D-135). ask: only asked at the next login (D-137).
    component Answer: Row {
        id: a
        property bool yes
        property bool again
        property bool ask: false
        property string date
        spacing: Theme.s4
        StateIcon { level: !a.date ? 0 : a.yes && !a.again && !a.ask ? LabController.Ok : LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
        Label { height: parent.height; font.pixelSize: Theme.small
                text: !a.date ? qsTr("not asked") : a.again ? qsTr("re-consent needed") : a.ask ? qsTr("%1 %2, ask again").arg(a.yes ? qsTr("yes") : qsTr("no")).arg(a.date) : qsTr("%1 %2").arg(a.yes ? qsTr("yes") : qsTr("no")).arg(a.date) }
    }

    // ---- pages
    Card {
        id: left
        x: s.gap; y: s.gap; width: Theme.px(212); height: s.height - 2 * s.gap
        title: qsTr("SETTINGS")
        Column {
            width: parent.width
            Repeater {
                model: s.pages
                T.Button {
                    id: pb
                    readonly property bool on: modelData[0] === s.page
                    width: left.width - 2 * Theme.line; height: Theme.touch
                    focusPolicy: Qt.NoFocus
                    text: modelData[1]
                    onClicked: s.page = modelData[0]
                    background: Rectangle {
                        color: pb.on ? Theme.raised : "transparent"
                        Rectangle { visible: pb.on; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                    contentItem: Label { leftPadding: Theme.s12; text: pb.text; font.weight: pb.on ? Font.DemiBold : Font.Normal }
                }
            }
        }
        Column {
            x: Theme.s12; width: parent.width - 2 * x
            anchors { bottom: parent.bottom; bottomMargin: Theme.s12 }
            visible: !s.instructor
            spacing: Theme.s8
            Row {
                spacing: Theme.s4
                StateIcon { level: LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
                Label { width: left.width - Theme.px(48); text: qsTr("Only the instructor may change these, except General."); wrapMode: Text.WordWrap; font.pixelSize: Theme.caption }
            }
            SecondaryButton { width: parent.width; text: qsTr("Log in"); focusPolicy: Qt.NoFocus; onClicked: s.loginAsked() }
        }
    }

    Card {
        id: body
        x: left.x + left.width + s.gap; y: s.gap; width: s.width - x - s.gap; height: left.height
        title: s.name.toUpperCase()
        headerItems: Label { anchors.right: parent.right; height: parent.height; color: Theme.textSecondary; font.pixelSize: Theme.caption
                             text: s.page === "records" && s.instructor ? qsTr("%1 records").arg(s.access.people.length) : s.page === "general" || s.page === "about" ? "" : qsTr("instructor") }
        Label { anchors.centerIn: parent; color: Theme.textSecondary
                visible: !s.full && s.page !== "recordings" && s.page !== "records" && s.page !== "instructor"
                text: qsTr("Not built: this page needs its backend.") }

        // ---- every page but the records
        Item {
            id: pane
            visible: s.page !== "records"
            x: Theme.s16; y: Theme.s4; width: parent.width - 2 * x; height: foot.y - y
            Column {
                id: upper
                width: parent.width
                // Tests and live state come from the Dashboard's tiles.
                Repeater {
                    model: s.page === "audio" ? [0, 1] : s.page === "ptt" ? [2] : s.page === "speech" ? [3] : s.page === "voice" ? [4] : s.page === "recordings" ? [5] : []
                    Item {
                        id: tl
                        readonly property var tile: s.dash.tiles[modelData] || ({})
                        readonly property int no: modelData
                        width: upper.width; height: Theme.px(56)
                        StateIcon { id: ti; level: tl.tile.level || 0; anchors.verticalCenter: parent.verticalCenter }
                        Label { x: Theme.px(22); y: Theme.px(6); width: parent.width - x - tb.width - Theme.s8; height: Theme.px(22); text: qsTr("%1: %2").arg(tl.tile.title || "").arg(tl.tile.state || ""); font { pixelSize: Theme.small; weight: Font.DemiBold } }
                        Label { x: Theme.px(22); y: Theme.px(28); width: parent.width - x - tb.width - Theme.s8; height: Theme.px(20); color: Theme.textSecondary; font.pixelSize: Theme.caption
                                text: [tl.tile.line1, (tl.tile.line2 || "").replace("\n", "   ")].filter(x => x).join("   ") }
                        Row {
                            id: tb
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            spacing: Theme.s8
                            SecondaryButton { visible: tl.no < 3; text: tl.tile.action || ""; enabled: tl.tile.actionEnabled === true; font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: s.dash.tileAction(tl.no) }
                            SecondaryButton { visible: tl.no === 1; text: tl.tile.action2 || ""; enabled: tl.tile.action2Enabled === true; font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: s.dash.toneHeard() }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                }
                Label { visible: s.page === "audio"; width: parent.width; height: Theme.px(28); text: qsTr("Speak in the band −20 to −6 dBFS, about −12 dBFS."); color: Theme.textSecondary; font.pixelSize: Theme.small }
                SwitchRow { visible: s.page === "recordings"; width: parent.width; enabled: s.instructor; on: s.settings.saveRecordings
                            title: qsTr("Save Speech Lab recordings")
                            sub: qsTr("On this machine only, and only of a logged-in trainee whose consent is yes.")
                            onToggled: (on) => s.settings.setSaveRecordings(on) }
                SwitchRow { visible: s.page === "ptt" || (!s.full && s.page === "recordings"); width: parent.width; enabled: s.instructor; on: s.settings.pttFallback
                            title: qsTr("Allow on-screen PTT fallback")
                            sub: qsTr("A missing PTT then only warns at Start. The result logs \"fallback PTT used\".")
                            onToggled: (on) => s.settings.setPttFallback(on) }
                Repeater {
                    model: s.choices.length
                    SetupChoiceRow { width: upper.width; info: s.choices[index] || ({}); onAsked: { s.choiceKey = info.key; s.dialog = "choice" } }
                }
                // Two columns: the PTT page has ten numbers and fits without scrolling.
                Grid {
                    id: grid
                    width: parent.width
                    columns: s.numbers.length > 4 ? 2 : 1
                    columnSpacing: Theme.s24
                    Repeater {
                        model: s.numbers.length
                        SetupNumberRow { width: (grid.width - (grid.columns - 1) * Theme.s24) / grid.columns; info: s.numbers[index] || ({}); may: s.instructor && !s.setup.busy
                                         onStepped: (n) => s.setup.stepNumber(info.key, n) }
                    }
                }
                Repeater {
                    model: s.facts
                    Item {
                        width: upper.width; height: Theme.px(36)
                        Label { width: Theme.px(150); height: parent.height; text: modelData.name; color: Theme.textSecondary; font.pixelSize: Theme.small }
                        Label { x: Theme.px(150); width: parent.width - x; height: parent.height; text: modelData.value; mono: true; font.pixelSize: Theme.small; wrapMode: Text.WrapAnywhere; maximumLineCount: 2 }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    }
                }
                // Guest results: a count, never a list (D-131).
                Item {
                    visible: s.full && s.page === "recordings"
                    width: parent.width; height: Theme.px(64)
                    Label { y: Theme.px(8); height: Theme.px(24); text: s.full ? qsTr("Guest results: %1").arg(s.setup.guests) : ""; font { pixelSize: Theme.small; weight: Font.DemiBold } }
                    Label { y: Theme.px(34); height: Theme.px(20); text: qsTr("Only their number is shown. They are in no history."); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    DangerButton { anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: qsTr("Delete guest results"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                                   enabled: s.instructor && s.full && s.setup.guests > 0 && !s.setup.busy; onClicked: { s.guestCount = s.setup.guests; s.dialog = "guests" } }
                }
                Label { visible: s.page === "recordings"; width: parent.width; height: Theme.px(28); text: qsTr("Stored recordings are listed in the Speech Lab."); color: Theme.textSecondary; font.pixelSize: Theme.small }
                Item {
                    visible: s.page === "instructor"
                    width: parent.width; height: Theme.px(64)
                    StateIcon { id: pi; level: s.access.pinSet ? LabController.Ok : LabController.Caution; y: Theme.px(14) }
                    Label { x: Theme.px(22); y: Theme.px(8); height: Theme.px(24); text: s.access.pinSet ? qsTr("PIN: set") : qsTr("PIN: not set (set_instructor_pin)"); font { pixelSize: Theme.small; weight: Font.DemiBold } }
                    Label { x: Theme.px(22); y: Theme.px(34); height: Theme.px(20); text: qsTr("Stop of an assessed exercise always needs the PIN."); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                    SecondaryButton { visible: s.full; anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: qsTr("Change PIN..."); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                                      enabled: s.instructor && s.access.pinSet && !s.setup.busy; onClicked: s.dialog = "pin" }
                }
            }
        }
        Item {
            id: foot
            visible: s.page !== "records"
            x: Theme.s16; width: parent.width - 2 * x; height: Theme.px(56)
            anchors { bottom: parent.bottom; bottomMargin: Theme.s8 }
            readonly property string note: s.full && s.setup.message ? s.setup.message : s.settings.message
            Row {
                anchors { left: parent.left; right: footB.left; verticalCenter: parent.verticalCenter }
                height: Theme.px(24)
                spacing: Theme.s4
                StateIcon { level: s.full && s.setup.message ? s.setup.messageLevel : foot.note ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
                Label { objectName: "setupMessage"; width: parent.width - Theme.px(20); height: parent.height; text: foot.note; font.pixelSize: Theme.small }
            }
            Row {
                id: footB
                anchors { right: parent.right; bottom: parent.bottom }
                spacing: Theme.s8
                SecondaryButton { visible: s.full && s.page === "ptt"; text: qsTr("Learn pedal..."); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.instructor && !s.setup.busy; onClicked: { s.dialog = "learn"; s.setup.startLearn() } }
                SecondaryButton { visible: s.full && s.page === "voice"; text: qsTr("Test sentence"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.instructor && !s.setup.busy; onClicked: s.setup.testSentence() }
                SecondaryButton { visible: s.full && (s.numbers.length > 0 || s.choices.length > 0); text: qsTr("Restore starting values"); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; enabled: s.instructor && !s.setup.busy; onClicked: s.setup.restore(s.page) }
            }
        }

        // ---- trainee records (5b)
        Item {
            id: records
            visible: s.page === "records"
            anchors.fill: parent
            readonly property var cols: [0.15, 0.21, 0.14, 0.25, 0.25]
            Label { anchors.centerIn: parent; visible: !s.instructor; text: qsTr("Instructor only. Log in to see or change records."); color: Theme.textSecondary }
            Row {
                visible: s.instructor
                x: Theme.s12; width: parent.width - 2 * x; height: Theme.px(30)
                Repeater {
                    model: [qsTr("ID"), qsTr("Name"), qsTr("Rank"), qsTr("Recording consent"), qsTr("Pilot voice")]
                    Label { width: parent.width * records.cols[index]; height: parent.height; text: modelData; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                }
            }
            ListView {
                id: people
                visible: s.instructor
                y: Theme.px(30); width: parent.width; height: marks.y - y
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.instructor ? s.access.people : []
                delegate: Rectangle {
                    id: row
                    readonly property var d: modelData
                    readonly property bool on: d.id === s.selectedId
                    width: ListView.view.width; height: Theme.touch
                    color: on ? Theme.raised : "transparent"
                    Rectangle { visible: row.on; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                    Row {
                        x: Theme.s12; width: parent.width - 2 * x; height: parent.height
                        Label { width: parent.width * records.cols[0]; height: parent.height; mono: true; text: row.d.id; font.weight: Font.DemiBold }
                        Label { width: parent.width * records.cols[1]; height: parent.height; text: row.d.name || "-"; font.pixelSize: Theme.small }
                        Label { width: parent.width * records.cols[2]; height: parent.height; text: row.d.rank || "-"; font.pixelSize: Theme.small }
                        Answer { width: parent.width * records.cols[3]; height: parent.height; yes: row.d.consent; date: row.d.date; again: row.d.again; ask: row.d.ask === true }
                        Answer { width: parent.width * records.cols[4]; height: parent.height; yes: row.d.pilot; date: row.d.pilotDate; again: row.d.pilotAgain; ask: row.d.pilotAsk === true }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                    TapHandler { onTapped: s.selectedId = row.d.id }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("%1, recording consent %2").arg(row.d.id).arg(row.d.again ? qsTr("needed again") : row.d.consent ? qsTr("yes") : qsTr("no"))
                }
            }
            // "Wider use" of a text that replaced an approved one (D-137). Not shown while the texts are drafts.
            Column {
                id: marks
                visible: s.instructor
                x: Theme.s12; width: parent.width - 2 * x
                y: parent.height - Theme.px(180) - height
                Repeater {
                    model: s.instructor ? s.access.consentMarks : []
                    Item {
                        id: mk
                        readonly property var d: modelData
                        width: marks.width; height: Theme.px(56)
                        Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
                        Label { y: Theme.px(6); width: parent.width - yn.width - Theme.s8; height: Theme.px(22); text: mk.d.title; font { pixelSize: Theme.small; weight: Font.DemiBold } }
                        Label { objectName: "markNote"; y: Theme.px(28); width: parent.width - yn.width - Theme.s8; height: Theme.px(22); text: mk.d.note; color: Theme.textSecondary; font.pixelSize: Theme.caption }
                        Row {
                            id: yn
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            spacing: Theme.s8
                            ChoiceButton { text: qsTr("Wider use"); selected: mk.d.mark === 1; onClicked: s.access.markWider(mk.d.kind, true) }
                            ChoiceButton { text: qsTr("Wording only"); selected: mk.d.mark === 0; enabled: mk.d.mark !== 1; onClicked: s.access.markWider(mk.d.kind, false) }
                        }
                    }
                }
            }
            Label { visible: s.instructor; x: Theme.s12; y: parent.height - Theme.px(172); width: parent.width - 2 * x; height: Theme.px(100)
                    objectName: "consentTexts"; text: s.access.consentText + "\n" + s.access.pilotVoiceText; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.small }
            Row {
                visible: s.instructor
                anchors { right: parent.right; rightMargin: Theme.s12; bottom: parent.bottom; bottomMargin: Theme.s12 }
                spacing: Theme.s12
                SecondaryButton { text: qsTr("Pilot voice"); focusPolicy: Qt.NoFocus; enabled: s.selectedId !== ""; onClicked: s.dialog = "pilot" }
                SecondaryButton { text: qsTr("Change consent"); focusPolicy: Qt.NoFocus; enabled: s.selectedId !== ""; onClicked: s.dialog = "change" }
                PrimaryButton { text: qsTr("Add"); width: Theme.px(96); focusPolicy: Qt.NoFocus; onClicked: s.dialog = "add" }
            }
        }
    }

    // ---- dialogs
    Loader {
        anchors.fill: parent
        active: ["add", "change", "pilot"].indexOf(s.dialog) >= 0
        sourceComponent: Component {
            PersonDialog { access: s.access; changeId: s.dialog === "add" ? "" : s.selectedId; pilot: s.dialog === "pilot"; onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "choice"
        sourceComponent: Component {
            PickDialog {
                readonly property var c: s.choices.find(x => x.key === s.choiceKey) || ({})
                title: c.title || ""
                chosenId: c.value || ""
                options: c.options || []
                onPicked: (id) => { s.dialog = ""; s.setup.choose(s.choiceKey, id) }
                onClosed: s.dialog = ""
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "guests"
        sourceComponent: Component {
            ConfirmDialog { title: qsTr("Delete %1 guest results?").arg(s.guestCount); text: qsTr("All results of exercises without a login are deleted from this machine. Each deletion is logged.")
                            confirmText: qsTr("Delete %1 results").arg(s.guestCount); onConfirmed: { s.dialog = ""; s.setup.deleteGuests(s.guestCount) } onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "learn"
        sourceComponent: Component {
            ModalPanel {
                title: qsTr("Learn pedal")
                Component.onDestruction: s.setup.stopLearn()
                Label { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Press the pedal, hold it for 2 s and release it. Nothing is transmitted while this is open.") }
                ListView {
                    width: parent.width; height: Math.max(Theme.px(80), Math.min(contentHeight, Theme.px(260)))
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: Theme.s8
                    model: s.setup.presses
                    delegate: Item {
                        width: ListView.view.width; height: Theme.px(64)
                        Label { y: Theme.px(2); width: parent.width - take.width - Theme.s8; height: Theme.px(22); text: modelData.name + "   " + modelData.info; font.pixelSize: Theme.small }
                        StateIcon { y: Theme.px(32); level: modelData.level }
                        Label { x: Theme.px(20); y: Theme.px(26); width: parent.width - x - take.width - Theme.s8; height: Theme.px(24); text: modelData.state; font.pixelSize: Theme.small }
                        SecondaryButton { id: take; anchors.right: parent.right; text: qsTr("Use"); width: Theme.px(92); enabled: modelData.usable; focusPolicy: Qt.NoFocus; onClicked: { s.setup.takePedal(modelData.id); s.dialog = "" } }
                    }
                }
                Label { visible: s.setup.presses.length === 0; text: s.setup.message || qsTr("No press seen yet."); color: Theme.textSecondary }
                SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: s.dialog = "" }
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "pin"
        sourceComponent: Component {
            ModalPanel {
                id: pd
                title: qsTr("Change the instructor PIN")
                Component.onCompleted: { s.setup.clearMessage(); p0.take() }
                Connections { target: s.setup; function onPinChanged() { s.dialog = "" } }
                TextEntry { id: p0; width: parent.width; label: qsTr("Old PIN"); secret: true; maximumLength: 12; validator: RegularExpressionValidator { regularExpression: /[0-9]*/ } }
                TextEntry { id: p1; width: parent.width; label: qsTr("New PIN (6 to 12 digits)"); secret: true; maximumLength: 12; validator: RegularExpressionValidator { regularExpression: /[0-9]*/ } }
                TextEntry { id: p2; width: parent.width; label: qsTr("New PIN again"); secret: true; maximumLength: 12; validator: RegularExpressionValidator { regularExpression: /[0-9]*/ } }
                Row {
                    width: parent.width; height: Theme.px(22)
                    spacing: Theme.s8
                    StateIcon { level: s.setup.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; text: s.setup.message; font.pixelSize: Theme.small }
                }
                Row {
                    spacing: Theme.s12
                    PrimaryButton { text: qsTr("Change"); focusPolicy: Qt.NoFocus; enabled: !s.setup.busy && p0.text.length >= 6 && p1.text.length >= 6 && p2.text.length >= 6
                                    onClicked: { s.setup.changePin(p0.text, p1.text, p2.text); p0.text = ""; p1.text = ""; p2.text = "" } }
                    SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: s.dialog = "" }
                }
            }
        }
    }
}
