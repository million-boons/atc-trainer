import QtQuick
import Atc

// Login (04 section 5b). closed(): the dialog is to be removed. PTT keeps working behind it.
ModalPanel {
    id: d
    property AccessController access
    property bool instructor: false
    property string chosen: access.personId
    signal closed()
    title: qsTr("Log in")
    Component.onCompleted: access.clearMessage()
    Connections { target: d.access; function onLoggedIn() { d.closed() } }
    function go() {
        if (instructor) { access.loginInstructor(pin.text); pin.text = "" }
        else access.loginTrainee(chosen)
    }
    Label { width: parent.width; text: qsTr("Now: %1").arg(d.access.longText); color: Theme.textSecondary }
    OptionButton { width: parent.width; selected: !d.instructor; title: selected ? qsTr("Trainee, selected") : qsTr("Trainee")
                   sub: qsTr("Choose your ID. No PIN."); onClicked: d.instructor = false }
    OptionButton { width: parent.width; enabled: d.access.pinSet; selected: d.instructor
                   title: !enabled ? qsTr("Instructor, not available") : selected ? qsTr("Instructor, selected") : qsTr("Instructor")
                   sub: enabled ? qsTr("Needs the PIN.") : qsTr("No PIN is set on this machine (set_instructor_pin).")
                   onClicked: { d.instructor = true; pin.take() } }
    // Fixed height: the dialog does not change size with the choice.
    Item {
        width: parent.width; height: Theme.px(116)
        Flickable {
            anchors.fill: parent
            visible: !d.instructor
            clip: true
            contentHeight: ids.height
            boundsBehavior: Flickable.StopAtBounds
            Flow {
                id: ids
                width: parent.width
                spacing: Theme.s8
                Repeater {
                    model: d.access.people
                    ChoiceButton { text: modelData.id; selected: d.chosen === modelData.id; onClicked: d.chosen = modelData.id }
                }
                Label { visible: d.access.people.length === 0; text: qsTr("No trainee records yet. The instructor adds them in Settings."); color: Theme.textSecondary }
            }
        }
        TextEntry { id: pin; width: parent.width; visible: d.instructor; label: qsTr("PIN"); secret: true; maximumLength: 12
                    validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
                    onAccepted: if (!d.access.busy) d.go() }
    }
    Row {
        spacing: Theme.s8
        height: Theme.px(22)
        StateIcon { level: d.access.message ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
        Label { height: parent.height; text: d.access.busy ? qsTr("Checking the PIN") : d.access.message; font.pixelSize: Theme.small }
    }
    Row {
        spacing: Theme.s12
        PrimaryButton { text: qsTr("Log in"); focusPolicy: Qt.NoFocus; enabled: !d.access.busy && (d.instructor ? pin.text.length >= 6 : d.chosen !== "")
                        onClicked: d.go() }
        SecondaryButton { text: qsTr("Log out"); focusPolicy: Qt.NoFocus; enabled: !d.access.busy && (d.access.trainee || d.access.instructor)
                          onClicked: if (d.access.logout()) d.closed() }
        SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: d.closed() }
    }
}
