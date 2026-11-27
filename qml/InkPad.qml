pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property GlassTheme theme
    property var strokes: []
    property var currentStroke: []
    property bool submitted: false
    property bool drawing: false
    readonly property real inkWidth: canvas.width
    readonly property real inkHeight: canvas.height

    signal traceReady(var completeStrokes, real width, real height)
    signal recognitionInvalidated

    Accessible.name: qsTr("手写输入区域")

    function clear(): void {
        debounce.stop();
        root.strokes = [];
        root.currentStroke = [];
        root.drawing = false;
        root.submitted = false;
        root.recognitionInvalidated();
        canvas.requestPaint();
    }

    function appendPosition(position: point): void {
        if (root.currentStroke.length >= 4096)
            return;
        const x = Math.max(0, Math.min(canvas.width, position.x));
        const y = Math.max(0, Math.min(canvas.height, position.y));
        const last = root.currentStroke[root.currentStroke.length - 1];
        if (!last || Math.abs(last.x - x) + Math.abs(last.y - y) >= 1.5)
            root.currentStroke.push({
                x: x,
                y: y
            });
        canvas.requestPaint();
    }

    onVisibleChanged: {
        if (!visible)
            root.clear();
    }

    Rectangle {
        id: paper
        anchors.fill: parent
        anchors.margins: 2
        radius: 16
        color: root.theme.functionFill
        border.width: root.theme.highContrast ? 2 : 1
        border.color: root.theme.keyBorder
        antialiasing: true
    }

    Text {
        anchors.centerIn: paper
        visible: !root.drawing && root.strokes.length === 0
        text: qsTr("在此手写")
        color: root.theme.textSecondary
        font.pixelSize: Math.round(16 * root.theme.fontScale)
    }

    Canvas {
        id: canvas
        anchors.fill: paper
        anchors.margins: 4
        antialiasing: true

        function paintStroke(context: var, stroke: var): void {
            if (stroke.length === 0)
                return;
            context.beginPath();
            context.moveTo(stroke[0].x, stroke[0].y);
            for (let i = 1; i < stroke.length; ++i)
                context.lineTo(stroke[i].x, stroke[i].y);
            if (stroke.length === 1)
                context.lineTo(stroke[0].x + 0.1, stroke[0].y);
            context.stroke();
        }

        onPaint: {
            const context = getContext("2d");
            context.clearRect(0, 0, width, height);
            context.strokeStyle = root.theme.textPrimary;
            context.lineWidth = 3.5;
            context.lineCap = "round";
            context.lineJoin = "round";
            for (const stroke of root.strokes)
                paintStroke(context, stroke);
            paintStroke(context, root.currentStroke);
        }

        MultiPointTouchArea {
            anchors.fill: parent
            minimumTouchPoints: 1
            maximumTouchPoints: 1
            mouseEnabled: true
            touchPoints: TouchPoint {
                id: pen
            }

            onPressed: {
                if (root.strokes.length >= 32)
                    root.clear();
                debounce.stop();
                root.submitted = false;
                root.recognitionInvalidated();
                root.currentStroke = [];
                root.drawing = true;
                root.appendPosition(Qt.point(pen.x, pen.y));
            }
            onUpdated: {
                if (root.drawing)
                    root.appendPosition(Qt.point(pen.x, pen.y));
            }
            onReleased: {
                if (root.drawing) {
                    root.appendPosition(Qt.point(pen.x, pen.y));
                    if (root.currentStroke.length)
                        root.strokes = root.strokes.concat([root.currentStroke]);
                    root.currentStroke = [];
                    root.drawing = false;
                    debounce.restart();
                }
            }
            onCanceled: {
                root.currentStroke = [];
                root.drawing = false;
                canvas.requestPaint();
            }
        }
    }

    Timer {
        id: debounce
        interval: 280
        onTriggered: {
            if (root.visible && !root.drawing && root.strokes.length) {
                root.submitted = true;
                root.traceReady(root.strokes, root.inkWidth, root.inkHeight);
            }
        }
    }
}
