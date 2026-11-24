import QtQuick
import Atc

// chips: list of {name, value, level}. scenario: active scenario name; it is shortened when the chips
// need the space. mock: show MOCK. login: who is logged in, short; loginAsked(): its button.
Rectangle {
    id: bar
    property var chips: []
    property string scenario
    property bool mock: false
    property string login
    property string loginLong
    property bool loginEnabled: true
    signal loginAsked()
    color: Theme.surface
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.border }

    Label {
        id: name
        x: Theme.s12
        height: parent.height
        text: qsTr("MILITARY ATC TRAINING SIMULATOR")
        font { pixelSize: Theme.small; weight: Font.DemiBold }
    }
    Row {
        id: chipRow
        anchors { left: name.right; leftMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.s4
        Repeater {
            model: bar.chips
            StatusChip { name: modelData.name; value: modelData.value; level: modelData.level }
        }
    }
    Row {
        anchors { right: parent.right; rightMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.s8
        Tag { id: mockTag; visible: bar.mock; text: qsTr("MOCK"); anchors.verticalCenter: parent.verticalCenter }
        Label { text: bar.scenario; color: Theme.textSecondary; font.pixelSize: Theme.caption
                width: Math.max(0, Math.min(implicitWidth, bar.width - chipRow.x - chipRow.width - clock.width
                       - (bar.mock ? mockTag.width + Theme.s8 : 0) - loginBtn.width - Theme.s12 - 3 * Theme.s8))
                anchors.verticalCenter: parent.verticalCenter }
        BaseButton {
            id: loginBtn
            width: Math.max(Theme.touch, loginText.implicitWidth + 2 * Theme.s8)
            enabled: bar.loginEnabled
            focusPolicy: Qt.NoFocus
            onClicked: bar.loginAsked()
            Accessible.name: qsTr("Login: %1").arg(bar.loginLong)
            contentItem: Label { id: loginText; text: bar.login; color: bar.loginEnabled ? Theme.textPrimary : Theme.textMuted; horizontalAlignment: Text.AlignHCenter
                                 font { pixelSize: Theme.caption; weight: Font.DemiBold } }
        }
        Label {
            id: clock
            mono: true
            font.pixelSize: Theme.caption
            anchors.verticalCenter: parent.verticalCenter
            function refresh() {
                var d = new Date()
                function z(n) { return String(n).padStart(2, "0") }
                text = z(d.getUTCHours()) + ":" + z(d.getUTCMinutes()) + ":" + z(d.getUTCSeconds()) + "Z "
                     + z(d.getHours()) + ":" + z(d.getMinutes()) + "L"
            }
            Component.onCompleted: refresh()
            Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.refresh() }
        }
    }
}
