import QtQuick
import Atc

// The segments of the text above it. Scrolls by itself, so that a long text never moves the rest.
Flickable {
    id: f
    property StudioController studio
    contentHeight: flow.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    signal touched()
    Flow {
        id: flow
        width: f.width
        spacing: Theme.s8
        Repeater {
            model: f.studio.segments
            Row {
                spacing: Theme.s4
                SegmentChip { words: modelData.text; dynamic: modelData.dynamic; position: modelData.position; clipState: modelData.state
                              selected: index === f.studio.segment
                              onClicked: { f.touched(); f.studio.selectSegment(index) } }
                Label { visible: modelData.comma; text: ","; mono: true; height: Theme.px(52); color: Theme.textSecondary }
            }
        }
    }
}
