import QtQuick
import Atc

// Right pane of the Studio: what to say, record, trim, play, decide. Fixed places: nothing moves when a take appears.
Card {
    id: r
    property StudioController studio
    signal deleteAsked()
    signal touched()
    readonly property var clip_: studio.clip
    readonly property var take_: studio.take
    readonly property bool free: !studio.recording && !studio.busy
    function esc(t) { return (t || "").replace(/&/g, "&amp;").replace(/</g, "&lt;") }
    title: qsTr("RECORDER")
    Label { anchors { fill: parent; margins: Theme.s12 } visible: !r.studio.replies || !r.studio.hasClip
            verticalAlignment: Text.AlignTop; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.small
            text: r.studio.replies ? qsTr("Choose a segment. A new clip can be recorded after its text is saved.") : qsTr("ATC examples are not recorded.") }
    Item {
        visible: r.studio.replies && r.studio.hasClip
        x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: parent.height - 2 * y
        Label { id: idT; width: parent.width; height: Theme.px(20); text: r.clip_.id || ""; mono: true; font.pixelSize: Theme.small }
        Label { id: wordsT; anchors.top: idT.bottom; width: parent.width; height: Theme.px(18); text: qsTr("Words \"%1\"").arg(r.clip_.words || ""); font.pixelSize: Theme.small }
        Label { id: usedT; anchors.top: wordsT.bottom; width: parent.width; height: Theme.px(18); font.pixelSize: Theme.caption
                color: r.clip_.differs ? Theme.caution : Theme.textSecondary; text: r.clip_.differs || r.clip_.used || "" }
        Label { id: sayT; anchors.top: usedT.bottom; width: parent.width; height: Theme.px(18); text: qsTr("Say this:"); font { pixelSize: Theme.caption; weight: Font.DemiBold } color: Theme.textSecondary }
        Label { id: say; objectName: "sayThis"; anchors.top: sayT.bottom; width: parent.width; height: Theme.px(56); wrapMode: Text.WordWrap; maximumLineCount: 3
                verticalAlignment: Text.AlignTop; textFormat: Text.StyledText; font.pixelSize: Theme.small
                text: r.esc(r.clip_.before) + " <b><u>" + r.esc(r.clip_.target) + "</u></b> " + r.esc(r.clip_.after) }

        BaseButton {
            id: rec
            anchors.top: say.bottom
            width: Theme.px(124)
            enabled: r.studio.recording || r.studio.canRecord
            tint: r.studio.recording ? Theme.fault : Theme.accent
            text: r.studio.recording ? qsTr("STOP") : qsTr("RECORD")
            font.weight: Font.DemiBold
            leftPadding: Theme.px(22)
            focusPolicy: Qt.NoFocus
            onClicked: { r.touched(); r.studio.toggleRecord() }
            Rectangle { x: Theme.s12; width: Theme.px(12); height: width; anchors.verticalCenter: parent.verticalCenter
                        radius: r.studio.recording ? 0 : width / 2; color: rec.enabled ? Theme.fault : Theme.textMuted }
        }
        Label { anchors { left: rec.right; leftMargin: Theme.s8; top: rec.top } width: Theme.px(64); height: Theme.touch; mono: true; text: r.studio.elapsed; font.pixelSize: Theme.small }
        ChoiceButton { anchors { right: parent.right; top: rec.top } width: Theme.px(92); text: qsTr("Pedal"); selected: r.studio.pedal
                       enabled: r.studio.session && !r.studio.recording; onClicked: r.studio.setPedal(!r.studio.pedal) }
        Item {
            id: meter
            anchors { top: rec.bottom; topMargin: Theme.s4 }
            width: parent.width; height: Theme.px(20)
            Rectangle {
                width: parent.width - Theme.px(84); height: Theme.px(12); anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radiusChip; color: Theme.raised
                border { color: Theme.border; width: Theme.line }
                Rectangle { x: Theme.line; y: Theme.line; height: parent.height - 2 * y; width: (parent.width - 2 * x) * Math.max(0, Math.min(1, (r.studio.level + 60) / 60))
                            color: r.studio.level > -1 ? Theme.fault : Theme.textSecondary }
                Repeater { model: [-20, -6]
                           Rectangle { x: parent.width * (modelData + 60) / 60; width: 2 * Theme.line; height: parent.height; color: Theme.ok } }
            }
            Label { anchors.right: parent.right; height: parent.height; mono: true; font.pixelSize: Theme.caption
                    text: !r.studio.recording ? qsTr("level off") : r.studio.level > -1 ? qsTr("TOO LOUD") : r.studio.level.toFixed(0) + " dBFS" }
        }
        TrimWaveform {
            id: wave
            anchors { top: meter.bottom; topMargin: Theme.s4 }
            width: parent.width; height: Theme.px(64)
            visible: r.studio.hasTake
            peaks: r.take_.peaks || []
            from: r.take_.start || 0
            to: r.take_.end === undefined ? 1 : r.take_.end
            editable: (r.take_.trimmable || false) && r.free
            onTrimmed: (a, b) => r.studio.setTrim(a, b)
        }
        Label { anchors.fill: wave; visible: !r.studio.hasTake; text: qsTr("No take"); color: Theme.textMuted; font.pixelSize: Theme.small; horizontalAlignment: Text.AlignHCenter }
        Row {
            id: warn
            anchors.top: wave.bottom
            height: Theme.px(22)
            spacing: Theme.s4
            StateIcon { level: r.take_.warnings ? LabController.Caution : 0; anchors.verticalCenter: parent.verticalCenter }
            Label { height: parent.height; text: r.take_.warnings || ""; font.pixelSize: Theme.caption }
        }
        Row {
            id: plays
            anchors.top: warn.bottom
            width: parent.width
            spacing: Theme.s4
            Repeater {
                model: [["raw", qsTr("Raw")], ["trimmed", qsTr("Trimmed")], ["radio", qsTr("Radio")], ["sentence", qsTr("Sentence")]]
                BaseButton { width: (plays.width - 3 * Theme.s4) / 4; text: modelData[1]; font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus
                             enabled: r.studio.hasTake && r.free && !r.take_.withdrawn
                             Accessible.name: qsTr("Play %1").arg(modelData[1])
                             onClicked: { r.touched(); r.studio.play(modelData[0]) } }
            }
        }
        Row {
            id: acts
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: Theme.s4
            readonly property bool on: r.studio.hasTake && r.free
            component Act: BaseButton { width: (acts.width - 2 * Theme.s4) / 3; font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus }
            Act { text: qsTr("Primary"); tint: Theme.accent; enabled: acts.on && (r.take_.decidable || false); onClicked: { r.touched(); r.studio.accept(true) } }
            Act { text: qsTr("Alternate"); enabled: acts.on && (r.take_.decidable || false); onClicked: { r.touched(); r.studio.accept(false) } }
            Act { text: r.take_.removable ? qsTr("Delete...") : qsTr("Reject"); tint: r.take_.removable ? Theme.fault : Theme.border; enabled: acts.on
                  onClicked: { r.touched(); r.take_.removable ? r.deleteAsked() : r.studio.reject() } }
        }
        TakeList {
            anchors { top: plays.bottom; topMargin: Theme.s4; bottom: acts.top; bottomMargin: Theme.s4 }
            width: parent.width
            studio: r.studio
        }
    }
}
