import QtQuick
import Atc

// chips: list of {name, value, level}. scenario: active scenario name. mock: show MOCK DATA.
Rectangle {
    id: bar
    property var chips: []
    property string scenario
    property bool mock: false
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
        anchors { left: name.right; leftMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.px(6)
        Repeater {
            model: bar.chips
            StatusChip { name: modelData.name; value: modelData.value; level: modelData.level }
        }
    }
    Row {
        anchors { right: parent.right; rightMargin: Theme.s12; verticalCenter: parent.verticalCenter }
        spacing: Theme.s8
        Tag { visible: bar.mock; text: qsTr("MOCK DATA"); anchors.verticalCenter: parent.verticalCenter }
        Label { text: bar.scenario; color: Theme.textSecondary; font.pixelSize: Theme.caption
                anchors.verticalCenter: parent.verticalCenter }
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
