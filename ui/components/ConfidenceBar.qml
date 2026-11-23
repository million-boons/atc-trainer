import QtQuick
import Atc

// Word over a bar. low: also marked with "?".
Item {
    property string word
    property real conf: 0
    property bool low: false
    implicitWidth: Theme.px(56); implicitHeight: Theme.px(23)
    Label { mono: true; width: parent.width; font.pixelSize: Theme.caption
            text: low ? word + " ?" : word; color: low ? Theme.caution : Theme.textPrimary }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: Theme.px(4)
        radius: height / 2
        color: Theme.border
        Rectangle { width: parent.width * conf; height: parent.height; radius: height / 2
                    color: low ? Theme.caution : Theme.textSecondary }
    }
}
