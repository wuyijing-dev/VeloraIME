import QtQuick

QtObject {
    property bool dark: false
    property bool highContrast: false
    property bool reduceMotion: false
    property bool securePad: false
    property real fontScale: 1

    readonly property color surfaceTint: dark ? "#2d3045" : (securePad ? "#f1e6de" : "#e7edf6")
    readonly property color solidSurface: highContrast ? (dark ? "#18191c" : "#f3f4f6") : (dark ? "#292b3d" : (securePad ? "#eee1d6" : "#dfe4ef"))
    readonly property real surfaceOpacity: dark ? 0.58 : 0.49
    readonly property color keyFill: highContrast ? (dark ? "#393b41" : "#ffffff") : (dark ? "#945c6078" : "#baffffff")
    readonly property color keyPressedFill: dark ? "#686d7c" : "#c6d2e3"
    readonly property color functionFill: highContrast ? (dark ? "#4c4f58" : "#dfe3ec") : (dark ? "#88505369" : "#87e8ecf0")
    readonly property color toolbarFill: dark ? "#655c6389" : "#85f7f9ff"
    readonly property color popupFill: dark ? "#303138" : "#f6f8fc"
    readonly property color rimHighlight: dark ? "#7fc5c9d8" : "#f5ffffff"
    readonly property color rimShadow: dark ? "#990a0a0b" : "#4b656d7c"
    readonly property color lensLight: dark ? "#a6baff" : (securePad ? "#fff1dc" : "#e8f7ff")
    readonly property color surfaceShadow: dark ? "#6c000000" : "#27626b7b"
    readonly property color keyRim: dark ? "#24f2f3ff" : "#70ffffff"
    readonly property color toolbarDivider: dark ? "#459da6cd" : "#4f9ca7bd"
    readonly property color contactShadow: dark ? "#50000000" : (securePad ? "#297b5e50" : "#24596985")
    readonly property color diffuseShadow: dark ? "#20000000" : (securePad ? "#127b5e50" : "#10596985")
    readonly property color keyBorder: highContrast ? (dark ? "#b0b6c4" : "#687181") : (dark ? "#61777c8b" : "#586e7888")
    readonly property color textPrimary: dark ? "#f7f8fc" : "#1c2029"
    readonly property color textSecondary: dark ? "#d2d6e0" : "#3d4757"
    readonly property color accent: dark ? "#4e73b6" : "#1267d4"
    readonly property color accentText: "#ffffff"
    readonly property color selectionFill: dark ? "#606b90" : "#cbdcf9"
    readonly property color focusRing: dark ? "#e0ecff" : "#174a93"
    readonly property real panelRadius: 34
    readonly property int motionDuration: reduceMotion ? 0 : 180
}
