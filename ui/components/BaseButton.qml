import QtQuick
import QtQuick.Templates as T
import Atc

// Shared button. tint: border and text colour.
T.Button {
    id: b
    property color tint: Theme.border
    property color ink: Theme.textPrimary
    implicitHeight: Theme.touch
    implicitWidth: Math.max(Theme.touch, label.implicitWidth + 2 * Theme.s16)
    focusPolicy: Qt.TabFocus
    font { family: Theme.ui; pixelSize: Theme.body }
    contentItem: Label {
        id: label
        text: b.text
        font: b.font
        color: b.enabled ? b.ink : Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
    }
    background: Rectangle {
        radius: Theme.radius
        color: b.down ? Theme.border : Theme.raised
        border { width: b.visualFocus ? 2 * Theme.line : Theme.line
                 color: b.visualFocus ? Theme.accent : b.enabled ? b.tint : Theme.divider }
    }
}
