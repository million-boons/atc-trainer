import QtQuick
import Atc

// On-screen digits for the PIN. key(string): "0" to "9"; erase(): last digit; clear(): all.
// Never takes the keyboard. Keys have the touch size and stay where they are.
Grid {
    id: k
    signal key(string digit)
    signal erase()
    signal clear()
    readonly property int keyW: Theme.px(84)
    readonly property int keyH: Theme.px(52)
    readonly property int fullW: 3 * keyW + 2 * spacing   // also while hidden
    readonly property int fullH: 4 * keyH + 3 * spacing
    columns: 3
    spacing: Theme.s8
    Repeater {
        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "clear", "0", "erase"]
        BaseButton {
            readonly property bool digit: modelData.length === 1
            width: k.keyW; height: k.keyH
            focusPolicy: Qt.NoFocus
            text: digit ? modelData : modelData === "clear" ? qsTr("Clear") : qsTr("Erase")
            font { family: digit ? Theme.mono : Theme.ui; pixelSize: digit ? Theme.h2 : Theme.small; weight: digit ? Font.DemiBold : Font.Normal }
            onClicked: digit ? k.key(modelData) : modelData === "clear" ? k.clear() : k.erase()
        }
    }
}
