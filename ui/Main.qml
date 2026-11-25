import QtQuick
import QtQuick.Window
import Atc

Window {
    id: root
    required property LabController lab
    property MockBackend mock: null   // null: live backend
    required property SttController stt
    required property StatusFeed feed
    required property AccessController access
    required property SettingsController settings
    required property RecordingsController recordings
    required property string exerciseModel
    required property string themeMode
    required property bool reduceMotion
    property string screen_: "lab"
    property bool loginOpen: false
    onLoginOpenChanged: { lab.releaseRequested("SPACE"); if (!loginOpen) { access.clearMessage(); shell.forceActiveFocus() } }
    property ExerciseController exercise: null   // live; null: this build cannot run an exercise
    readonly property ExerciseController ex: mock ? mock.exercise : exercise
    property StudioController studio: null   // live; null: no Studio backend in this build
    readonly property StudioController phrases: mock ? mock.studio : studio
    property PlannerController planner: null   // live; null: no Planner backend in this build
    readonly property PlannerController plans: mock ? mock.planner : planner
    property ScenarioController scenarios: null   // live; null: this build has no Scenario Selection
    readonly property ScenarioController scens: mock ? mock.scenarios : scenarios
    property HistoryController history: null      // live; null: this build keeps no history
    readonly property HistoryController results: mock ? mock.history : history
    property SetupController setup: null          // live; null: only what the first Settings had
    readonly property SetupController setups: mock ? mock.setup : setup
    property string settingsPage: "recordings"
    property bool motionReduced: reduceMotion
    readonly property bool reconsentOpen: access.reconsent.length > 0 && !loginOpen && !exActive
    onReconsentOpenChanged: if (!reconsentOpen) shell.forceActiveFocus()
    property string trainTab: phrases !== null ? "phrases" : "exercises"
    readonly property bool exActive: ex !== null && ex.active
    readonly property bool exResult: ex !== null && ex.phase === ExerciseController.Result
    width: 1280; height: 800
    minimumWidth: 1280; minimumHeight: 800
    visible: false
    title: qsTr("Military ATC Training Simulator")
    color: Theme.bg

    Binding { target: Theme; property: "windowWidth"; value: root.width }
    Binding { target: Theme; property: "windowHeight"; value: root.height }
    Binding { target: Theme; property: "reduceMotion"; value: root.motionReduced }
    Component.onCompleted: Theme.mode = themeMode

    Binding { target: root.phrases; property: "trainerBusy"; value: root.exActive; when: root.phrases !== null }

    Binding { target: root.plans; property: "trainerBusy"; value: root.exActive; when: root.plans !== null }

    Binding { target: root.scens; property: "trainerBusy"; value: root.exActive; when: root.scens !== null }
    Binding { target: root.setups; property: "trainerBusy"; value: root.exActive; when: root.setups !== null }
    Connections {
        target: root.setups
        function onThemeChosen(mode) { Theme.mode = mode }
        function onMotionChosen(reduce) { root.motionReduced = reduce }
    }

    // Space is a fallback PTT; losing focus always releases it.
    onActiveChanged: if (!active) lab.releaseRequested("SPACE")

    DashboardController {
        id: dashCtl
        feed: root.feed
        stt: root.stt
        lab: root.lab
        exercise: root.ex
        pttFallback: root.settings.pttFallback
        exerciseModel: root.exerciseModel
    }
    onScreen_Changed: { lab.releaseRequested("SPACE"); dashCtl.cancelTests(); shell.forceActiveFocus() }
    Connections {
        target: root.mock
        function onCurrentChanged() {
            const c = root.mock.current
            if (c.startsWith("dash")) root.screen_ = "dash"
            else if (c.startsWith("phr")) { root.trainTab = "phrases"; root.screen_ = "train" }
            else if (c.startsWith("plan")) { root.trainTab = "exercises"; root.screen_ = "train" }
            else if (c.startsWith("scen")) root.screen_ = "scen"
            else if (c.startsWith("hist")) root.screen_ = "ass"
            else if (c.startsWith("set")) {
                root.settingsPage = c === "setinstr" || c === "setmark" ? "records" : c === "setoff" ? "recordings" : c.slice(3)
                root.screen_ = "set"
            }
            else if (c.startsWith("sav") || c.startsWith("re") || c === "login") root.screen_ = "lab"
            root.loginOpen = c === "login"
        }
    }

    Connections {
        target: root.ex
        function onScreenRequested(id) { root.screen_ = id }
    }

    Item {
        id: shell
        anchors.fill: parent
        focus: true
        Rectangle { anchors.fill: parent; color: Theme.bg }
        Keys.onPressed: (e) => {
            // In the Studio the keys record and play; no key transmits in Training Content.
            if (root.screen_ === "train" && !root.loginOpen && train.item && train.item.handleKey(e.key, e.isAutoRepeat)) e.accepted = true
            else if (e.key === Qt.Key_Space) { if (!e.isAutoRepeat && !root.loginOpen && (root.screen_ === "lab" || dashCtl.pttTesting || (root.screen_ === "ex" && root.ex.pttLive))) root.lab.pressRequested("SPACE"); e.accepted = true }
            else if (e.key === Qt.Key_M && root.mock) { panel.active = !panel.active; e.accepted = true }
        }
        Keys.onReleased: (e) => {
            if (e.key === Qt.Key_Space) { if (!e.isAutoRepeat) root.lab.releaseRequested("SPACE"); e.accepted = true }
        }

        StatusBar {
            id: status
            width: parent.width; height: Theme.px(48)
            chips: dashCtl.chips
            scenario: dashCtl.scenario.name || qsTr("No scenario")
            clockMode: root.setups && root.setups.clock ? root.setups.clock : "both"
            mock: root.mock !== null
            login: root.access.shortText; loginLong: root.access.longText
            loginEnabled: !root.exActive   // the trainee of an exercise does not change during it
            loggedIn: root.access.trainee || root.access.instructor
            onLoginAsked: root.loginOpen = true
            onLogoutAsked: if (!root.access.logout()) root.loginOpen = true   // the dialog says why
        }
        NavRail {
            id: nav
            readonly property string busy: qsTr("Not available during an exercise. Stop the exercise first.")
            z: 1  // its tooltip lies over the screen
            anchors { top: status.bottom; bottom: alert.top }
            width: Theme.px(88)
            current: root.screen_
            onActivated: (id) => root.screen_ = id
            entries: [
                { id: "dash", name: qsTr("Dashboard"), enabled: !root.exActive, off: true, why: nav.busy, icon: "M 3 3 L 9 3 L 9 9 L 3 9 Z M 11 3 L 17 3 L 17 9 L 11 9 Z M 3 11 L 9 11 L 9 17 L 3 17 Z M 11 11 L 17 11 L 17 17 L 11 17 Z" },
                { id: "scen", name: qsTr("Scenarios"), enabled: root.scens !== null && !root.exActive, off: root.scens !== null, why: root.exActive ? nav.busy : qsTr("The backend of this screen is not built yet. It opens here when it is."),
                  icon: "M 4 5 L 16 5 M 4 10 L 16 10 M 4 15 L 16 15" },
                { id: "ex", name: qsTr("Exercise"), enabled: root.exActive, off: root.ex !== null, why: root.mock ? qsTr("No exercise is running. Mock: press M and choose an Exercise case.") : root.ex ? qsTr("No exercise is running. Start one on the Dashboard.") : qsTr("This build cannot run an exercise."),
                  icon: "M 6 4 L 16 10 L 6 16 Z" },
                { id: "lab", name: qsTr("Speech Lab"), enabled: !root.exActive, off: true, why: nav.busy, icon: "M 7 3 L 13 3 L 13 11 L 7 11 Z M 4 9 L 4 14 L 16 14 L 16 9 M 10 14 L 10 18" },
                { id: "ass", name: qsTr("Assessment"), enabled: root.exResult || (root.results !== null && !root.exActive), off: root.ex !== null, why: root.exActive ? nav.busy : root.mock ? qsTr("No result yet. Mock: press M and choose a RESULT case.") : root.ex ? qsTr("No result yet. It opens after an exercise.") : qsTr("This build cannot run an exercise."),
                  icon: "M 4 16 L 4 10 M 10 16 L 10 4 M 16 16 L 16 8" },
                { id: "train", name: qsTr("Training"), enabled: train.built && !root.exActive, off: train.built, why: root.exActive ? nav.busy : qsTr("The backends of the Studio and the Planner are not built yet. They open here when they are."),
                  icon: "M 4 4 L 12 4 L 12 16 L 4 16 Z M 15 6 L 15 14 M 18 8 L 18 12" },
                { id: "set", name: qsTr("Settings"), enabled: !root.exActive, off: true, why: nav.busy,
                  icon: "M 3 6 L 17 6 M 13 4 L 13 8 M 3 14 L 17 14 M 7 12 L 7 16" }
            ]
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.screen_ === "lab"
            sourceComponent: Component { SpeechLab { lab: root.lab; stt: root.stt; recs: root.recordings } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.screen_ === "dash"
            sourceComponent: Component { Dashboard { dash: dashCtl; planner: root.plans; scenarios: root.scens !== null; results: root.results !== null
                                                    onSettingsAsked: root.screen_ = "set"; onScenariosAsked: root.screen_ = "scen"; onResultsAsked: root.screen_ = "ass" } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.ex !== null && (root.screen_ === "ex" || root.exActive)   // stays loaded during an exercise
            visible: root.screen_ === "ex"
            sourceComponent: Component { Exercise { lab: root.lab; ex: root.ex; access: root.access; canStart: root.mock !== null || dashCtl.startEnabled } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.ex !== null && root.screen_ === "ass" && root.exResult
            sourceComponent: Component { Result { ex: root.ex } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.results !== null && root.screen_ === "ass" && !root.exResult
            sourceComponent: Component { Assessment { history: root.results; onLoginAsked: root.loginOpen = true; onKeyboardFree: shell.forceActiveFocus() } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.scens !== null && root.screen_ === "scen"
            sourceComponent: Component { Scenarios { scenarios: root.scens; onDashboardAsked: root.screen_ = "dash"; onKeyboardFree: shell.forceActiveFocus() } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.screen_ === "set"
            sourceComponent: Component {
                Settings { access: root.access; settings: root.settings; dash: dashCtl; setup: root.setups; page: root.settingsPage
                           onLoginAsked: root.loginOpen = true; onKeyboardFree: shell.forceActiveFocus() }
            }
        }
        Loader {
            id: train
            readonly property bool built: root.phrases !== null || root.plans !== null
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: built && root.screen_ === "train"
            sourceComponent: root.trainTab === "phrases" ? studioScreen : plannerScreen
        }
        Component {
            id: studioScreen
            Studio { studio: root.phrases; exercisesOn: root.plans !== null; onTabAsked: (id) => root.trainTab = id
                     onLoginAsked: root.loginOpen = true; onKeyboardFree: shell.forceActiveFocus() }
        }
        Component {
            id: plannerScreen
            Planner { planner: root.plans; phrasesOn: root.phrases !== null; onTabAsked: (id) => root.trainTab = id; onDashboardAsked: root.screen_ = "dash"
                      onLoginAsked: root.loginOpen = true; onKeyboardFree: shell.forceActiveFocus() }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.loginOpen
            sourceComponent: Component { LoginDialog { access: root.access; onClosed: root.loginOpen = false } }
        }
        Loader {
            anchors { top: status.bottom; bottom: alert.top; left: nav.right; right: parent.right }
            active: root.reconsentOpen
            sourceComponent: Component { ReconsentDialog { access: root.access } }
        }
        AlertBanner {
            id: alert
            anchors.bottom: parent.bottom
            width: parent.width; height: Theme.px(32)
            level: dashCtl.alertLevel
            text: dashCtl.alert
            tag: root.ex && (root.screen_ === "ex" || root.screen_ === "ass") ? root.ex.preview : ""
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
            sourceComponent: Component { MockPanel { mock: root.mock; exercise: root.screen_ === "ex" || root.screen_ === "ass" } }
        }
    }
}
