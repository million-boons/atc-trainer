import QtQuick
import Atc

// Mock controls: case and theme. Not part of the product design; loaded only with --mock.
Rectangle {
    id: mp
    property MockBackend mock
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
                model: mp.mock.scripts
                BaseButton { text: modelData.name; font.pixelSize: Theme.caption
                             tint: mp.mock.current === modelData.id ? Theme.caution : Theme.border
                             focusPolicy: Qt.NoFocus
                             onClicked: mp.mock.run(modelData.id) }
            }
        }
        Row {
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
