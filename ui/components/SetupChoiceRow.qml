import QtQuick
import Atc

// One choice of the settings. Without permission: the reason instead of a button. asked(): the list of the options.
Item {
    id: r
    property var info: ({})           // {title, text, byUser, applies, may}
    signal asked()
    implicitHeight: Theme.px(60)
    Label { y: Theme.px(8); width: parent.width - b.width - Theme.s8; height: Theme.px(22); text: r.info.title || ""; font { pixelSize: Theme.small; weight: Font.DemiBold } }
    Label { y: Theme.px(30); width: parent.width - b.width - Theme.s8; height: Theme.px(20); color: Theme.textSecondary; font.pixelSize: Theme.caption
            text: [r.info.byUser ? qsTr("set by user") : qsTr("starting value"), r.info.applies ? qsTr("applies: %1").arg(r.info.applies) : "", r.info.may ? "" : qsTr("instructor only")].filter(x => x !== "").join("   ") }
    BaseButton { id: b; anchors { right: parent.right; verticalCenter: parent.verticalCenter } width: Math.min(Theme.px(320), r.width * 0.5); enabled: r.info.may === true
                 text: r.info.text || ""; font.pixelSize: Theme.small; focusPolicy: Qt.NoFocus; onClicked: r.asked() }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
}
