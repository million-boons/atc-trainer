import QtQuick
import Atc

// chips: list of {name, value, level}. scenario: active scenario name; two lines, then shortened, when the
// chips need the space. mock: show MOCK. login: who is logged in, short; loginAsked(): its button;
// loggedIn: show Log out; logoutAsked(). The app name gives way last, so that nothing overlaps.
Rectangle {
    id: bar
    property var chips: []
    property string scenario
    property bool mock: false
    property string login
    property string loginLong
    property bool loginEnabled: true
    property bool loggedIn: false
    property string clockMode: "both"     // both, utc, local
    onClockModeChanged: clock.refresh()
    signal loginAsked()
    signal logoutAsked()
    // Width the right part needs without the scenario name.
    readonly property int fixedW: clock.width + loginBtn.width + (loggedIn ? logoutBtn.width + Theme.s8 : 0) + (mock ? mockTag.width + Theme.s8 : 0) + 2 * Theme.s8
    color: Theme.surface
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.border }

    Label {
        id: name
        x: Theme.s12
        height: parent.height
        width: Math.max(Theme.px(80), Math.min(implicitWidth, bar.width - chipRow.width - bar.fixedW - 3 * Theme.s12))
        text: qsTr("MILITARY ATC TRAINING SIMULATOR")
        font { pixelSize: Theme.small; weight: Font.DemiBold }
    }
    Row {
        id: chipRow
        objectName: "chipRow"
        anchors { left: name.right; leftMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.s4
        Repeater {
            model: bar.chips
            StatusChip { name: modelData.name; value: modelData.value; level: modelData.level }
        }
    }
    Row {
        objectName: "statusRight"
        anchors { right: parent.right; rightMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.s8
        Tag { id: mockTag; visible: bar.mock; text: qsTr("MOCK"); anchors.verticalCenter: parent.verticalCenter }
        Label { objectName: "scenarioName"; text: bar.scenario; color: Theme.textSecondary; font.pixelSize: Theme.caption
                wrapMode: Text.WordWrap; maximumLineCount: 2
                width: Math.max(0, Math.min(implicitWidth, bar.width - chipRow.x - chipRow.width - bar.fixedW - 2 * Theme.s12))
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
        BaseButton {
            id: logoutBtn
            visible: bar.loggedIn
            width: logoutText.implicitWidth + 2 * Theme.s8
            enabled: bar.loginEnabled
            focusPolicy: Qt.NoFocus
            onClicked: bar.logoutAsked()
            Accessible.name: qsTr("Log out")
            contentItem: Label { id: logoutText; text: qsTr("Log out"); color: bar.loginEnabled ? Theme.textPrimary : Theme.textMuted
                                 horizontalAlignment: Text.AlignHCenter; font.pixelSize: Theme.caption }
        }
        Label {
            id: clock
            mono: true
            font.pixelSize: Theme.caption
            anchors.verticalCenter: parent.verticalCenter
            function refresh() {
                var d = new Date()
                function z(n) { return String(n).padStart(2, "0") }
                var utc = z(d.getUTCHours()) + ":" + z(d.getUTCMinutes()) + ":" + z(d.getUTCSeconds()) + "Z"
                var local = z(d.getHours()) + ":" + z(d.getMinutes()) + (bar.clockMode === "local" ? ":" + z(d.getSeconds()) : "") + "L"
                text = bar.clockMode === "utc" ? utc : bar.clockMode === "local" ? local : utc + "\n" + local
            }
            Component.onCompleted: refresh()
            Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.refresh() }
        }
    }
}
