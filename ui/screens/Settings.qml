import QtQuick
import Atc

// Settings and trainee records (04 section 5b, D-118). Everybody may look; only the instructor changes.
Item {
    id: s
    property AccessController access
    property SettingsController settings
    property DashboardController dash
    property string selectedId
    property string dialog            // "", "add", "change" or "pilot"
    signal loginAsked()
    signal keyboardFree()             // a text field has gone: the shell takes the keyboard back
    readonly property int gap: Theme.s12
    readonly property int leftW: Math.round((width - 3 * gap) * 0.42)
    readonly property bool instructor: access.instructor
    onInstructorChanged: { if (!instructor) { dialog = ""; selectedId = "" } }
    onDialogChanged: if (dialog === "") keyboardFree()

    // One answer of a record. again: a draft answer after an approved text is in place; it counts as no (D-135).
    component Answer: Row {
        id: a
        property bool yes
        property bool again
        property string date
        spacing: Theme.s4
        StateIcon { level: !a.date ? 0 : a.yes && !a.again ? LabController.Ok : LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
        Label { height: parent.height; font.pixelSize: Theme.small
                text: !a.date ? qsTr("not asked") : a.again ? qsTr("re-consent needed") : qsTr("%1 %2").arg(a.yes ? qsTr("yes") : qsTr("no")).arg(a.date) }
    }
    Card {
        id: left
        x: s.gap; y: s.gap; width: s.leftW; height: s.height - 2 * s.gap
        title: qsTr("SETTINGS")
        Column {
            x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x
            spacing: Theme.s4
            Row {
                visible: !s.instructor
                width: parent.width; height: Theme.touch
                spacing: Theme.s8
                StateIcon { level: LabController.Caution; anchors.verticalCenter: parent.verticalCenter }
                Label { width: parent.width - in_.width - Theme.px(36); height: parent.height; text: qsTr("Only the instructor may change these."); font.pixelSize: Theme.small }
                SecondaryButton { id: in_; text: qsTr("Log in"); focusPolicy: Qt.NoFocus; onClicked: s.loginAsked() }
            }
            SwitchRow { width: parent.width; enabled: s.instructor; on: s.settings.saveRecordings
                        title: qsTr("Save Speech Lab recordings")
                        sub: qsTr("On this machine only, and only of a logged-in trainee whose consent is yes.")
                        onToggled: (on) => s.settings.setSaveRecordings(on) }
            SwitchRow { width: parent.width; enabled: s.instructor; on: s.settings.pttFallback
                        title: qsTr("Allow on-screen PTT fallback")
                        sub: qsTr("A missing PTT then only warns at Start. The result logs \"fallback PTT used\".")
                        onToggled: (on) => s.settings.setPttFallback(on) }
            Item {
                readonly property var tile: s.dash.tiles[5] || ({})
                width: parent.width; height: Theme.px(48)
                Label { height: parent.height; text: qsTr("Storage"); font.weight: Font.DemiBold }
                Row {
                    anchors.right: parent.right
                    height: parent.height
                    spacing: Theme.s8
                    StateIcon { level: parent.parent.tile.level || 0; anchors.verticalCenter: parent.verticalCenter }
                    Label { height: parent.height; mono: true; text: parent.parent.tile.line1 || "-"; font.pixelSize: Theme.small }
                }
            }
            Label { width: parent.width; text: s.settings.message; visible: text !== ""; wrapMode: Text.WordWrap; font.pixelSize: Theme.small; color: Theme.caution }
        }
    }

    Card {
        id: records
        x: left.x + left.width + s.gap; y: s.gap; width: s.width - x - s.gap; height: left.height
        title: qsTr("TRAINEE RECORDS")
        headerItems: Label { anchors.right: parent.right; height: parent.height; visible: s.instructor
                             text: qsTr("%1 records").arg(s.access.people.length); color: Theme.textSecondary; font.pixelSize: Theme.caption }
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
            id: list
            visible: s.instructor
            y: Theme.px(30); width: parent.width; height: parent.height - y - Theme.px(180)
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
                    Answer { width: parent.width * records.cols[3]; height: parent.height; yes: row.d.consent; date: row.d.date; again: row.d.again }
                    Answer { width: parent.width * records.cols[4]; height: parent.height; yes: row.d.pilot; date: row.d.pilotDate; again: row.d.pilotAgain }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                TapHandler { onTapped: s.selectedId = row.d.id }
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, recording consent %2").arg(row.d.id).arg(row.d.again ? qsTr("needed again") : row.d.consent ? qsTr("yes") : qsTr("no"))
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
    Loader {
        anchors.fill: parent
        active: s.dialog !== ""
        sourceComponent: Component {
            PersonDialog { access: s.access; changeId: s.dialog === "add" ? "" : s.selectedId; pilot: s.dialog === "pilot"; onClosed: s.dialog = "" }
        }
    }
}
