import QtQuick
import QtQuick.Templates as T
import Atc

// Speech model: [dropdown] [Load] status. readOnly: name and status only (exercise, D-39).
// Never takes keyboard focus, so Space stays a PTT key.
Item {
    id: ms
    property SttController stt
    property bool readOnly: false

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        spacing: Theme.px(6)

        T.Button {
            id: drop
            visible: !ms.readOnly
            width: ms.width - load.width - status.width - 2 * row.spacing
            height: Theme.touch
            enabled: !ms.stt.locked && ms.stt.models.length > 0
            focusPolicy: Qt.NoFocus
            onClicked: popup.visible ? popup.close() : popup.open()
            Accessible.name: qsTr("Speech model: %1").arg(ms.stt.selectedName)
            background: Rectangle {
                radius: Theme.radius
                color: drop.down || popup.visible ? Theme.border : Theme.raised
                border { color: popup.visible ? Theme.accent : Theme.border; width: Theme.line }
            }
            contentItem: Item {
                Label { x: Theme.s8; y: Theme.px(4); text: qsTr("Model"); color: Theme.textSecondary; font.pixelSize: Theme.caption }
                Label { x: Theme.s8; y: Theme.px(22); width: parent.width - Theme.px(34)
                        text: ms.stt.selectedName; font.pixelSize: Theme.small }
                IconItem { width: Theme.px(14); height: width; x: parent.width - width - Theme.s8
                           anchors.verticalCenter: parent.verticalCenter
                           path: "M 4 7 L 10 13 L 16 7"; color: Theme.textSecondary }
            }
        }
        SecondaryButton {
            id: load
            visible: !ms.readOnly
            text: qsTr("Load")
            width: Theme.px(62)
            font.pixelSize: Theme.small
            focusPolicy: Qt.NoFocus
            enabled: ms.stt.loadEnabled
            onClicked: { popup.close(); ms.stt.load() }
        }
        Item {
            id: status
            width: ms.readOnly ? Math.min(ms.width, Theme.px(260)) : Theme.px(104); height: Theme.touch
            StateIcon { id: ic; level: ms.stt.level; anchors.verticalCenter: st.verticalCenter }
            Label { id: st; x: ic.width + Theme.px(5); y: sub.text ? (parent.height - height - sub.height) / 2 : (parent.height - height) / 2
                    text: ms.stt.stateText; font { pixelSize: Theme.caption; weight: Font.DemiBold } }
            Label { id: sub; objectName: "loadedModel"; y: st.y + st.height; width: parent.width; font.pixelSize: Theme.caption; color: Theme.textSecondary
                    wrapMode: Text.WordWrap; maximumLineCount: 2; lineHeight: 0.95   // the whole name (D-126)
                    // Which model is loaded, when it is not the one in the dropdown.
                    text: ms.stt.loadedName && (ms.readOnly || ms.stt.loadedName !== ms.stt.selectedName) ? ms.stt.loadedName : "" }
        }
    }

    T.Popup {
        id: popup
        x: 0; y: ms.height
        width: Theme.px(470); height: list.contentHeight + 2 * Theme.line
        focus: false
        padding: Theme.line
        closePolicy: T.Popup.CloseOnPressOutsideParent
        background: Rectangle { color: Theme.raised; radius: Theme.radius; border { color: Theme.accent; width: Theme.line } }
        contentItem: ListView {
            id: list
            clip: true
            interactive: false
            model: ms.stt.models
            delegate: T.ItemDelegate {
                id: d
                width: list.width; height: Theme.px(56)
                enabled: modelData.installed
                focusPolicy: Qt.NoFocus
                onClicked: { ms.stt.select(index); popup.close() }
                background: Rectangle { color: d.down ? Theme.border : "transparent"
                    Rectangle { visible: index === ms.stt.selected; width: 3 * Theme.line; height: parent.height; color: Theme.accent }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider } }
                contentItem: Item {
                    Label { x: Theme.s12; y: Theme.s8; text: modelData.name; font.pixelSize: Theme.small
                            color: d.enabled ? Theme.textPrimary : Theme.textMuted }
                    Label { anchors { right: parent.right; rightMargin: Theme.s12 } y: Theme.s8; visible: !d.enabled
                            text: qsTr("not installed"); font { pixelSize: Theme.caption; italic: true } color: Theme.textMuted }
                    Label { x: Theme.s12; y: Theme.px(29); width: parent.width - 2 * x; text: modelData.note
                            font.pixelSize: Theme.caption; color: d.enabled ? Theme.textSecondary : Theme.textMuted }
                }
            }
        }
    }
    function closeList() { popup.close() }
}
