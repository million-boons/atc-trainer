import QtQuick
import Atc

// Plain text of a few lines with a limit. Enter leaves the field.
Rectangle {
    id: r
    property alias text: edit.text
    property int limit: 200
    property string name
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
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            color: Theme.textPrimary
            selectionColor: Theme.accent; selectedTextColor: Theme.onFill
            selectByMouse: true
            inputMethodHints: Qt.ImhNoPredictiveText
            font { family: Theme.ui; pixelSize: Theme.small }
            Accessible.name: r.name
            onTextChanged: {
                if (!activeFocus) return
                if (text.length > r.limit) { remove(r.limit, text.length); return }   // comes here again
                r.edited(text)
            }
            Keys.onReturnPressed: r.leaving()
            Keys.onEnterPressed: r.leaving()
            Keys.onEscapePressed: r.leaving()
        }
    }
}
