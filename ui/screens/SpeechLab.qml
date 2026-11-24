import QtQuick
import Atc

// PTT and Speech Lab (D-09). View only.
Item {
    id: s
    property LabController lab
    property SttController stt
    readonly property int gap: Theme.s12
    readonly property int barH: Theme.px(128)
    readonly property int leftW: Math.round(width * 0.262)
    readonly property int rightW: Math.round(width * 0.242)
    readonly property int topH: height - barH - 3 * gap

    // ---- PTT events
    Card {
        id: log
        x: s.gap; y: s.gap; width: s.leftW; height: s.topH
        title: qsTr("PTT EVENTS")
        headerItems: Label {
            anchors.fill: parent
            horizontalAlignment: Text.AlignRight
            text: s.lab.pttSource; font.pixelSize: Theme.caption
        }
        Row {
            id: tiles
            x: Theme.s12; y: Theme.s8
            spacing: Theme.s8
            PttButtonTile { width: (log.width - 2 * Theme.s12 - Theme.s8) / 2; height: Theme.px(60)
                            name: qsTr("RIGHT (talk)"); down: s.lab.rightDown }
            PttButtonTile { width: (log.width - 2 * Theme.s12 - Theme.s8) / 2; height: Theme.px(60)
                            name: qsTr("LEFT (no function)"); down: s.lab.leftDown }
        }
        Rectangle { y: Theme.px(76); width: parent.width; height: Theme.line; color: Theme.divider }
        ListView {
            id: evList
            anchors { top: parent.top; topMargin: Theme.px(77); left: parent.left; right: parent.right; bottom: parent.bottom }
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.lab.events
            delegate: Item {
                required property string time
                required property string source
                required property string edge
                required property string detail
                required property bool warn
                width: evList.width; height: Theme.px(28)
                readonly property color ink: warn ? Theme.caution : Theme.textPrimary
                Row {
                    x: Theme.s12; height: parent.height
                    spacing: Theme.px(6)
                    Label { mono: true; font.pixelSize: Theme.caption; width: Theme.px(56); height: parent.height; text: time; color: Theme.textSecondary }
                    Label { mono: true; font.pixelSize: Theme.caption; width: Theme.px(50); height: parent.height; text: source }
                    Label { mono: true; font.pixelSize: Theme.caption; width: Theme.px(54); height: parent.height; text: edge; color: ink }
                    Label { mono: true; font.pixelSize: Theme.caption; width: evList.width - Theme.px(202); height: parent.height; text: detail; color: ink }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
            }
        }
    }

    // ---- centre
    Item {
        id: mid
        x: log.x + log.width + s.gap; y: s.gap
        width: s.width - s.leftW - s.rightW - 4 * s.gap; height: s.topH

        Card {
            id: textCard
            width: parent.width; height: Theme.px(216)
            title: qsTr("RECOGNISED TEXT")
            headerHeight: Theme.px(54)
            headerItems: ModelSelector { id: selector; anchors.fill: parent; stt: s.stt; readOnly: s.stt.locked }
            Label {
                x: Theme.s12; y: Theme.s8
                width: parent.width - 2 * x; height: Theme.px(50)
                mono: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                verticalAlignment: Text.AlignTop
                text: s.lab.text
                color: s.lab.textPartial ? Theme.textSecondary : Theme.textPrimary
                font { pixelSize: Theme.h2; italic: s.lab.textPartial }
            }
            // Result line: model and release-to-text, or why the model is not usable.
            Row {
                x: Theme.s12; y: Theme.px(60)
                width: parent.width - 2 * x - textState.width - Theme.s8; height: Theme.px(22)
                spacing: Theme.s8
                StateIcon { visible: level > 0 && s.stt.message !== ""; level: s.stt.level === LabController.Fault ? LabController.Fault : LabController.Caution
                            anchors.verticalCenter: parent.verticalCenter }
                Label { width: Math.min(implicitWidth, parent.width - Theme.px(s.lab.textPlaceholder ? 112 : 20)); height: parent.height; font.pixelSize: Theme.caption
                        text: s.stt.message || s.lab.resultInfo
                        elide: s.stt.message ? Text.ElideRight : Text.ElideMiddle
                        color: s.stt.message ? Theme.textPrimary : Theme.textSecondary }
                Tag { visible: s.lab.textPlaceholder; text: s.lab.textTag }
            }
            Label { id: textState; anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(60); height: Theme.px(22)
                    text: s.lab.textState; font.pixelSize: Theme.caption }
            Flow {
                x: Theme.s12; y: Theme.px(86)
                width: parent.width - 2 * x; height: Theme.px(50)
                spacing: Theme.s4
                clip: true
                Repeater {
                    model: s.lab.words
                    ConfidenceBar {
                        required property var model
                        width: (textCard.width - 2 * Theme.s12 - 7 * Theme.s4 - 2) / 8
                        word: model.word; conf: model.conf; low: model.low
                    }
                }
            }
            Label { x: Theme.s12; y: Theme.px(139); width: parent.width - 2 * x
                    text: qsTr("Bars: word confidence on the engine's own scale. \"?\" marks a low value.")
                    color: Theme.textMuted; font.pixelSize: Theme.caption }
        }

        Card {
            id: intent
            y: textCard.height + s.gap
            width: parent.width; height: Theme.px(70)
            title: qsTr("INTENT AND PARAMETERS")
            Row {
                x: Theme.s12; height: parent.height
                spacing: Theme.s8
                Label { text: qsTr("Parser not connected"); font { pixelSize: Theme.small; weight: Font.DemiBold } height: parent.height }
                Label { text: qsTr("Nothing on this screen is interpreted or scored yet."); color: Theme.textSecondary
                        font.pixelSize: Theme.small; height: parent.height }
            }
        }

        Card {
            y: intent.y + intent.height + s.gap
            width: parent.width; height: parent.height - y
            title: qsTr("RECORDING")
            headerItems: Label { anchors.fill: parent; horizontalAlignment: Text.AlignRight; mono: true
                                 text: s.lab.recordingInfo; font.pixelSize: Theme.caption; color: Theme.textSecondary }
            Waveform { width: parent.width; height: parent.height - Theme.px(116); lab: s.lab }
            Rectangle { y: parent.height - Theme.px(116); width: parent.width; height: Theme.line; color: Theme.divider }
            Row {
                id: checkRow
                y: parent.height - Theme.px(115)
                width: parent.width; height: Theme.px(50)
                Repeater {
                    model: s.lab.checks
                    CheckCell { width: checkRow.width / 4; height: checkRow.height
                                level: modelData.level; title: modelData.title; detail: modelData.detail }
                }
            }
            Rectangle { y: parent.height - Theme.px(65); width: parent.width; height: Theme.line; color: Theme.divider }
            Row {
                x: Theme.s12; y: parent.height - Theme.px(57)
                spacing: Theme.s8
                PrimaryButton { text: qsTr("Play"); enabled: s.lab.hasRecording; onClicked: s.lab.playRequested(false) }
                SecondaryButton { text: qsTr("Keyed part only"); enabled: s.lab.hasRecording; onClicked: s.lab.playRequested(true) }
            }
            Label { anchors { right: parent.right; rightMargin: Theme.s12 }
                    y: parent.height - Theme.px(57); height: Theme.touch
                    text: qsTr("Playback never\nadds radio effect"); horizontalAlignment: Text.AlignRight
                    color: Theme.textSecondary; font.pixelSize: Theme.caption }
        }
    }

    // ---- recordings
    Card {
        x: s.width - s.rightW - s.gap; y: s.gap; width: s.rightW; height: s.topH
        title: qsTr("RECORDINGS")
        headerItems: Label { anchors.fill: parent; horizontalAlignment: Text.AlignRight
                             text: qsTr("%1 shown").arg(recList.count); font.pixelSize: Theme.caption }
        ListView {
            id: recList
            anchors { fill: parent; bottomMargin: Theme.px(65) }
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.lab.recordings
            delegate: Rectangle {
                id: rec
                required property int index
                required property int recId
                required property string time
                required property string duration
                required property string text
                required property int level
                required property string status
                required property string result
                width: recList.width; height: Theme.px(64)
                color: index === s.lab.selectedRow ? Theme.raised : "transparent"
                Rectangle { visible: rec.index === s.lab.selectedRow; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                Label { x: Theme.s12; y: Theme.px(5); mono: true; font.pixelSize: Theme.caption; text: qsTr("TX %1  %2").arg(rec.recId).arg(rec.time) }
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
                TapHandler { onTapped: s.lab.selectRecording(rec.index) }
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Transmission %1, %2").arg(rec.recId).arg(rec.status)
            }
        }
        Rectangle { y: parent.height - Theme.px(65); width: parent.width; height: Theme.line; color: Theme.divider }
        Row {
            x: Theme.s12; y: parent.height - Theme.px(57)
            spacing: Theme.s8
            SecondaryButton {
                width: (s.rightW - 2 * Theme.s12 - Theme.s8) / 2
                text: qsTr("Older")
                enabled: !recList.atYEnd
                onClicked: recList.contentY = Math.min(recList.contentY + recList.height,
                                                       Math.max(0, recList.contentHeight - recList.height))
            }
            SecondaryButton { width: (s.rightW - 2 * Theme.s12 - Theme.s8) / 2; text: qsTr("Export")
                              enabled: false   // off for the slice (D-407)
                              onClicked: s.lab.exportRequested() }
        }
    }

    RadioBar {
        id: bar
        x: s.gap; y: s.height - height - s.gap; width: s.width - 2 * s.gap; height: s.barH
        lab: s.lab
        onPressing: selector.closeList()
    }
}
