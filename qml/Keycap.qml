pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    required property GlassTheme theme
    property string label: ""
    property string gestureLetter: ""
    property string detail: ""
    property bool detailBelow: false
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
    property bool prominentLabel: false
    property real visualGap: 4
    property real verticalGap: 6
    property real fontSize: secondary ? 15 : 22
    property real visualRadius: secondary ? 15 : 12
    property real iconSize: 24
    readonly property bool pressedVisual: tap.pressed || drag.active || highlighted

    signal activated
    signal held
    signal holdMoved(point scenePosition)
    signal holdReleased
    signal holdCanceled
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
        visible: !root.transparentRest && !root.theme.highContrast && !root.pressedVisual
        x: cap.x - 1
        y: cap.y + 3
        width: cap.width + 2
        height: cap.height + 1
        radius: cap.radius + 1
        color: root.theme.diffuseShadow
        antialiasing: true
    }

    Rectangle {
        visible: !root.transparentRest && !root.theme.highContrast && !root.pressedVisual
        x: cap.x + 1
        y: cap.y + 2
        width: Math.max(0, cap.width - 2)
        height: cap.height
        radius: cap.radius
        color: root.theme.contactShadow
        antialiasing: true
    }

    Rectangle {
        id: cap
        anchors.fill: parent
        anchors.leftMargin: root.visualGap / 2
        anchors.rightMargin: root.visualGap / 2
        anchors.topMargin: root.verticalGap / 2
        anchors.bottomMargin: root.verticalGap / 2
        radius: Math.min(root.visualRadius, Math.min(width, height) / 2)
        antialiasing: true
        color: root.pressedVisual ? root.theme.keyPressedFill : (root.accent ? root.theme.accent : (root.selected ? root.theme.selectionFill : (root.transparentRest ? "transparent" : (root.secondary ? root.theme.functionFill : root.theme.keyFill))))
        border.color: root.highlighted || root.activeFocus ? root.theme.focusRing : (root.theme.highContrast ? root.theme.keyBorder : root.theme.keyRim)
        border.width: root.highlighted || root.activeFocus ? 2 : (root.transparentRest && !root.pressedVisual && !root.selected ? 0 : (root.theme.highContrast ? 2 : 1))
        scale: root.theme.reduceMotion || root.highlighted ? 1 : (root.pressedVisual ? 0.95 : 1)

        Behavior on scale {
            enabled: !root.theme.reduceMotion
            NumberAnimation {
                duration: root.pressedVisual ? 65 : 140
                easing.type: Easing.OutCubic
            }
        }

        Behavior on color {
            enabled: !root.pressedVisual && root.theme.motionDuration > 0
            ColorAnimation {
                duration: Math.round(root.theme.motionDuration * 0.65)
            }
        }

        Rectangle {
            visible: (!root.transparentRest || root.selected) && !root.theme.highContrast
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            opacity: root.pressedVisual ? 0.30 : (root.accent ? 0.42 : 0.22)
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.theme.rimHighlight
                }
                GradientStop {
                    position: 0.26
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
            readonly property bool inverted: root.accent && !root.pressedVisual

            anchors.centerIn: parent
            anchors.verticalCenterOffset: root.detailBelow ? -5 : 0
            width: Math.max(1, parent.width - 6)
            height: Math.max(0, parent.height - (root.detailBelow ? 13 : 4))
            visible: root.icon === ""
            text: root.label
            color: inverted ? root.theme.accentText : root.theme.textPrimary
            font.pixelSize: Math.round(root.fontSize * root.theme.fontScale)
            font.weight: root.secondary || root.prominentLabel ? Font.DemiBold : Font.Normal
            fontSizeMode: Text.Fit
            minimumPixelSize: 11
            maximumLineCount: 1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Text {
            anchors.right: root.detailBelow ? undefined : parent.right
            anchors.top: root.detailBelow ? undefined : parent.top
            anchors.horizontalCenter: root.detailBelow ? parent.horizontalCenter : undefined
            anchors.bottom: root.detailBelow ? parent.bottom : undefined
            anchors.rightMargin: 4
            anchors.topMargin: 2
            anchors.bottomMargin: 3
            visible: root.detail !== "" && (root.detailBelow || parent.width >= 30)
            text: root.detail
            color: glyph.inverted ? root.theme.accentText : root.theme.textSecondary
            font.pixelSize: Math.round((root.detailBelow ? 9 : 8) * Math.min(root.theme.fontScale, 1.35))
        }

        Shape {
            anchors.centerIn: parent
            width: 24
            height: 24
            scale: root.iconSize / 24
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
                        case "dismiss":
                            return "M 6 6 L 18 18 M 18 6 L 6 18";
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
                        case "globe":
                            return "M 21 12 A 9 9 0 1 1 3 12 A 9 9 0 1 1 21 12 M 3 12 H 21 M 12 3 C 6.7 7.2 6.7 16.8 12 21 M 12 3 C 17.3 7.2 17.3 16.8 12 21";
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
        property bool holdRecognized: false
        grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType | (holdRecognized ? 0 : PointerHandler.ApprovesTakeOverByAnything)
        gesturePolicy: root.cursorDrag || root.preferScroll ? TapHandler.DragThreshold : TapHandler.ReleaseWithinBounds
        longPressThreshold: root.repeats ? 0.36 : (root.hasAlternates ? 0.44 : 60)
        onTapped: root.activated()
        onLongPressed: {
            if (root.repeats) {
                root.activated();
                repeatTimer.start();
            }
            if (root.hasAlternates) {
                holdRecognized = true;
                root.held();
            }
        }
        onPressedChanged: {
            if (!pressed)
                repeatTimer.stop();
        }
        onGrabChanged: (transition, eventPoint) => {
            if (!holdRecognized)
                return;
            const canceled = transition === PointerDevice.CancelGrabExclusive || transition === PointerDevice.CancelGrabPassive;
            const ungrabbed = transition === PointerDevice.UngrabExclusive || transition === PointerDevice.UngrabPassive;
            if (!canceled && !ungrabbed)
                return;
            holdRecognized = false;
            if (canceled || eventPoint.state !== EventPoint.Released) {
                root.holdCanceled();
                return;
            }
            root.holdMoved(eventPoint.scenePosition);
            root.holdReleased();
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
