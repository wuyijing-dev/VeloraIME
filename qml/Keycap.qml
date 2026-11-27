pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    required property GlassTheme theme
    property string label: ""
    property string gestureLetter: ""
    property string detail: ""
    property string icon: ""
    property string accessibleLabel: label
    property bool secondary: false
    property bool accent: false
    property bool selected: false
    property bool transparentRest: false
    property bool repeats: false
    property bool hasAlternates: false
    property bool cursorDrag: false
    property bool preferScroll: false
    property bool highlighted: false
    property real visualGap: 4
    property real fontSize: secondary ? 15 : 22
    property real visualRadius: 12

    signal activated
    signal held
    signal holdMoved(point scenePosition)
    signal holdReleased
    signal cursorMoved(int steps)

    implicitWidth: 44
    implicitHeight: 44
    opacity: enabled ? 1 : 0.52

    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel
    Accessible.checkable: selected
    Accessible.checked: selected
    Accessible.onPressAction: {
        if (root.enabled)
            root.activated();
    }

    Rectangle {
        id: cap
        anchors.fill: parent
        anchors.leftMargin: root.visualGap / 2
        anchors.rightMargin: root.visualGap / 2
        anchors.topMargin: root.visualGap / 2
        anchors.bottomMargin: root.visualGap / 2
        radius: root.visualRadius
        antialiasing: true
        color: tap.pressed || root.highlighted || drag.active ? root.theme.keyPressedFill : (root.accent || root.selected ? root.theme.accent : (root.transparentRest ? "transparent" : (root.secondary ? root.theme.functionFill : root.theme.keyFill)))
        border.color: root.highlighted || root.activeFocus ? root.theme.focusRing : root.theme.keyBorder
        border.width: root.highlighted || root.activeFocus || root.theme.highContrast ? 2 : (root.transparentRest && !tap.pressed ? 0 : 1)

        Behavior on color {
            enabled: !tap.pressed && root.theme.motionDuration > 0
            ColorAnimation {
                duration: Math.round(root.theme.motionDuration * 0.65)
            }
        }

        Rectangle {
            visible: !root.transparentRest && !root.theme.highContrast
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            opacity: tap.pressed ? 0.66 : 0.35
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.theme.rimHighlight
                }
                GradientStop {
                    position: 0.34
                    color: "transparent"
                }
                GradientStop {
                    position: 0.78
                    color: "transparent"
                }
                GradientStop {
                    position: 1
                    color: root.theme.rimShadow
                }
            }
        }

        Text {
            id: glyph
            readonly property bool inverted: (root.accent || root.selected) && !tap.pressed && !root.highlighted

            anchors.centerIn: parent
            width: Math.max(1, parent.width - 6)
            visible: root.icon === ""
            text: root.label
            color: inverted ? root.theme.accentText : root.theme.textPrimary
            font.pixelSize: Math.round(root.fontSize * root.theme.fontScale)
            font.weight: root.secondary ? Font.DemiBold : Font.Medium
            fontSizeMode: Text.Fit
            minimumPixelSize: 11
            maximumLineCount: 1
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 5
            anchors.topMargin: 2
            visible: root.detail !== "" && parent.width >= 36
            text: root.detail
            color: glyph.inverted ? root.theme.accentText : root.theme.textSecondary
            font.pixelSize: Math.round(9 * root.theme.fontScale)
        }

        Shape {
            anchors.centerIn: parent
            width: 24
            height: 24
            visible: root.icon !== ""
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"
                strokeColor: glyph.inverted ? root.theme.accentText : root.theme.textPrimary
                strokeWidth: root.theme.highContrast ? 2.15 : 1.65
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: {
                        switch (root.icon) {
                        case "backspace":
                            return "M 3 12 L 8 6 H 20 V 18 H 8 Z M 11 9 L 17 15 M 17 9 L 11 15";
                        case "shift":
                            return "M 4 12 L 12 4 L 20 12 H 16 V 20 H 8 V 12 Z";
                        case "caps":
                            return "M 4 11 L 12 3 L 20 11 H 16 V 16 H 8 V 11 Z M 8 20 H 16";
                        case "close":
                            return "M 5 9 L 12 15 L 19 9 M 8 19 H 16";
                        case "expand":
                            return "M 5 9 L 12 15 L 19 9";
                        case "collapse":
                            return "M 5 15 L 12 9 L 19 15";
                        case "enter":
                            return "M 19 6 V 13 H 5 M 5 13 L 10 8 M 5 13 L 10 18";
                        case "emoji":
                            return "M 12 3 A 9 9 0 1 1 11.99 3 M 8 10 H 8.2 M 15.8 10 H 16 M 8 14 Q 12 19 16 14";
                        case "clipboard":
                            return "M 8 6 H 5 V 21 H 19 V 6 H 16 M 8 4 H 16 V 8 H 8 Z M 8 12 H 16 M 8 16 H 14";
                        case "layout":
                            return "M 3 5 H 21 V 19 H 3 Z M 7 9 H 7.1 M 12 9 H 12.1 M 17 9 H 17.1 M 7 14 H 17";
                        case "left":
                            return "M 14 5 L 7 12 L 14 19";
                        case "right":
                            return "M 10 5 L 17 12 L 10 19";
                        case "privacy":
                            return "M 7 10 V 7 A 5 5 0 0 1 17 7 V 10 M 5 10 H 19 V 21 H 5 Z M 12 14 V 17";
                        case "undo":
                            return "M 12 5 L 6 10 L 12 15 M 6 10 H 15 C 21 10 21 19 15 19 H 11";
                        case "ink":
                            return "M 5 19 L 9 18 L 20 7 L 17 4 L 6 15 Z M 14 7 L 17 10 M 4 22 H 20";
                        case "microphone":
                            return "M 9 5 A 3 3 0 0 1 15 5 V 11 A 3 3 0 0 1 9 11 Z M 6 10 V 12 A 6 6 0 0 0 18 12 V 10 M 12 18 V 21 M 8 21 H 16";
                        case "microphoneStop":
                            return "M 7 7 H 17 V 17 H 7 Z";
                        default:
                            return "";
                        }
                    }
                }
            }
        }
    }

    TapHandler {
        id: tap
        gesturePolicy: root.cursorDrag || root.preferScroll ? TapHandler.DragThreshold : TapHandler.ReleaseWithinBounds
        longPressThreshold: root.repeats ? 0.36 : (root.hasAlternates ? 0.44 : 60)
        onTapped: root.activated()
        onLongPressed: {
            if (root.repeats) {
                root.activated();
                repeatTimer.start();
            }
            if (root.hasAlternates)
                root.held();
        }
        onPressedChanged: {
            if (!pressed) {
                repeatTimer.stop();
                if (root.hasAlternates)
                    root.holdReleased();
            }
        }
        onPointChanged: {
            if (root.hasAlternates && pressed)
                root.holdMoved(point.scenePosition);
        }
    }

    Timer {
        id: repeatTimer
        interval: 70
        repeat: true
        onTriggered: {
            const point = tap.point.position;
            if (tap.pressed && root.contains(point))
                root.activated();
        }
    }

    DragHandler {
        id: drag
        enabled: root.cursorDrag
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        dragThreshold: 12
        property int committedSteps: 0

        onActiveChanged: committedSteps = 0
        onActiveTranslationChanged: {
            if (!active)
                return;
            const steps = Math.trunc(activeTranslation.x / 18);
            if (steps !== committedSteps) {
                root.cursorMoved(steps - committedSteps);
                committedSteps = steps;
            }
        }
    }
}
