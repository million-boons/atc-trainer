import QtQuick
import Atc

// Re-consent at login (D-137, D-139): the new text, Yes or No, stored only with the instructor's PIN. One question at a
// time. Keypad only, so that Space stays PTT. PTT keeps working behind it.
ModalPanel {
    id: d
    property AccessController access
    readonly property var item: access.reconsent[0] || ({})
    property int answer: -1       // neither answer is preselected
    property string pin
    title: qsTr("Consent: please answer again")
    onItemChanged: { answer = -1; pin = "" }
    Component.onCompleted: access.clearMessage()
    Label { width: parent.width; text: qsTr("%1   %2").arg(d.access.personId).arg(d.item.title || ""); font.weight: Font.DemiBold }
    Label { objectName: "reconsentText"; width: parent.width; text: d.item.text || ""; wrapMode: Text.WordWrap; font.pixelSize: Theme.small }
    Row {
        spacing: Theme.s12
        ChoiceButton { text: qsTr("Yes"); selected: d.answer === 1; enabled: !d.access.busy; onClicked: d.answer = 1 }
        ChoiceButton { text: qsTr("No"); selected: d.answer === 0; enabled: !d.access.busy; onClicked: d.answer = 0 }
    }
    Label { width: parent.width; text: qsTr("The answer is stored only with the instructor's PIN, with the instructor present."); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.small }
    Row {
        spacing: Theme.s16
        KeyPad {
            id: pad
            enabled: !d.access.busy
            onKey: (digit) => { if (d.pin.length < 12) d.pin += digit }
            onErase: d.pin = d.pin.slice(0, -1)
            onClear: d.pin = ""
        }
        Column {
            spacing: Theme.s12
            width: d.width > 0 ? Theme.px(220) : 0
            Label { width: parent.width; height: Theme.px(28); mono: d.pin !== ""; text: d.pin ? "•".repeat(d.pin.length) : qsTr("PIN, 6 to 12 digits"); color: d.pin ? Theme.textPrimary : Theme.textSecondary }
            PrimaryButton { width: parent.width; text: qsTr("Store answer"); focusPolicy: Qt.NoFocus; enabled: d.answer >= 0 && d.pin.length >= 6 && !d.access.busy
                            onClicked: { d.access.answerReconsent(d.item.kind, d.answer === 1, d.pin); d.pin = "" } }
            SecondaryButton { width: parent.width; text: qsTr("Later"); focusPolicy: Qt.NoFocus; enabled: !d.access.busy; onClicked: d.access.reconsentLater() }
            Label { objectName: "laterNote"; width: parent.width; text: d.item.later || ""; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.caption }
        }
    }
    Row {
        spacing: Theme.s8
        width: parent.width; height: Theme.px(22)
        StateIcon { level: d.access.message ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
        Label { height: parent.height; text: d.access.busy ? qsTr("Checking the PIN") : d.access.message; font.pixelSize: Theme.small }
    }
}
