import QtQuick

QtObject {
    property bool dark: false
    property bool highContrast: false
    property bool reduceMotion: false
    property real fontScale: 1

    readonly property color surfaceTint: dark ? "#172233" : "#e7f1fa"
    readonly property color solidSurface: dark ? "#1f2b3c" : "#e8eff7"
    readonly property real surfaceOpacity: dark ? 0.84 : 0.83
    readonly property color keyFill: dark ? "#bc3b4d63" : "#c9ffffff"
    readonly property color keyPressedFill: dark ? "#647e9d" : "#b7d4f2"
    readonly property color functionFill: dark ? "#c2475c76" : "#bfd7e8f9"
    readonly property color popupFill: dark ? "#283b53" : "#f0f6ff"
    readonly property color rimHighlight: dark ? "#778facc7" : "#eaffffff"
    readonly property color rimShadow: dark ? "#a5071019" : "#5367819f"
    readonly property color keyBorder: highContrast ? (dark ? "#a9c6e8" : "#586983") : (dark ? "#705c7899" : "#71788fad")
    readonly property color textPrimary: dark ? "#f7fbff" : "#172a41"
    readonly property color textSecondary: dark ? "#ddeafd" : "#364e69"
    readonly property color accent: dark ? "#a6d5ff" : "#183d6e"
    readonly property color accentText: dark ? "#101f31" : "#ffffff"
    readonly property color focusRing: dark ? "#dfedff" : "#123e82"
    readonly property int panelRadius: 24
    readonly property int motionDuration: reduceMotion ? 0 : 180
}
