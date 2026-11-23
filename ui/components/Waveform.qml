import QtQuick
import Atc

// Waveform of the selected recording with labelled press and end markers.
Item {
    id: wf
    property LabController lab
    WaveformItem {
        anchors.fill: parent
        visible: wf.lab.hasRecording
        lab: wf.lab
        color: Theme.textSecondary
        shade: Theme.raised
        marker: Theme.tx
        clipColor: Theme.fault
    }
    component Mark: Rectangle {
        property alias text: t.text
        property alias ink: t.color
        visible: wf.lab.hasRecording
        width: t.implicitWidth + Theme.px(6); height: Theme.px(18)
        radius: Theme.radiusChip
        color: Theme.surface
        Label { id: t; anchors.centerIn: parent; font.pixelSize: Theme.caption; color: Theme.textMuted }
    }
    Mark { text: wf.lab.startLabel; ink: Theme.tx; x: wf.width * wf.lab.pressFrac + Theme.s4; y: Theme.px(2) }
    Mark { text: wf.lab.endLabel; ink: Theme.tx; x: wf.width * wf.lab.endFrac - width - Theme.s4; y: Theme.px(2) }
    Mark { visible: wf.lab.hasRecording && wf.lab.pressFrac > 0; text: qsTr("pre-roll"); x: Theme.px(2); y: wf.height - height - Theme.px(2) }
    Mark { text: wf.lab.tailLabel; x: wf.width - width - Theme.px(2); y: wf.height - height - Theme.px(2) }
    Label {
        visible: !wf.lab.hasRecording
        anchors { fill: parent; margins: Theme.s12 }
        verticalAlignment: Text.AlignTop
        text: qsTr("The last recording appears here after release.")
        color: Theme.textMuted
        font.pixelSize: Theme.caption
    }
}
