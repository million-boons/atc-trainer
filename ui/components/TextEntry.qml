import QtQuick
import QtQuick.Templates as T
import Atc

// One-line text field with a label above. secret: digits shown as dots. Takes the keyboard while it has focus.
Item {
    id: e
    property string label
    property alias text: field.text
    property alias validator: field.validator
    property alias maximumLength: field.maximumLength
    property bool secret: false
    property bool mono: false
    signal accepted()
    function take() { field.forceActiveFocus() }
    implicitHeight: Theme.px(22) + Theme.touch
    Label { width: parent.width; height: Theme.px(20); text: e.label; color: Theme.textSecondary; font.pixelSize: Theme.caption }
    T.TextField {
        id: field
        y: Theme.px(22); width: parent.width; height: Theme.touch
        leftPadding: Theme.s12; rightPadding: Theme.s12
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.textPrimary
        selectionColor: Theme.accent; selectedTextColor: Theme.onFill
        echoMode: e.secret ? TextInput.Password : TextInput.Normal
        inputMethodHints: e.secret ? Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText : Qt.ImhNoPredictiveText
        font { family: e.mono || e.secret ? Theme.mono : Theme.ui; pixelSize: Theme.body }
        onAccepted: e.accepted()
        Accessible.name: e.label
        background: Rectangle {
            radius: Theme.radius
            color: Theme.raised
            border { color: field.activeFocus ? Theme.accent : Theme.border; width: (field.activeFocus ? 2 : 1) * Theme.line }
        }
    }
}
