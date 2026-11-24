import QtQuick
import QtQuick.Window
import Atc

Window {
    id: root
    required property LabController lab
    property MockBackend mock: null   // null: live backend
    required property SttController stt
    required property string themeMode
    required property bool reduceMotion
    property string screen_: "lab"
    width: 1280; height: 800
    minimumWidth: 1280; minimumHeight: 800
    visible: false
    title: qsTr("Military ATC Training Simulator")
    color: Theme.bg

    Binding { target: Theme; property: "windowWidth"; value: root.width }
    Binding { target: Theme; property: "windowHeight"; value: root.height }
    Binding { target: Theme; property: "reduceMotion"; value: root.reduceMotion }
    Component.onCompleted: Theme.mode = themeMode

    // Space is a fallback PTT; losing focus always releases it.
    onActiveChanged: if (!active) lab.releaseRequested("SPACE")

    // The Dashboard shows mock data until 1a feeds it; a live build keeps it disabled.
    DashboardController {
        id: dashCtl
        feed: root.mock ? root.mock.status : null
        stt: root.stt
    }
    onScreen_Changed: { lab.releaseRequested("SPACE"); dashCtl.cancelTests() }
    Connections {
        target: root.mock
        function onCurrentChanged() { if (root.mock.current.startsWith("dash")) root.screen_ = "dash" }
    }

    Item {
        id: shell
        anchors.fill: parent
        focus: true
        Rectangle { anchors.fill: parent; color: Theme.bg }
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Space) { if (!e.isAutoRepeat && (root.screen_ === "lab" || dashCtl.pttTesting)) root.lab.pressRequested("SPACE"); e.accepted = true }
            else if (e.key === Qt.Key_M && root.mock) { panel.active = !panel.active; e.accepted = true }
        }
        Keys.onReleased: (e) => {
            if (e.key === Qt.Key_Space) { if (!e.isAutoRepeat) root.lab.releaseRequested("SPACE"); e.accepted = true }
        }

        StatusBar {
            id: status
            width: parent.width; height: Theme.px(48)
            chips: root.lab.chips
            scenario: qsTr("No scenario")
            mock: root.mock !== null
        }
        NavRail {
            id: nav
            anchors { top: status.bottom; bottom: alert.top }
            width: Theme.px(88)
            current: root.screen_
            onActivated: (id) => root.screen_ = id
            entries: [
                { id: "dash", name: qsTr("Dashboard"), enabled: root.mock !== null, icon: "M 3 3 L 9 3 L 9 9 L 3 9 Z M 11 3 L 17 3 L 17 9 L 11 9 Z M 3 11 L 9 11 L 9 17 L 3 17 Z M 11 11 L 17 11 L 17 17 L 11 17 Z" },
                { id: "scen", name: qsTr("Scenarios"), enabled: false, icon: "M 4 5 L 16 5 M 4 10 L 16 10 M 4 15 L 16 15" },
                { id: "ex", name: qsTr("Exercise"), enabled: false, icon: "M 6 4 L 16 10 L 6 16 Z" },
                { id: "lab", name: qsTr("Speech Lab"), enabled: true, icon: "M 7 3 L 13 3 L 13 11 L 7 11 Z M 4 9 L 4 14 L 16 14 L 16 9 M 10 14 L 10 18" },
                { id: "ass", name: qsTr("Assessment"), enabled: false, icon: "M 4 16 L 4 10 M 10 16 L 10 4 M 16 16 L 16 8" },
                { id: "set", name: qsTr("Settings"), enabled: false, icon: "M 3 6 L 17 6 M 13 4 L 13 8 M 3 14 L 17 14 M 7 12 L 7 16" }
            ]
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.screen_ === "lab"
            sourceComponent: Component { SpeechLab { lab: root.lab; stt: root.stt } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.screen_ === "dash"
            sourceComponent: Component { Dashboard { dash: dashCtl } }
        }
        AlertBanner {
            id: alert
            anchors.bottom: parent.bottom
            width: parent.width; height: Theme.px(32)
            // A radio alert comes first; otherwise the Dashboard shows its own summary.
            readonly property bool dashAlert: root.screen_ === "dash" && root.lab.alertLevel < LabController.Caution
            level: dashAlert ? dashCtl.alertLevel : root.lab.alertLevel
            text: dashAlert ? dashCtl.alert : root.lab.alert
        }

        BaseButton {
            anchors { left: parent.left; leftMargin: Theme.s8; bottom: alert.top; bottomMargin: Theme.s8 }
            width: Theme.px(72)
            visible: root.mock !== null
            text: qsTr("Mock (M)")
            tint: Theme.caution; ink: Theme.caution
            font.pixelSize: Theme.caption
            focusPolicy: Qt.NoFocus
            onClicked: panel.active = !panel.active
        }
        Loader {
            id: panel
            active: false
            anchors { left: nav.right; leftMargin: Theme.s8; bottom: alert.top; bottomMargin: Theme.s8 }
            sourceComponent: Component { MockPanel { mock: root.mock } }
        }
    }
}
