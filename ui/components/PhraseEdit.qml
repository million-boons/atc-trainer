import QtQuick
import Atc

// One template, edited as text with {slot}. A template is one line: Enter leaves the field.
Rectangle {
    id: r
    property alias text: edit.text
    property bool readOnly: false
    signal edited(string text)
    signal leaving()
    function leave() { edit.focus = false }
    radius: Theme.radius
    color: Theme.raised
    border { color: edit.activeFocus ? Theme.accent : Theme.border; width: (edit.activeFocus ? 2 : 1) * Theme.line }
    Flickable {
        id: view
        anchors { fill: parent; margins: Theme.s8 }
        contentHeight: edit.contentHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        TextEdit {
            id: edit
            width: view.width; height: Math.max(implicitHeight, view.height)
            readOnly: r.readOnly
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            color: Theme.textPrimary
            selectionColor: Theme.accent; selectedTextColor: Theme.onFill
            selectByMouse: true
            inputMethodHints: Qt.ImhNoPredictiveText
            font { family: Theme.ui; pixelSize: Theme.body }
            Accessible.name: qsTr("Template")
            onTextChanged: if (activeFocus) r.edited(text)
            Keys.onReturnPressed: r.leaving()
            Keys.onEnterPressed: r.leaving()
            Keys.onEscapePressed: r.leaving()
        }
    }
    SlotHighlighter { target: edit.textDocument; color: Theme.accent; family: Theme.mono }
}
