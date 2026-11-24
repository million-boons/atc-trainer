import QtQuick
import Atc

// Modal panel. rows: optional list of lines. confirmText empty: only the close button.
Item {
    id: dlg
    property string title
    property string text
    property var rows: []
    property string confirmText
    property string closeText: qsTr("Cancel")
    signal confirmed()
    signal closed()
    Rectangle { anchors.fill: parent; color: Theme.bg; opacity: 0.8 }
    MouseArea { anchors.fill: parent }
    Rectangle {
        anchors.centerIn: parent
        width: Theme.px(560); height: col.height + 2 * Theme.s24
        radius: Theme.radius
        color: Theme.surface
        border { color: Theme.border; width: Theme.line }
        Column {
            id: col
            x: Theme.s24; y: Theme.s24; width: parent.width - 2 * x
            spacing: Theme.s12
            Label { width: parent.width; text: dlg.title; font { pixelSize: Theme.h1; weight: Font.DemiBold } }
            Label { width: parent.width; visible: text !== ""; text: dlg.text; wrapMode: Text.WordWrap }
            ListView {
                width: parent.width
                visible: count > 0
                height: Math.min(contentHeight, Theme.px(280))
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: dlg.rows
                delegate: Label { width: ListView.view.width; height: Theme.px(28); mono: true
                                  font.pixelSize: Theme.small; text: modelData }
            }
            Row {
                spacing: Theme.s12
                DangerButton { visible: dlg.confirmText !== ""; text: dlg.confirmText; focusPolicy: Qt.NoFocus
                               onClicked: dlg.confirmed() }
                SecondaryButton { text: dlg.closeText; focusPolicy: Qt.NoFocus; onClicked: dlg.closed() }
            }
        }
    }
}
