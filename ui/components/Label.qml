import QtQuick
import Atc

// Text with theme defaults. mono: JetBrains Mono.
Text {
    property bool mono: false
    color: Theme.textPrimary
    font.family: mono ? Theme.mono : Theme.ui
    font.pixelSize: Theme.body
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.PlainText
}
