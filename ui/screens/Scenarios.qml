import QtQuick
import QtQuick.Templates as T
import Atc

// Scenario Selection (04 section 5d). View only. Steps show the intent, never the expected values.
Item {
    id: s
    property ScenarioController scenarios
    property string dialog            // "", difficulty, clips, errors
    signal dashboardAsked()
    signal keyboardFree()
    readonly property int gap: Theme.s8
    readonly property var d: scenarios.detail
    onDialogChanged: if (dialog === "") keyboardFree()
    Component.onCompleted: scenarios.enter()
    Connections { target: s.scenarios; function onSelected() { s.dashboardAsked() } }
    MouseArea { anchors.fill: parent; onPressed: s.keyboardFree() }

    // ---- list
    Card {
        id: list
        x: s.gap; y: s.gap; width: Theme.px(400); height: s.height - 2 * s.gap
        title: qsTr("SCENARIOS")
        headerItems: Label { anchors.right: parent.right; height: parent.height; text: s.scenarios.countText; mono: true; font.pixelSize: Theme.caption; color: Theme.textSecondary }
        Grid {
            id: cats
            x: Theme.s8; y: Theme.s8; width: parent.width - 2 * x
            columns: 5
            spacing: Theme.s4
            Repeater {
                model: s.scenarios.categories
                ChoiceButton { width: (cats.width - 4 * Theme.s4) / 5; text: modelData.name; enabled: !modelData.off; selected: modelData.selected; font.pixelSize: Theme.caption
                               Accessible.name: modelData.id; onClicked: { s.keyboardFree(); s.scenarios.setCategory(modelData.id) } }
            }
        }
        TextEntry { id: find; anchors { top: cats.bottom; topMargin: Theme.s4 } x: Theme.s8; width: parent.width - 2 * x - diff.width - Theme.s8; label: qsTr("Search"); maximumLength: 40
                    onTextChanged: s.scenarios.search(text); onAccepted: s.keyboardFree() }
        BaseButton { id: diff; anchors { right: parent.right; rightMargin: Theme.s8; bottom: find.bottom } width: Theme.px(156); font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus
                     text: s.scenarios.difficulty === "all" ? qsTr("All difficulties") : s.scenarios.difficulty; onClicked: s.dialog = "difficulty" }
        ListView {
            anchors { top: find.bottom; topMargin: Theme.s8; bottom: parent.bottom }
            width: parent.width
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: s.scenarios.rows
            delegate: T.Button {
                id: row
                width: ListView.view.width; height: Theme.px(80)
                focusPolicy: Qt.NoFocus
                onClicked: { s.keyboardFree(); s.scenarios.open(modelData.id) }
                Accessible.name: modelData.name
                background: Rectangle {
                    color: modelData.selected ? Theme.raised : "transparent"
                    Rectangle { visible: modelData.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                }
                contentItem: Item {
                    Label { x: Theme.s12; y: Theme.px(6); width: parent.width - tag.width - 3 * Theme.s12; height: Theme.px(24); text: modelData.name; font.weight: Font.DemiBold }
                    Tag { id: tag; visible: modelData.placeholder; anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(7); text: modelData.tag }
                    Label { visible: !modelData.placeholder; anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.px(7); height: Theme.px(22); text: modelData.tag; font { pixelSize: Theme.caption; weight: Font.DemiBold } }
                    Label { x: Theme.s12; y: Theme.px(30); width: parent.width - 2 * x; height: Theme.px(20); text: modelData.sub; color: Theme.textSecondary; font.pixelSize: Theme.small }
                    StateIcon { id: st; x: Theme.s12; y: Theme.px(56); level: modelData.level }
                    Label { objectName: "scenarioState"; x: st.visible ? st.x + st.width + Theme.s4 : Theme.s12; y: Theme.px(52); width: parent.width - x - Theme.s8; height: Theme.px(20); text: modelData.state; font.pixelSize: Theme.caption }
                }
            }
        }
        Label { anchors.centerIn: parent; visible: s.scenarios.ready && s.scenarios.rows.length === 0; text: qsTr("No scenario matches."); color: Theme.textSecondary }
    }

    // ---- detail
    Card {
        id: det
        x: list.x + list.width + s.gap; y: s.gap; width: s.width - x - s.gap; height: list.height
        title: s.d.id || qsTr("SCENARIO")
        headerItems: [
            Tag { visible: s.d.placeholder === true; anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: qsTr("PLACEHOLDER") },
            Label { visible: s.d.placeholder === false; anchors.right: parent.right; height: parent.height; text: s.d.tag || ""; font { pixelSize: Theme.caption; weight: Font.DemiBold } }
        ]
        Label { anchors.centerIn: parent; visible: !s.scenarios.hasDetail; text: s.scenarios.message || qsTr("Loading"); color: Theme.textSecondary }
        Item {
            visible: s.scenarios.hasDetail
            x: Theme.s12; y: Theme.s8; width: parent.width - 2 * x; height: parent.height - 2 * y
            Label { id: nm; width: parent.width; height: Theme.px(32); text: s.d.name || ""; font { pixelSize: Theme.h1; weight: Font.DemiBold } }
            Label { id: info; objectName: "scenarioInfo"; anchors.top: nm.bottom; width: parent.width; height: Theme.px(24); text: s.d.info || ""; color: Theme.textSecondary; font.pixelSize: Theme.small }
            SectionHeader { id: obH; anchors { top: info.bottom; topMargin: Theme.s4 } x: -Theme.s12; width: parent.width + 2 * Theme.s12; text: qsTr("OBJECTIVES") }
            ListView {
                id: ob
                anchors { top: obH.bottom; topMargin: Theme.s4 }
                width: parent.width; height: Theme.px(66)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.scenarios.objectives
                delegate: Label { width: ListView.view.width; height: Theme.px(22); text: modelData; font.pixelSize: Theme.small }
            }
            SectionHeader { id: stH; anchors { top: ob.bottom; topMargin: Theme.s4 } x: -Theme.s12; width: parent.width + 2 * Theme.s12; text: s.d.stepsTitle || "" }
            Label { anchors { right: parent.right; top: stH.top } height: stH.height; text: s.d.rule || ""; font.pixelSize: Theme.small }
            ListView {
                anchors { top: stH.bottom; bottom: vaH.top }
                width: parent.width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: s.scenarios.steps
                delegate: Item {
                    width: ListView.view.width; height: Theme.px(34)
                    Label { width: Theme.px(28); height: parent.height; text: modelData.no; mono: true; color: Theme.textSecondary }
                    Label { x: Theme.px(28); width: Theme.px(180); height: parent.height; text: modelData.name; font.weight: Font.DemiBold }
                    Label { x: Theme.px(212); width: Theme.px(300); height: parent.height; text: modelData.intent; mono: true; font.pixelSize: Theme.small }
                    Label { anchors.right: parent.right; height: parent.height; text: modelData.wait; color: Theme.textSecondary; font.pixelSize: Theme.small }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
                }
            }
            Label { visible: s.scenarios.stepsHidden; anchors { top: stH.bottom; topMargin: Theme.s8 } text: qsTr("Steps: instructor only. This scenario may be used Assessed."); color: Theme.textSecondary; font.pixelSize: Theme.small }
            SectionHeader { id: vaH; anchors.bottom: va.top; x: -Theme.s12; width: parent.width + 2 * Theme.s12; text: qsTr("VALUES (drawn from the pools)") }
            Column {
                id: va
                anchors { bottom: bankR.top; bottomMargin: Theme.s4 }
                width: parent.width
                Label { width: parent.width; height: Theme.px(26); text: s.d.values || ""; mono: true; font.pixelSize: Theme.small }
                Label { width: parent.width; height: Theme.px(22); text: qsTr("Fixed values: in an exercise plan"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
            }
            Item {
                id: bankR
                anchors { bottom: asR.top }
                width: parent.width; height: Theme.touch
                Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
                StateIcon { id: bi; level: s.d.bankLevel || 0; anchors.verticalCenter: parent.verticalCenter }
                Label { objectName: "scenarioBank"; x: Theme.px(20); width: parent.width - x - listBtn.width - Theme.s8; height: parent.height; text: s.d.bank || ""; font.pixelSize: Theme.small }
                SecondaryButton { id: listBtn; anchors.right: parent.right; text: qsTr("List"); width: Theme.px(92); focusPolicy: Qt.NoFocus; font.pixelSize: Theme.small
                                  enabled: s.d.hasMissing === true || s.d.hasErrors === true; onClicked: s.dialog = s.d.hasErrors ? "errors" : "clips" }
            }
            Item {
                id: asR
                anchors { bottom: msgR.top }
                width: parent.width; height: Theme.px(28)
                StateIcon { level: s.d.assessedLevel || 0; anchors.verticalCenter: parent.verticalCenter }
                Label { x: Theme.px(20); width: parent.width - x; height: parent.height; text: s.d.assessed || ""; font.pixelSize: Theme.small }
            }
            Item {
                id: msgR
                anchors { bottom: btns.top; bottomMargin: Theme.s4 }
                width: parent.width; height: Theme.px(24)
                StateIcon { level: s.scenarios.messageLevel; anchors.verticalCenter: parent.verticalCenter }
                Label { x: Theme.px(20); width: parent.width - x; height: parent.height; text: s.scenarios.message; font.pixelSize: Theme.small }
            }
            Row {
                id: btns
                anchors.bottom: parent.bottom
                spacing: Theme.s8
                PrimaryButton { text: qsTr("Select scenario"); enabled: s.scenarios.canSelect; focusPolicy: Qt.NoFocus; onClicked: s.scenarios.select() }
                SecondaryButton { text: qsTr("Back to Dashboard"); focusPolicy: Qt.NoFocus; onClicked: s.dashboardAsked() }
            }
        }
    }

    // ---- dialogs
    Loader {
        anchors.fill: parent
        active: s.dialog === "difficulty"
        sourceComponent: Component {
            PickDialog { title: qsTr("Difficulty"); chosenId: s.scenarios.difficulty
                         options: [{ id: "all", text: qsTr("All difficulties") }, { id: "basic", text: "basic" }, { id: "intermediate", text: "intermediate" }, { id: "advanced", text: "advanced" }]
                         onPicked: (id) => { s.dialog = ""; s.scenarios.setDifficulty(id) }
                         onClosed: s.dialog = "" }
        }
    }
    Loader {
        anchors.fill: parent
        active: s.dialog === "clips" || s.dialog === "errors"
        sourceComponent: Component {
            ConfirmDialog { title: s.dialog === "clips" ? qsTr("Missing clips") : qsTr("The scenario cannot be loaded")
                            rows: s.dialog === "clips" ? s.scenarios.missing : s.scenarios.errors
                            closeText: qsTr("Close"); onClosed: s.dialog = "" }
        }
    }
}
