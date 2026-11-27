pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property GlassTheme theme
    property Item backgroundSource: null
    property bool reduceTransparency: false
    property int graphicsMode: 0

    readonly property bool canSample: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown && GraphicsInfo.api !== GraphicsInfo.Null
    readonly property bool sampleBackground: visible && canSample && !reduceTransparency && !theme.highContrast && graphicsMode !== 3 && backgroundSource !== null && sourceIsIndependent() && backgroundSource.width > 0 && backgroundSource.height > 0 && width > 0 && height > 0
    readonly property bool lightMaterial: graphicsMode === 2

    function sourceIsIndependent(): bool {
        for (let item = root; item; item = item.parent) {
            if (item === root.backgroundSource)
                return false;
        }
        return true;
    }

    // Mapping happens in scene coordinates. Changes in either item's geometry
    // also resample when the host repositions the keyboard.
    function captureRect(): rect {
        if (!root.backgroundSource)
            return Qt.rect(0, 0, 1, 1);

        const source = root.backgroundSource;
        const anchor = root.mapToItem(source, 0, 0);
        const far = root.mapToItem(source, root.width, root.height);
        return Qt.rect(anchor.x, anchor.y, Math.max(1, far.x - anchor.x), Math.max(1, far.y - anchor.y));
    }

    Rectangle {
        x: -1
        y: 3
        width: parent.width + 2
        height: parent.height
        radius: root.theme.panelRadius + 1
        color: root.theme.surfaceShadow
        antialiasing: true
        visible: !root.theme.highContrast
    }

    Rectangle {
        anchors.fill: parent
        radius: root.theme.panelRadius
        color: root.theme.solidSurface
        visible: !root.sampleBackground
        antialiasing: true
    }

    Loader {
        active: root.sampleBackground
        anchors.fill: parent
        sourceComponent: Item {
            id: sampledMaterial

            readonly property rect sampleRect: {
                // Register geometry of the source and of intermediate hosts.
                for (let item = root.parent; item; item = item.parent) {
                    const position = item.x + item.y + item.width + item.height;
                    if (!isFinite(position))
                        return Qt.rect(0, 0, 1, 1);
                }
                for (let item = root.backgroundSource; item; item = item.parent) {
                    const position = item.x + item.y + item.width + item.height;
                    if (!isFinite(position))
                        return Qt.rect(0, 0, 1, 1);
                }
                return root.captureRect();
            }

            ShaderEffectSource {
                id: sceneTexture
                anchors.fill: parent
                sourceItem: root.backgroundSource
                sourceRect: sampledMaterial.sampleRect
                textureSize: Qt.size(Math.max(1, Math.min(1024, Math.ceil(width / (root.lightMaterial ? 4 : 2)))), Math.max(1, Math.min(512, Math.ceil(height / (root.lightMaterial ? 4 : 2)))))
                visible: false
                smooth: true
            }

            ShaderEffect {
                anchors.fill: parent
                property var source: sceneTexture
                property size panelSize: Qt.size(width, height)
                property color tint: root.theme.surfaceTint
                property real tintStrength: root.theme.surfaceOpacity
                property real cornerRadius: Math.min(root.theme.panelRadius, Math.min(width, height) / 2)
                property real refraction: root.lightMaterial ? 0 : 9
                property real blurRadius: root.lightMaterial ? 7 : 14
                fragmentShader: "qrc:/qt/qml/Rk/TouchKeyboard/shaders/GlassPanel.frag.qsb"
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.theme.panelRadius
        color: "transparent"
        border.width: root.theme.highContrast ? 2 : 1
        border.color: root.theme.highContrast ? root.theme.keyBorder : root.theme.rimShadow
        antialiasing: true
    }

    Rectangle {
        visible: !root.theme.highContrast
        anchors.fill: parent
        anchors.margins: 1
        radius: root.theme.panelRadius - 1
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.theme.rimHighlight
            }
            GradientStop {
                position: 0.055
                color: "transparent"
            }
            GradientStop {
                position: 0.88
                color: "transparent"
            }
            GradientStop {
                position: 1
                color: root.theme.rimShadow
            }
        }
        opacity: root.sampleBackground ? 0.24 : (root.theme.dark ? 0.42 : 0.62)
        antialiasing: true
    }
}
