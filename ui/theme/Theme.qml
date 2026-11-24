pragma Singleton
import QtQuick

// All design tokens. No other file holds colours or raw sizes.
QtObject {
    property string mode: "dark"          // dark | night | hc
    property bool reduceMotion: false
    property real windowWidth: 1280
    property real windowHeight: 800

    // D-14: 1.0 at 800 px screen height. The width limit stops a narrow screen overflowing.
    readonly property real scale: Math.max(1, Math.min(windowHeight / 800, windowWidth / 1280))
    function px(n) { return Math.round(n * scale) }

    readonly property var palettes: ({
        dark:  { bg: "#0B0F14", surface: "#121821", raised: "#19212C", border: "#243041", divider: "#1C2633",
                 textPrimary: "#E6EDF3", textSecondary: "#9AA7B4", textMuted: "#8592A0", accent: "#38BDF8",
                 tx: "#F59E0B", rx: "#22D3EE", txFill: "#F59E0B", rxFill: "#22D3EE", onFill: "#0B0F14",
                 ok: "#22C55E", caution: "#EAB308", fault: "#F87171", laneAtc: "#151F2B", lanePilot: "#13242D" },
        night: { bg: "#060708", surface: "#0B0D10", raised: "#101318", border: "#1D232B", divider: "#151A20",
                 textPrimary: "#A8B0B8", textSecondary: "#8A929A", textMuted: "#7B838B", accent: "#2F93BC",
                 tx: "#C07C0A", rx: "#1B9DB0", txFill: "#D68B0E", rxFill: "#1FABBF", onFill: "#060708",
                 ok: "#22994D", caution: "#AE860A", fault: "#D85A5A", laneAtc: "#0D1217", lanePilot: "#0C1518" },
        hc:    { bg: "#000000", surface: "#000000", raised: "#101010", border: "#A0A8B0", divider: "#5A626A",
                 textPrimary: "#FFFFFF", textSecondary: "#E0E6EC", textMuted: "#C0C8D0", accent: "#5CCBFF",
                 tx: "#FFB020", rx: "#3DE8FF", txFill: "#FFB020", rxFill: "#3DE8FF", onFill: "#000000",
                 ok: "#4ADE80", caution: "#FFE04A", fault: "#FF8A8A", laneAtc: "#07121A", lanePilot: "#04161A" }
    })
    readonly property var p: palettes[mode] || palettes.dark

    readonly property color bg: p.bg
    readonly property color surface: p.surface
    readonly property color raised: p.raised
    readonly property color border: p.border
    readonly property color divider: p.divider
    readonly property color textPrimary: p.textPrimary
    readonly property color textSecondary: p.textSecondary
    readonly property color textMuted: p.textMuted
    readonly property color accent: p.accent
    readonly property color tx: p.tx
    readonly property color rx: p.rx
    readonly property color txFill: p.txFill
    readonly property color rxFill: p.rxFill
    readonly property color onFill: p.onFill
    readonly property color ok: p.ok
    readonly property color caution: p.caution
    readonly property color fault: p.fault
    readonly property color laneAtc: p.laneAtc       // transcript entries
    readonly property color lanePilot: p.lanePilot

    // Status levels, same order as LabController.Level: None, Ok, Caution, Fault.
    function levelColor(l) { return [textMuted, ok, caution, fault][l] }
    readonly property var levelIcon: ["", "M 2 6.5 L 5 9.5 L 10 3",
        "M 6 1 L 11.3 11 L 0.7 11 Z M 6 4.6 L 6 7.6 M 6 9.2 L 6 9.4", "M 2 2 L 10 10 M 10 2 L 2 10"]

    readonly property string iconNow: "M 3 1.5 L 9.5 6 L 3 10.5 Z"   // also: play
    readonly property string iconPending: "M 6 1.5 L 9.2 2.8 L 10.5 6 L 9.2 9.2 L 6 10.5 L 2.8 9.2 L 1.5 6 L 2.8 2.8 Z"

    readonly property string ui: "Inter"
    readonly property string mono: "JetBrains Mono"
    readonly property int display: px(32)
    readonly property int h1: px(24)
    readonly property int h2: px(18)
    readonly property int body: px(15)
    readonly property int small: px(13)
    readonly property int caption: px(12)

    readonly property int s4: px(4)
    readonly property int s8: px(8)
    readonly property int s12: px(12)
    readonly property int s16: px(16)
    readonly property int s24: px(24)
    readonly property int radius: px(6)
    readonly property int radiusChip: px(4)
    readonly property int line: Math.max(1, Math.round(scale))
    readonly property int touch: px(48)
    readonly property int motion: reduceMotion ? 0 : 120
}
