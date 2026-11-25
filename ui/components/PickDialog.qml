import QtQuick
import Atc

// Choose one of a list. options: list of {id, text, note, off}. picked(id).
ModalPanel {
    id: pick
    property var options: []
    property string chosenId
    signal picked(string id)
    signal closed()
    ListView {
        width: parent.width; height: Math.min(contentHeight, Theme.px(320))
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: Theme.s4
        model: pick.options
        delegate: ChoiceButton { width: ListView.view.width; text: modelData.note ? modelData.text + "   " + modelData.note : modelData.text
                                 enabled: !modelData.off; selected: modelData.id === pick.chosenId; onClicked: pick.picked(modelData.id) }
    }
    SecondaryButton { text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: pick.closed() }
}
