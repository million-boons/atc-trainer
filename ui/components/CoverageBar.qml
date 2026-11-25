import QtQuick
import Atc

// Bottom of the Studio: coverage of one scenario, and the way to the next missing clip.
Rectangle {
    id: c
    property StudioController studio
    signal scenarioAsked()
    signal filterAsked()
    signal unusedAsked()
    signal touched()
    readonly property var filterNames: ({ all: qsTr("All clips"), "this": qsTr("This phrase"), pools: qsTr("Pool values"),
                                          initial: qsTr("Initial"), medial: qsTr("Medial"), "final": qsTr("Final") })
    color: Theme.surface
    border { color: Theme.border; width: Theme.line }
    radius: Theme.radius
    component B: BaseButton { font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus; anchors.verticalCenter: parent.verticalCenter }
    Row {
        x: Theme.s8; height: parent.height
        spacing: Theme.s8
        B { text: c.studio.scenario || qsTr("No scenario"); enabled: c.studio.scenarios.length > 1; implicitWidth: Math.max(Theme.px(120), contentItem.implicitWidth + 2 * Theme.s12); onClicked: c.scenarioAsked() }
        Label { objectName: "coverage"; height: parent.height; text: c.studio.coverage; mono: true; font.pixelSize: Theme.small }
        B { text: qsTr("Unused %1").arg(c.studio.unused.length); enabled: c.studio.unused.length > 0; width: Theme.px(88); onClicked: c.unusedAsked() }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s4
            Label { text: qsTr("Voice %1").arg(c.studio.voice); width: Theme.px(176); font.pixelSize: Theme.caption; color: Theme.textSecondary }
            Tag { visible: c.studio.placeholderVoice; text: qsTr("PLACEHOLDER VOICE") }
        }
    }
    Row {
        anchors { right: parent.right; rightMargin: Theme.s8 }
        height: parent.height
        spacing: Theme.s8
        B { text: c.filterNames[c.studio.filter] || c.studio.filter; width: Theme.px(104); onClicked: c.filterAsked() }
        B { text: qsTr("< Prev missing"); width: Theme.px(112); enabled: c.studio.missingCount > 0; onClicked: { c.touched(); c.studio.stepMissing(-1) } }
        B { text: qsTr("Next missing >"); width: Theme.px(112); enabled: c.studio.missingCount > 0; tint: Theme.accent; onClicked: { c.touched(); c.studio.stepMissing(1) } }
    }
}
