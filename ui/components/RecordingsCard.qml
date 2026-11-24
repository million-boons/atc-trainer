import QtQuick
import Atc

// Recordings of this session, or the stored ones (04 section 5b). deleteAsked(row, what): to be confirmed.
Card {
    id: c
    property LabController lab
    property RecordingsController recs
    property bool storedView: false
    property int storedRow: -1
    signal deleteAsked(int row, string what)
    title: qsTr("RECORDINGS")
    headerItems: Label { anchors.fill: parent; horizontalAlignment: Text.AlignRight; font.pixelSize: Theme.caption
                         text: c.storedView ? qsTr("%1 stored").arg(storedList.count) : qsTr("%1 shown").arg(recList.count) }
    Label { x: Theme.s12; y: Theme.s4; width: parent.width - 2 * x; height: Theme.px(24); font { pixelSize: Theme.small; weight: Font.DemiBold }
            text: !c.storedView ? c.recs.speakerText : c.recs.message || (!c.recs.playerReady ? qsTr("Playback after merge") : c.recs.canDelete ? qsTr("Stored recordings")
                                                                                             : qsTr("Play and delete: instructor only")) }
    StateIcon { id: li; x: Theme.s12; y: Theme.px(36); level: c.recs.lineLevel }
    Label { x: Theme.px(32); y: Theme.px(30); width: parent.width - x - Theme.s12; height: Theme.px(24); text: c.recs.lineText; font.pixelSize: Theme.small }
    Rectangle { y: Theme.px(58); width: parent.width; height: Theme.line; color: Theme.divider }

    component Entry: Rectangle {
        id: rec
        property string head
        property string duration
        property string text
        property int level: 0
        property string status
        property string result
        property bool selected: false
        signal tapped()
        height: Theme.px(64)
        color: selected ? Theme.raised : "transparent"
        Rectangle { visible: rec.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
        Label { x: Theme.s12; y: Theme.px(5); mono: true; font.pixelSize: Theme.caption; text: rec.head }
        Label { anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(5); mono: true; font.pixelSize: Theme.caption
                text: rec.duration; color: Theme.textSecondary }
        Label { x: Theme.s12; y: Theme.px(24); width: parent.width - 2 * x; text: rec.text; font.pixelSize: Theme.small }
        Row {
            x: Theme.s12; y: Theme.px(43)
            spacing: Theme.s4
            StateIcon { level: rec.level; size: Theme.px(11); anchors.verticalCenter: parent.verticalCenter }
            Label { id: recStatus; text: rec.status; color: Theme.levelColor(rec.level); font.pixelSize: Theme.caption }
        }
        Label { anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(43)
                width: Math.min(implicitWidth, parent.width - recStatus.width - Theme.px(48))
                text: rec.result; elide: Text.ElideMiddle; color: Theme.textSecondary; font.pixelSize: Theme.caption }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
        TapHandler { onTapped: rec.tapped() }
        Accessible.role: Accessible.ListItem
        Accessible.name: qsTr("%1, %2").arg(rec.head).arg(rec.status)
    }
    ListView {
        id: recList
        anchors { fill: parent; topMargin: Theme.px(59); bottomMargin: Theme.px(65) }
        visible: !c.storedView
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: c.lab.recordings
        delegate: Entry {
            required property int index
            required property var model
            width: recList.width
            head: qsTr("TX %1  %2").arg(model.recId).arg(model.time); duration: model.duration; text: model.text
            level: model.level; status: model.status; result: model.result
            selected: index === c.lab.selectedRow
            onTapped: c.lab.selectRecording(index)
        }
    }
    ListView {
        id: storedList
        anchors.fill: recList
        visible: c.storedView
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: c.recs.stored
        onCountChanged: c.storedRow = -1
        delegate: Entry {
            required property int index
            required property var model
            width: storedList.width
            head: qsTr("%1  %2").arg(model.speaker).arg(model.time); duration: model.duration; text: model.text
            level: model.level; status: model.status; result: qsTr("TX %1").arg(model.tx)
            selected: index === c.storedRow
            onTapped: c.storedRow = index
        }
    }
    Rectangle { y: parent.height - Theme.px(65); width: parent.width; height: Theme.line; color: Theme.divider }
    Row {
        x: Theme.s12; y: parent.height - Theme.px(57)
        spacing: Theme.s8
        readonly property int third: (parent.width - 2 * Theme.s12 - 2 * Theme.s8) / 3
        SecondaryButton {
            width: parent.third
            font.pixelSize: Theme.small
            focusPolicy: Qt.NoFocus
            text: c.storedView ? qsTr("Session") : recList.atYEnd ? qsTr("Stored") : qsTr("Older")
            onClicked: {
                if (!c.storedView && !recList.atYEnd)
                    recList.contentY = Math.min(recList.contentY + recList.height, Math.max(0, recList.contentHeight - recList.height))
                else { c.storedView = !c.storedView; if (c.storedView) c.recs.refreshStored() }
            }
        }
        SecondaryButton {
            width: parent.third
            font.pixelSize: Theme.small
            focusPolicy: Qt.NoFocus
            text: qsTr("Play")
            enabled: c.storedView && c.recs.canPlay && c.storedRow >= 0
            Accessible.description: qsTr("Stored recordings, instructor only")
            onClicked: c.recs.playStored(c.storedRow)
        }
        DangerButton {
            width: parent.third
            font.pixelSize: Theme.small
            focusPolicy: Qt.NoFocus
            text: qsTr("Delete")
            enabled: c.storedView && c.recs.canDelete && c.storedRow >= 0
            Accessible.description: qsTr("Stored recordings, instructor only")
            onClicked: c.deleteAsked(c.storedRow, storedList.itemAtIndex(c.storedRow) ? storedList.itemAtIndex(c.storedRow).head : "")
        }
    }
}
