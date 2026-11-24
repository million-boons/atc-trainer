import QtQuick
import Atc

// Second header inside a Card.
Item {
    property alias text: label.text
    height: Theme.px(36)
    Rectangle { width: parent.width; height: Theme.line; color: Theme.divider }
    Label { id: label; x: Theme.s12; width: parent.width - 2 * x; height: parent.height; color: Theme.textSecondary
            font { pixelSize: Theme.caption; weight: Font.DemiBold; letterSpacing: Theme.px(1) } }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.line; color: Theme.divider }
}
