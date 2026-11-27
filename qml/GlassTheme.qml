import QtQuick

QtObject {
    property bool dark: false
    property bool highContrast: false
    property bool reduceMotion: false
    property real fontScale: 1

    readonly property color surfaceTint: dark ? "#222327" : "#f2f4f7"
    readonly property color solidSurface: highContrast ? (dark ? "#18191c" : "#f3f4f6") : (dark ? "#2b2c31" : "#e7e9ee")
    readonly property real surfaceOpacity: dark ? 0.65 : 0.54
    readonly property color keyFill: highContrast ? (dark ? "#393b41" : "#ffffff") : (dark ? "#ad3b3d44" : "#aefeffff")
    readonly property color keyPressedFill: dark ? "#686d7c" : "#c6d2e3"
    readonly property color functionFill: highContrast ? (dark ? "#4c4f58" : "#dfe3ec") : (dark ? "#bd303239" : "#9ad8dde7")
    readonly property color toolbarFill: dark ? "#a3363941" : "#9cfcfdff"
    readonly property color popupFill: dark ? "#303138" : "#f6f8fc"
    readonly property color rimHighlight: dark ? "#7fc5c9d8" : "#f5ffffff"
    readonly property color rimShadow: dark ? "#990a0a0b" : "#4b656d7c"
    readonly property color surfaceShadow: dark ? "#6c000000" : "#27626b7b"
    readonly property color keyBorder: highContrast ? (dark ? "#b0b6c4" : "#687181") : (dark ? "#61777c8b" : "#586e7888")
    readonly property color textPrimary: dark ? "#f7f8fc" : "#1c2029"
    readonly property color textSecondary: dark ? "#d2d6e0" : "#3d4757"
    readonly property color accent: dark ? "#9dccff" : "#185bad"
    readonly property color accentText: dark ? "#12243a" : "#ffffff"
    readonly property color selectionFill: dark ? "#505f75" : "#dce8f9"
    readonly property color focusRing: dark ? "#e0ecff" : "#174a93"
    readonly property real panelRadius: 29
    readonly property int motionDuration: reduceMotion ? 0 : 180
}
