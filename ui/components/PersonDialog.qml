import QtQuick
import Atc

// Adds a trainee record, or changes the consent of one (changeId not empty). Instructor only (04 section 5b).
ModalPanel {
    id: d
    property AccessController access
    property string changeId
    readonly property bool adding: changeId === ""
    property int consent: -1      // -1: not answered yet; neither answer is preselected
    signal closed()
    title: adding ? qsTr("Add a record") : qsTr("Consent of %1").arg(changeId)
    Component.onCompleted: { access.clearMessage(); date.text = access.today; if (adding) code.take(); else date.take() }
    Row {
        visible: d.adding
        width: parent.width
        spacing: Theme.s12
        TextEntry { id: code; width: (parent.width - 2 * parent.spacing) * 0.28; label: qsTr("ID (required)"); mono: true; maximumLength: 16
                    validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9_-]*/ } }
        TextEntry { id: name; width: (parent.width - 2 * parent.spacing) * 0.44; label: qsTr("Name (optional)"); maximumLength: 60 }
        TextEntry { id: rank; width: (parent.width - 2 * parent.spacing) * 0.28; label: qsTr("Rank (optional)"); maximumLength: 60 }
    }
    Label { text: qsTr("Consent to be recorded (required)"); font.weight: Font.DemiBold }
    Label { width: parent.width; text: d.access.consentText; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.small }
    Row {
        spacing: Theme.s12
        ChoiceButton { anchors.bottom: parent.bottom; text: qsTr("Yes"); selected: d.consent === 1; onClicked: d.consent = 1 }
        ChoiceButton { anchors.bottom: parent.bottom; text: qsTr("No"); selected: d.consent === 0; onClicked: d.consent = 0 }
        TextEntry { id: date; width: Theme.px(180); label: qsTr("Date of the answer"); mono: true; maximumLength: 10
                    validator: RegularExpressionValidator { regularExpression: /[0-9-]*/ } }
    }
    Row {
        spacing: Theme.s8
        height: Theme.px(22)
        StateIcon { level: d.access.message ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
        Label { height: parent.height; text: d.access.message; font.pixelSize: Theme.small }
    }
    Row {
        spacing: Theme.s12
        PrimaryButton {
            text: d.adding ? qsTr("Add") : qsTr("Change consent")
            focusPolicy: Qt.NoFocus
            enabled: d.consent >= 0 && date.text.length === 10 && (!d.adding || code.text !== "")
            onClicked: if (d.adding ? d.access.addPerson(code.text, name.text, rank.text, d.consent === 1, date.text)
                                    : d.access.setConsent(d.changeId, d.consent === 1, date.text)) d.closed()
        }
        SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: d.closed() }
    }
}
