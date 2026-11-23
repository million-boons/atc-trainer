import QtQuick
import Atc

// Bordered surface. title: header text (empty = no header). headerItems: right of the title.
Rectangle {
    id: card
    property string title: ""
    property int headerHeight: Theme.px(36)
    property alias headerItems: headerSlot.data
    default property alias content: body.data
    color: Theme.surface
    border { color: Theme.border; width: Theme.line }
    radius: Theme.radius

    Item {
        id: header
        visible: card.title !== ""
        width: parent.width
        height: visible ? card.headerHeight : 0
        Label {
            id: titleText
            x: Theme.s12
            height: parent.height
            text: card.title
            color: Theme.textSecondary
            font { pixelSize: Theme.caption; weight: Font.DemiBold; letterSpacing: Theme.px(1) }
        }
        Item {
            id: headerSlot
            anchors { left: titleText.right; leftMargin: Theme.s12; right: parent.right; rightMargin: Theme.s12
                      top: parent.top; bottom: parent.bottom }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            x: Theme.line; width: parent.width - 2 * Theme.line; height: Theme.line
            color: Theme.divider
        }
    }
    Item {
        id: body
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
                  margins: Theme.line }
        clip: true
    }
}
