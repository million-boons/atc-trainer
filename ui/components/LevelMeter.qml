import QtQuick
import Atc

// Peak meter, -60..0 dBFS, with limits (low, high) as lines and the normal-speech
// target as a mark below the track and in the label.
Item {
    id: m
    property real dbfs: -120
    property real low: -20
    property real high: -6
    property real target: -12
    implicitHeight: Theme.px(62)
    function frac(d) { return Math.max(0, Math.min(1, (d + 60) / 60)) }

    Label { text: qsTr("Microphone   normal speech %1 dBFS").arg(m.target); font.pixelSize: Theme.small; height: Theme.px(18) }
    Label {
        anchors.right: parent.right
        height: Theme.px(18)
        mono: true
        font.pixelSize: Theme.small
        text: m.dbfs <= -100 ? qsTr("silent") : m.dbfs.toFixed(0) + " dBFS"
    }
    Rectangle {
        id: track
        y: Theme.px(20)
        width: parent.width; height: Theme.px(20)
        radius: Theme.radiusChip
        color: Theme.raised
        border { color: Theme.border; width: Theme.line }
        Rectangle {
            x: Theme.line; y: Theme.line
            height: parent.height - 2 * Theme.line
            width: (parent.width - 2 * Theme.line) * m.frac(m.dbfs)
            color: m.dbfs > -1 ? Theme.fault : Theme.textSecondary
        }
        Repeater {
            model: [m.low, m.high]
            Rectangle { x: track.width * m.frac(modelData); width: 2 * Theme.line; height: track.height; color: Theme.ok }
        }
    }
    Rectangle { x: m.width * m.frac(m.target) - width / 2; y: Theme.px(37)
                width: Theme.px(8); height: Theme.px(6); color: Theme.textPrimary }
    Repeater {
        model: [-60, -40, -20, -12, -6, 0]
        Label {
            mono: true
            font.pixelSize: Theme.caption
            color: Theme.textSecondary
            text: modelData
            y: Theme.px(43)
            x: Math.max(0, Math.min(m.width - width, m.width * m.frac(modelData) - width / 2))
        }
    }
}
