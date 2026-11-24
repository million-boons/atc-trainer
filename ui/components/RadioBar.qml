import QtQuick
import Atc

// Radio bar, fixed height: TX, RX, microphone, notice, Screen PTT. The same on every screen that transmits.
// noticeLevel, notice: what the notice line shows. pressing(): Screen PTT, before the press.
Card {
    id: bar
    property LabController lab
    property int noticeLevel: lab.noticeLevel
    property string notice: lab.notice
    signal pressing()
    readonly property int blockH: Theme.px(102)
    height: Theme.px(128)

    TxRxIndicator {
        id: txInd
        x: Theme.s12; y: (parent.height - height) / 2
        width: Theme.px(260); height: bar.blockH
        tone: bar.lab.txTone; label: bar.lab.txLabel; value: bar.lab.txTime; sub: bar.lab.txSub; subLevel: bar.lab.txSubLevel
    }
    TxRxIndicator {
        id: rxInd
        x: txInd.x + txInd.width + Theme.s12; y: txInd.y
        width: Theme.px(176); height: bar.blockH
        tone: bar.lab.rxActive ? LabController.Active : LabController.Neutral
        label: qsTr("RX"); sub: bar.lab.rxSub
        accent: Theme.rx; fill: Theme.rxFill
        Tag { visible: bar.lab.pilotCut; solid: true; text: qsTr("PILOT CUT")
              anchors { right: parent.right; top: parent.top; margins: Theme.s8 } }
    }
    Item {
        x: rxInd.x + rxInd.width + Theme.s12; y: txInd.y
        width: ptt.x - x - Theme.s12; height: bar.blockH
        LevelMeter { width: parent.width; dbfs: bar.lab.micDbfs; low: bar.lab.micLow; high: bar.lab.micHigh; target: bar.lab.micTarget }
        Row {
            y: Theme.px(64)
            width: parent.width; height: Theme.px(38)
            spacing: Theme.s8
            StateIcon { id: ni; level: bar.noticeLevel; size: Theme.px(16); anchors.verticalCenter: parent.verticalCenter }
            Label {
                width: parent.width - (ni.visible ? ni.width + Theme.s8 : 0); height: parent.height
                wrapMode: Text.Wrap
                maximumLineCount: 2
                lineHeight: 0.95
                text: bar.notice || qsTr("No notices")
                color: bar.notice ? Theme.textPrimary : Theme.textMuted
                font.pixelSize: Theme.small
            }
        }
    }
    Rectangle {
        id: ptt
        x: parent.width - width - Theme.s12; y: txInd.y
        width: Theme.px(176); height: bar.blockH
        radius: Theme.radius
        color: Theme.raised
        border { color: area.pressed ? Theme.tx : Theme.border; width: (area.pressed ? 2 : 1) * Theme.line }
        Label { anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.verticalCenter; verticalCenterOffset: -Theme.px(10) }
                text: qsTr("Screen PTT"); font { pixelSize: Theme.h2; weight: Font.DemiBold } }
        Label { anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.verticalCenter; verticalCenterOffset: Theme.px(14) }
                text: qsTr("hold, or Space"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
        MouseArea {
            id: area
            anchors.fill: parent
            preventStealing: true
            onPressed: { bar.pressing(); bar.lab.pressRequested("SCREEN") }
            onReleased: bar.lab.releaseRequested("SCREEN")
            onCanceled: bar.lab.releaseRequested("SCREEN")
        }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Screen PTT, hold to talk")
    }
}
