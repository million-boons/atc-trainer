import QtQuick
import Atc

// Mock controls: case and theme. Not part of the product design; loaded only with --mock.
Rectangle {
    id: mp
    property MockBackend mock
    property bool exercise: false   // Exercise cases instead of the Lab and Dashboard cases
    width: Theme.px(820); height: col.height + 2 * Theme.s12
    color: Theme.raised
    border { color: Theme.caution; width: 2 * Theme.line }
    radius: Theme.radius
    MouseArea { anchors.fill: parent }
    Column {
        id: col
        x: Theme.s12; y: Theme.s12; width: parent.width - 2 * x
        spacing: Theme.s8
        Label { text: qsTr("MOCK CONTROLS (M closes)"); color: Theme.caution; font.pixelSize: Theme.caption }
        Flow {
            width: parent.width
            spacing: Theme.px(6)
            Repeater {
                model: mp.exercise ? mp.mock.exerciseScripts : mp.mock.scripts
                BaseButton { text: modelData.name; font.pixelSize: Theme.caption
                             tint: mp.mock.current === modelData.id ? Theme.caution : Theme.border
                             focusPolicy: Qt.NoFocus
                             onClicked: mp.mock.run(modelData.id) }
            }
        }
        Row {
            spacing: Theme.px(6)
            BaseButton { text: mp.exercise ? qsTr("Show Lab and Dashboard cases") : qsTr("Show Exercise cases"); font.pixelSize: Theme.caption
                         tint: Theme.caution; focusPolicy: Qt.NoFocus; onClicked: mp.exercise = !mp.exercise }
            BaseButton { visible: mp.exercise; text: mp.mock.assessed ? qsTr("Mode: Assessed (preview)") : qsTr("Mode: Practice")
                         font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus; onClicked: mp.mock.assessed = !mp.mock.assessed }
            BaseButton { visible: mp.exercise; text: mp.mock.consent ? qsTr("Consent on record") : qsTr("No consent")
                         font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus; onClicked: mp.mock.consent = !mp.mock.consent }
        }
        Row {
            visible: !mp.exercise
            spacing: Theme.px(6)
            Label { text: qsTr("Live, pilot speaks:"); font.pixelSize: Theme.caption; height: Theme.touch }
            Repeater {
                model: ["interrupt", "block", "queue"]
                BaseButton { text: modelData; font.pixelSize: Theme.caption; focusPolicy: Qt.NoFocus
                             onClicked: mp.mock.livePilot(modelData) }
            }
        }
        Row {
            spacing: Theme.px(6)
            Repeater {
                model: [["dark", "Dark"], ["night", "Night"], ["hc", "High contrast"]]
                BaseButton { text: modelData[1]; font.pixelSize: Theme.caption
                             tint: Theme.mode === modelData[0] ? Theme.caution : Theme.border
                             focusPolicy: Qt.NoFocus
                             onClicked: Theme.mode = modelData[0] }
            }
        }
    }
}
