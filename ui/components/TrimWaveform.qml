import QtQuick
import Atc

// Raw take with the kept part. Outside it: dimmed. The handles can be dragged while the take is pending.
Item {
    id: w
    property var peaks: []
    property real from: 0
    property real to: 1
    property bool editable: false
    signal trimmed(real from, real to)
    Rectangle { anchors.fill: parent; color: Theme.bg; radius: Theme.radiusChip; border { color: Theme.border; width: Theme.line } }
    PeaksItem { anchors.fill: parent; peaks: w.peaks; color: Theme.textSecondary }
    Rectangle { x: Theme.line; y: Theme.line; width: Math.max(0, lo.x + lo.width / 2 - x); height: parent.height - 2 * y; color: Theme.bg; opacity: 0.75 }
    Rectangle { x: hi.x + hi.width / 2; y: Theme.line; width: Math.max(0, parent.width - x - Theme.line); height: parent.height - 2 * y; color: Theme.bg; opacity: 0.75 }
    component Handle: Item {
        id: h
        property real at: 0
        property alias dragging: ma.pressed
        signal moved()
        width: Theme.touch; height: parent.height   // the touch target; the line is in its middle
        x: w.width * at - width / 2
        onAtChanged: if (!ma.pressed) x = w.width * at - width / 2
        function frac() { return (x + width / 2) / w.width }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 2 * Theme.line; height: parent.height; color: w.editable ? Theme.accent : Theme.textMuted }
        Rectangle { anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom } visible: w.editable
                    width: Theme.px(12); height: width; radius: width / 2; color: Theme.accent }
        MouseArea { id: ma; anchors.fill: parent; enabled: w.editable; preventStealing: true
                    drag { target: h; axis: Drag.XAxis; minimumX: -h.width / 2; maximumX: w.width - h.width / 2; threshold: 0 }
                    onReleased: h.moved() }
        Connections { target: w; function onWidthChanged() { h.x = w.width * h.at - h.width / 2 }
                      function onEditableChanged() { h.x = w.width * h.at - h.width / 2 } }   // also after a refused trim
    }
    Handle { id: lo; at: w.from; onMoved: w.trimmed(frac(), hi.frac()) }
    Handle { id: hi; at: w.to; onMoved: w.trimmed(lo.frac(), frac()) }
}
