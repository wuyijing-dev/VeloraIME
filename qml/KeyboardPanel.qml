pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import "KeyLayouts.js" as KeyLayouts

Item {
    id: root

    property KeyboardController controller: KeyboardController {
        enabled: root.visible
        window: root.Window.window
    }
    property Item backgroundSource: null
    property bool dark: systemPalette.window.hslLightness < 0.5
    property bool reduceTransparency: false
    property bool highContrast: false
    property bool reduceMotion: false
    property real fontScale: 1
    property vector4d safeAreaInsets: Qt.vector4d(0, 0, 0, 0)

    // 0 selects the full mode when a GPU and a sample source are available.
    // 1 asks for full, 2 light, and 3 always uses the solid material.
    property int graphicsMode: 0
    property string alignment: "center"
    property bool oneHanded: false
    property bool swipeEnabled: true
    property bool handwritingAvailable: false
    property bool voiceAvailable: false
    property bool listening: false
    property bool handwritingBusy: false
    property var handwritingCandidates: []

    readonly property rect keyboardRectangle: visible ? Qt.rect(0, 0, width, height) : Qt.rect(0, 0, 0, 0)
    readonly property string locale: controller.language
    readonly property string inputMode: controller.inputMode
    readonly property real minimumQwertyWidth: 10 * 44
    readonly property bool needsCompactQwerty: keysArea.width < minimumQwertyWidth

    signal closeRequested
    signal keyFeedback(string action)
    signal preferencesRequested
    signal handwritingRequested(var strokes, real width, real height)
    signal handwritingCancelled
    signal voiceRequested(bool start)
    signal voiceCancelled
    signal recognitionTextAccepted(string text)

    QtObject {
        id: panelState

        property string page: "letters"
        property string tray: ""
        property string preferredEnglish: "en-US"
        property string preferredChinese: "zh-CN"
        property string shift: "off"
        property double shiftTimestamp: 0
        property string emojiCategory: "faces"
        property bool searchingEmoji: false
        property string emojiQuery: ""
        property bool showAllCandidates: false
        property int keyboardCandidate: -1
        property Item alternateAnchor: null
        property var alternateValues: []
        property int alternateIndex: -1
        property point alternatePosition: Qt.point(0, 0)
        property bool alternateTracking: false
    }

    readonly property bool chinese: locale.indexOf("zh") === 0
    readonly property bool numeric: inputMode === "number" || inputMode === "decimal" || inputMode === "phone"
    readonly property bool activeSession: controller.active
    readonly property bool sensitiveSession: controller.sensitive
    readonly property bool canCompose: chinese && !sensitiveSession && inputMode === "text"
    readonly property bool canUseTools: !sensitiveSession && !numeric
    readonly property bool hasCandidates: !sensitiveSession && controller.candidates.length > 0
    readonly property bool shiftOn: panelState.shift !== "off"
    readonly property bool isNine: canCompose && controller.pinyinMode === "t9"
    readonly property bool canSwipe: swipeEnabled && activeSession && !sensitiveSession && locale.indexOf("en") === 0 && inputMode === "text" && panelState.page === "letters" && panelState.tray === "" && !panelState.showAllCandidates && !panelState.alternateAnchor
    readonly property bool largePortrait: width >= 720 && root.Window.window !== null && root.Window.window.height > root.Window.window.width * 1.12
    readonly property real physicalRowHeight: Screen.pixelDensity >= 3 && Screen.pixelDensity <= 18 ? Math.round(Screen.pixelDensity * 7.2) : 0
    readonly property real sidePadding: width < 480 ? 7 : 12
    readonly property real headerHeight: largePortrait ? 56 : 48
    readonly property real preferredKeyHeight: largePortrait ? Math.min(88, Math.max(64, physicalRowHeight, Math.round(width / 14))) : Math.min(61, Math.max(46, physicalRowHeight, Math.round(width / 16)))
    readonly property var keyRows: {
        if (numeric)
            return KeyLayouts.numbers(inputMode);
        if (panelState.searchingEmoji)
            return KeyLayouts.letters(false, "text");
        if (panelState.page !== "letters")
            return KeyLayouts.symbols(panelState.page === "extended", canCompose, locale);
        if (isNine)
            return KeyLayouts.nine();
        return KeyLayouts.letters(canCompose, inputMode);
    }
    readonly property var visibleEmoji: KeyLayouts.emojiKeys(panelState.emojiCategory, panelState.emojiQuery, controller.recentEmoji || [])

    implicitWidth: 700
    implicitHeight: headerHeight + 4 * preferredKeyHeight + 26 + Math.max(0, safeAreaInsets.y) + Math.max(0, safeAreaInsets.w)
    Accessible.name: qsTr("触摸键盘")

    SystemPalette {
        id: systemPalette
    }

    GlassTheme {
        id: glass
        dark: root.dark
        highContrast: root.highContrast
        reduceMotion: root.reduceMotion
        fontScale: Math.max(0.9, Math.min(root.fontScale, 1.8))
    }

    function clearAlternates(): void {
        panelState.alternateAnchor = null;
        panelState.alternateValues = [];
        panelState.alternateIndex = -1;
        panelState.alternateTracking = false;
    }

    function clearPanels(): void {
        if (root.listening)
            root.voiceCancelled();
        panelState.tray = "";
        panelState.searchingEmoji = false;
        panelState.emojiQuery = "";
        panelState.showAllCandidates = false;
        panelState.keyboardCandidate = -1;
        root.clearAlternates();
    }

    function changeLanguage(): void {
        root.clearPanels();
        panelState.page = "letters";
        panelState.shift = "off";
        panelState.shiftTimestamp = 0;
        controller.language = chinese ? panelState.preferredEnglish : panelState.preferredChinese;
    }

    function setPinyinMode(mode: string, language: string): void {
        root.clearPanels();
        panelState.page = "letters";
        controller.language = language;
        controller.pinyinMode = mode;
    }

    function cycleShift(): void {
        const now = Date.now();
        if (panelState.shift === "off")
            panelState.shift = "once";
        else if (panelState.shift === "once" && now - panelState.shiftTimestamp <= 350)
            panelState.shift = "caps";
        else
            panelState.shift = "off";
        panelState.shiftTimestamp = now;
    }

    function emitText(text: string, direct: bool): void {
        if (panelState.searchingEmoji && !direct) {
            panelState.emojiQuery += text;
            return;
        }
        if (direct || panelState.page !== "letters")
            controller.inputDirect(text);
        else
            controller.input(text);
        if (panelState.shift === "once")
            panelState.shift = "off";
    }

    function perform(action: string, value: string): void {
        if (!controller.active)
            return;
        root.clearAlternates();
        root.keyFeedback(action);
        switch (action) {
        case "text":
            root.emitText(value, false);
            break;
        case "shift":
            root.cycleShift();
            break;
        case "language":
            root.changeLanguage();
            break;
        case "symbol":
        case "extended":
        case "letters":
            panelState.page = action;
            break;
        case "backspace":
            if (panelState.searchingEmoji)
                panelState.emojiQuery = panelState.emojiQuery.slice(0, -1);
            else
                controller.backspace();
            break;
        case "space":
            if (panelState.searchingEmoji)
                panelState.emojiQuery += " ";
            else
                controller.space();
            break;
        case "enter":
            if (panelState.searchingEmoji)
                panelState.searchingEmoji = false;
            else
                controller.enter();
            break;
        case "left":
            controller.moveCursor(-1);
            break;
        case "right":
            controller.moveCursor(1);
            break;
        }
    }

    function closePanel(): void {
        controller.cancelComposition();
        root.clearPanels();
        root.closeRequested();
    }

    function showTray(tray: string): void {
        const alreadyOpen = panelState.tray === tray;
        root.clearPanels();
        if (!alreadyOpen) {
            controller.cancelComposition();
            panelState.tray = tray;
        }
    }

    function chooseCandidate(candidateId: string): void {
        if (!controller.active || controller.sensitive)
            return;
        root.clearPanels();
        root.keyFeedback("candidate");
        controller.selectCandidate(candidateId);
    }

    function chooseEmoji(value: string): void {
        if (!controller.active || !canUseTools)
            return;
        root.keyFeedback("emoji");
        root.emitText(value, true);
        controller.recordEmoji(value);
        panelState.emojiQuery = "";
        panelState.searchingEmoji = false;
    }

    function alternatePosition(scenePosition: point): void {
        const relative = root.mapFromItem(null, scenePosition);
        panelState.alternatePosition = relative;
        if (!alternatesTray.visible)
            return;
        const inside = relative.x >= alternatesTray.x && relative.x < alternatesTray.x + alternatesTray.width && relative.y >= alternatesTray.y && relative.y < alternatesTray.y + alternatesTray.height;
        panelState.alternateIndex = inside ? Math.min(panelState.alternateValues.length - 1, Math.floor((relative.x - alternatesTray.x + alternateList.contentX) / 46)) : -1;
    }

    function releaseAlternate(anchor: Item): void {
        if (panelState.alternateAnchor !== anchor)
            return;
        panelState.alternateTracking = false;
        if (panelState.alternateIndex >= 0) {
            root.perform("text", panelState.alternateValues[panelState.alternateIndex]);
            return;
        }
        const pointInAnchor = anchor.mapFromItem(root, panelState.alternatePosition);
        if (!anchor.contains(pointInAnchor))
            root.clearAlternates();
    }

    function keyName(key: var): string {
        switch (key.action) {
        case "shift":
            return panelState.shift === "caps" ? qsTr("大写锁定") : qsTr("切换大小写");
        case "backspace":
            return qsTr("删除");
        case "left":
            return qsTr("光标向左");
        case "right":
            return qsTr("光标向右");
        case "language":
            return qsTr("切换中文和英文");
        case "enter":
            return panelState.searchingEmoji ? qsTr("查看结果") : controller.enterLabel;
        default:
            return key.accessibleLabel || key.label;
        }
    }

    onVisibleChanged: {
        if (!visible) {
            controller.cancelComposition();
            root.clearPanels();
        }
    }
    onInputModeChanged: {
        panelState.page = "letters";
        panelState.shift = "off";
        panelState.shiftTimestamp = 0;
        root.clearPanels();
    }
    onLocaleChanged: {
        if (locale.indexOf("en") === 0)
            panelState.preferredEnglish = locale;
        else if (locale.indexOf("zh") === 0)
            panelState.preferredChinese = locale;
        panelState.page = "letters";
        panelState.shift = "off";
        root.clearAlternates();
    }
    onActiveSessionChanged: {
        if (!activeSession)
            root.clearPanels();
    }
    onSensitiveSessionChanged: root.clearPanels()
    onHandwritingAvailableChanged: {
        if (!handwritingAvailable && panelState.tray === "ink")
            root.clearPanels();
    }
    onVoiceAvailableChanged: {
        if (!voiceAvailable && root.listening)
            root.voiceCancelled();
    }

    Connections {
        target: root.controller
        function onSessionTokenChanged() {
            root.clearPanels();
        }
        function onCandidatesChanged() {
            panelState.keyboardCandidate = -1;
            if (!root.hasCandidates)
                panelState.showAllCandidates = false;
            if (candidateList.contentWidth > 0)
                candidateList.positionViewAtBeginning();
        }
    }

    GlassSurface {
        id: backing
        anchors.fill: parent
        theme: glass
        backgroundSource: root.backgroundSource
        reduceTransparency: root.reduceTransparency
        graphicsMode: root.graphicsMode
        opacity: root.visible ? 1 : 0

        Behavior on opacity {
            enabled: glass.motionDuration > 0
            NumberAnimation {
                duration: glass.motionDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    Item {
        id: interior
        x: root.sidePadding + Math.max(0, root.safeAreaInsets.x)
        y: 6 + Math.max(0, root.safeAreaInsets.y)
        width: Math.max(0, root.width - x - root.sidePadding - Math.max(0, root.safeAreaInsets.z))
        height: Math.max(0, root.height - y - 12 - Math.max(0, root.safeAreaInsets.w))

        Item {
            id: header
            width: parent.width
            height: root.headerHeight

            Keycap {
                id: languageKey
                theme: glass
                width: root.controller.preedit.length > 0 ? Math.min(130, Math.max(72, header.width / 5)) : 56
                height: parent.height
                label: root.controller.preedit || (root.chinese ? (root.locale === "zh-TW" ? "繁" : "拼") : (root.locale === "en-GB" ? "EN UK" : "EN"))
                icon: root.controller.sensitive ? "privacy" : ""
                accessibleLabel: root.controller.sensitive ? qsTr("隐私输入") : (root.controller.preedit || qsTr("输入语言"))
                secondary: true
                transparentRest: true
                enabled: root.controller.active && !root.numeric && !root.controller.sensitive
                hasAlternates: false
                onActivated: root.changeLanguage()
            }

            Keycap {
                id: closeKey
                objectName: "hideKeyboard"
                theme: glass
                anchors.right: parent.right
                width: 46
                height: parent.height
                icon: "close"
                accessibleLabel: qsTr("隐藏键盘")
                secondary: true
                transparentRest: true
                onActivated: root.closePanel()
            }

            Keycap {
                id: expandKey
                theme: glass
                anchors.right: closeKey.left
                width: visible ? 46 : 0
                height: parent.height
                visible: root.hasCandidates
                icon: panelState.showAllCandidates ? "collapse" : "expand"
                accessibleLabel: panelState.showAllCandidates ? qsTr("收起候选") : qsTr("展开候选")
                secondary: true
                transparentRest: true
                onActivated: panelState.showAllCandidates = !panelState.showAllCandidates
            }

            ListView {
                id: candidateList
                anchors.left: languageKey.right
                anchors.right: expandKey.left
                height: parent.height
                clip: true
                visible: root.hasCandidates
                model: visible ? root.controller.candidates : []
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds

                delegate: Keycap {
                    id: candidate
                    required property var modelData
                    required property int index
                    theme: glass
                    width: Math.min(candidateList.width, Math.max(54, labelMetrics.width + 26))
                    height: candidateList.height
                    label: modelData.text
                    accessibleLabel: qsTr("候选 %1，%2").arg(index + 1).arg(label)
                    fontSize: 18
                    transparentRest: index !== 0
                    highlighted: panelState.keyboardCandidate === index
                    preferScroll: true
                    onActivated: root.chooseCandidate(modelData.id)

                    TextMetrics {
                        id: labelMetrics
                        text: candidate.label
                        font.pixelSize: Math.round(18 * glass.fontScale)
                    }
                }
            }

            Flickable {
                id: toolbar
                anchors.left: languageKey.right
                anchors.right: closeKey.left
                height: parent.height
                visible: (!root.hasCandidates || root.listening) && !root.numeric && !root.controller.sensitive && !panelState.searchingEmoji
                contentWidth: toolButtons.width
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Row {
                    id: toolButtons
                    height: toolbar.height

                    Keycap {
                        objectName: "keyboardLayouts"
                        theme: glass
                        width: 44
                        height: parent.height
                        icon: "layout"
                        accessibleLabel: qsTr("输入布局")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        selected: panelState.tray === "layout"
                        enabled: root.controller.active
                        onActivated: root.showTray("layout")
                    }
                    Keycap {
                        objectName: "keyboardEmoji"
                        theme: glass
                        width: 44
                        height: parent.height
                        icon: "emoji"
                        accessibleLabel: qsTr("表情")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        selected: panelState.tray === "emoji"
                        enabled: root.controller.active
                        onActivated: root.showTray("emoji")
                    }
                    Keycap {
                        objectName: "keyboardClipboard"
                        theme: glass
                        width: 44
                        height: parent.height
                        icon: "clipboard"
                        accessibleLabel: qsTr("剪贴板操作")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        selected: panelState.tray === "clipboard"
                        enabled: root.controller.active
                        onActivated: root.showTray("clipboard")
                    }
                    Keycap {
                        theme: glass
                        width: 44
                        height: toolButtons.height
                        icon: "ink"
                        objectName: "keyboardInk"
                        accessibleLabel: qsTr("手写")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        selected: panelState.tray === "ink"
                        visible: root.handwritingAvailable
                        enabled: root.controller.active
                        onActivated: root.showTray("ink")
                    }
                    Keycap {
                        theme: glass
                        width: 44
                        height: toolButtons.height
                        icon: root.listening ? "microphoneStop" : "microphone"
                        objectName: "keyboardVoice"
                        accessibleLabel: root.listening ? qsTr("完成录音") : qsTr("语音输入")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        selected: root.listening
                        visible: root.voiceAvailable
                        enabled: root.controller.active
                        onActivated: {
                            if (!root.listening) {
                                root.clearPanels();
                                root.controller.cancelComposition();
                            }
                            root.voiceRequested(!root.listening);
                        }
                    }
                    Text {
                        height: toolButtons.height
                        visible: root.listening
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("正在听")
                        color: glass.textPrimary
                        font.pixelSize: Math.round(13 * glass.fontScale)
                    }
                    Keycap {
                        theme: glass
                        width: 44
                        height: parent.height
                        icon: "undo"
                        accessibleLabel: qsTr("撤销上次选词")
                        secondary: true
                        transparentRest: true
                        preferScroll: true
                        visible: root.controller.canUndoCandidate
                        enabled: root.controller.active
                        onActivated: root.controller.undoCandidate()
                    }
                }
            }

            Text {
                anchors.centerIn: candidateList
                visible: panelState.searchingEmoji
                text: panelState.emojiQuery || qsTr("输入名称，按回车查看")
                color: glass.textPrimary
                font.pixelSize: Math.round(14 * glass.fontScale)
                width: Math.max(0, candidateList.width - 12)
                elide: Text.ElideRight
            }
        }

        Rectangle {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 3
            height: 1
            color: glass.keyBorder
            opacity: root.highContrast ? 1 : 0.6
        }

        Item {
            id: keysArea
            width: Math.min(parent.width, root.oneHanded ? Math.max(330, parent.width * 0.62) : 960)
            x: root.alignment === "left" ? 0 : (root.alignment === "right" ? parent.width - width : (parent.width - width) / 2)
            y: header.height + 8
            height: Math.max(0, parent.height - y)
            visible: !panelState.showAllCandidates && (panelState.tray === "" || panelState.searchingEmoji)
            enabled: root.controller.active && (!panelState.alternateAnchor || panelState.alternateTracking)

            Column {
                anchors.fill: parent

                Repeater {
                    id: layoutRows
                    model: root.keyRows

                    Item {
                        id: row
                        required property var modelData
                        readonly property real weightTotal: modelData.reduce((sum, key) => sum + key.weight, 0)
                        readonly property real unit: Math.min(width / weightTotal, width / 10)
                        width: keysArea.width
                        height: Math.floor(keysArea.height / root.keyRows.length)

                        Row {
                            id: keyRow
                            anchors.centerIn: parent
                            height: parent.height

                            Repeater {
                                model: row.modelData

                                Keycap {
                                    id: key
                                    required property var modelData
                                    readonly property bool isFunction: modelData.action !== "text"
                                    readonly property bool namedEnterAction: modelData.action === "enter" && root.controller.enterLabel !== "" && root.controller.enterLabel !== qsTr("回车")
                                    readonly property var alternates: KeyLayouts.alternatives(modelData, !root.canCompose || panelState.searchingEmoji, root.shiftOn)
                                    objectName: "key-" + modelData.action + (modelData.action === "text" ? "-" + modelData.label : "")
                                    theme: glass
                                    width: (root.numeric || root.isNine && panelState.page === "letters" && !panelState.searchingEmoji ? row.width / row.weightTotal : row.unit) * modelData.weight
                                    height: keyRow.height
                                    label: namedEnterAction && width >= 56 ? root.controller.enterLabel : (!isFunction && root.shiftOn ? modelData.label.toUpperCase() : modelData.label)
                                    gestureLetter: /^[a-z]$/.test(modelData.label) ? label : ""
                                    detail: modelData.detail || ""
                                    icon: namedEnterAction && width >= 56 ? "" : (modelData.action === "shift" && panelState.shift === "caps" ? "caps" : modelData.icon || "")
                                    accessibleLabel: isFunction || modelData.accessibleLabel ? root.keyName(modelData) : label
                                    accent: modelData.action === "enter"
                                    selected: modelData.action === "shift" && root.shiftOn
                                    secondary: isFunction
                                    fontSize: isFunction ? (root.largePortrait ? 17 : 15) : (root.largePortrait ? 26 : 22)
                                    highlighted: swipeDrag.active && gestureLetter !== "" && gestureLetter === swipeZone.path.slice(-1)
                                    visualGap: row.width < root.minimumQwertyWidth ? 2 : 4
                                    repeats: modelData.action === "backspace"
                                    hasAlternates: alternates.length > 0
                                    cursorDrag: modelData.action === "space" && !panelState.searchingEmoji && !root.controller.sensitive
                                    enabled: isFunction || label !== ""
                                    visible: isFunction || label !== ""
                                    onActivated: root.perform(modelData.action, label)
                                    onCursorMoved: steps => root.controller.moveCursor(steps)
                                    onHeld: {
                                        panelState.alternateValues = alternates;
                                        panelState.alternateTracking = true;
                                        panelState.alternateAnchor = key;
                                        panelState.alternateIndex = -1;
                                        panelState.alternatePosition = key.mapToItem(root, width / 2, height / 2);
                                    }
                                    onHoldMoved: position => {
                                        if (panelState.alternateAnchor === key)
                                            root.alternatePosition(position);
                                    }
                                    onHoldReleased: root.releaseAlternate(key)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: swipeZone
                width: parent.width
                height: Math.floor(keysArea.height / root.keyRows.length) * 3
                z: 2
                enabled: root.canSwipe
                property string path: ""
                property var trace: []

                function visit(scenePosition: point): void {
                    const local = swipeZone.mapFromItem(null, scenePosition);
                    if (!swipeZone.contains(local) || swipeZone.height <= 0)
                        return;
                    const row = layoutRows.itemAt(Math.min(2, Math.floor(local.y * 3 / swipeZone.height)));
                    if (!row)
                        return;
                    const rowPoint = row.mapFromItem(null, scenePosition);
                    const keyRow = row.childAt(rowPoint.x, rowPoint.y);
                    if (keyRow) {
                        const keyPoint = keyRow.mapFromItem(null, scenePosition);
                        const key = keyRow.childAt(keyPoint.x, keyPoint.y) as Keycap;
                        const letter = key ? key.gestureLetter : "";
                        if (letter && letter !== swipeZone.path.slice(-1) && swipeZone.path.length < 40)
                            swipeZone.path += letter;
                    }
                    if (swipeZone.trace.length < 120)
                        swipeZone.trace = swipeZone.trace.concat(local);
                }

                function finish(commit: bool): void {
                    const wordPath = swipeZone.path;
                    swipeZone.path = "";
                    swipeZone.trace = [];
                    if (commit && root.canSwipe && wordPath.length >= 3) {
                        root.keyFeedback("swipe");
                        root.controller.inputSwipe(wordPath);
                        if (panelState.shift === "once")
                            panelState.shift = "off";
                    }
                }

                DragHandler {
                    id: swipeDrag
                    target: null
                    minimumPointCount: 1
                    maximumPointCount: 1
                    acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
                    dragThreshold: 18
                    grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything

                    onActiveChanged: {
                        if (active) {
                            swipeZone.finish(false);
                            swipeZone.visit(centroid.scenePressPosition);
                            swipeZone.visit(centroid.scenePosition);
                        }
                    }
                    onCentroidChanged: {
                        if (active)
                            swipeZone.visit(centroid.scenePosition);
                    }
                    onGrabChanged: (transition, eventPoint) => {
                        if (transition === PointerDevice.UngrabExclusive)
                            swipeZone.finish(true);
                        else if (transition === PointerDevice.CancelGrabExclusive)
                            swipeZone.finish(false);
                    }
                }

                Shape {
                    anchors.fill: parent
                    visible: swipeDrag.active && !root.reduceMotion && swipeZone.trace.length > 1
                    opacity: 0.56
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: glass.accent
                        strokeWidth: 4
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        startX: swipeZone.trace.length > 0 ? swipeZone.trace[0].x : 0
                        startY: swipeZone.trace.length > 0 ? swipeZone.trace[0].y : 0
                        PathPolyline {
                            path: swipeZone.trace
                        }
                    }
                }
            }
        }

        ListView {
            id: expandedCandidates
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: keysArea.top
            anchors.bottom: parent.bottom
            visible: panelState.showAllCandidates && root.hasCandidates
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: visible ? root.controller.candidates : []

            delegate: Keycap {
                required property var modelData
                required property int index
                theme: glass
                width: expandedCandidates.width
                height: 46
                label: modelData.text
                highlighted: panelState.keyboardCandidate === index
                preferScroll: true
                secondary: true
                onActivated: root.chooseCandidate(modelData.id)
            }
        }

        Item {
            id: toolArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: keysArea.top
            anchors.bottom: parent.bottom
            visible: panelState.tray !== "" && !panelState.searchingEmoji && !panelState.showAllCandidates

            Flow {
                anchors.fill: parent
                visible: panelState.tray === "layout"
                Repeater {
                    model: [
                        {
                            label: qsTr("简体全拼"),
                            pinyin: "full",
                            locale: "zh-CN"
                        },
                        {
                            label: qsTr("繁體全拼"),
                            pinyin: "full",
                            locale: "zh-TW"
                        },
                        {
                            label: qsTr("九键拼音"),
                            pinyin: "t9",
                            locale: "zh-CN"
                        },
                        {
                            label: qsTr("小鹤双拼"),
                            pinyin: "double",
                            locale: "zh-CN"
                        },
                        {
                            label: qsTr("English (US)"),
                            pinyin: "full",
                            locale: "en-US"
                        },
                        {
                            label: qsTr("English (UK)"),
                            pinyin: "full",
                            locale: "en-GB"
                        },
                        {
                            label: qsTr("右手"),
                            setting: "right"
                        },
                        {
                            label: qsTr("左手"),
                            setting: "left"
                        },
                        {
                            label: qsTr("全宽"),
                            setting: "center"
                        },
                        {
                            label: qsTr("模糊音"),
                            setting: "fuzzy"
                        },
                        {
                            label: qsTr("偏好设置"),
                            setting: "preferences"
                        }
                    ]
                    Keycap {
                        required property var modelData
                        theme: glass
                        width: toolArea.width / (toolArea.width < 520 ? 3 : 4)
                        height: 46
                        label: modelData.label
                        secondary: true
                        selected: modelData.locale ? modelData.locale === root.locale && modelData.pinyin === root.controller.pinyinMode : modelData.setting === root.alignment || (modelData.setting === "fuzzy" && root.controller.fuzzyPinyin)
                        onActivated: {
                            if (modelData.locale) {
                                root.setPinyinMode(modelData.pinyin, modelData.locale);
                            } else if (modelData.setting === "preferences") {
                                root.clearPanels();
                                root.preferencesRequested();
                            } else if (modelData.setting === "fuzzy") {
                                root.controller.fuzzyPinyin = !root.controller.fuzzyPinyin;
                            } else {
                                root.oneHanded = modelData.setting !== "center";
                                root.alignment = modelData.setting;
                                root.clearPanels();
                            }
                        }
                    }
                }
            }

            Item {
                anchors.fill: parent
                visible: panelState.tray === "clipboard"

                Flow {
                    id: clipboardActions
                    width: parent.width
                    height: 92

                    Repeater {
                        model: [qsTr("复制"), qsTr("剪切"), qsTr("粘贴"), qsTr("全选"), qsTr("光标左移"), qsTr("光标右移"), root.controller.clipboardHistoryEnabled ? qsTr("关闭历史") : qsTr("开启历史"), qsTr("清空历史")]
                        Keycap {
                            required property string modelData
                            required property int index
                            theme: glass
                            width: Math.floor(clipboardActions.width / 4)
                            height: 46
                            label: modelData
                            secondary: true
                            enabled: root.canUseTools && (index !== 7 || clipboardHistory.count > 0)
                            onActivated: {
                                switch (index) {
                                case 0:
                                    root.controller.copy();
                                    break;
                                case 1:
                                    root.controller.cut();
                                    break;
                                case 2:
                                    root.controller.paste();
                                    break;
                                case 3:
                                    root.controller.selectAll();
                                    break;
                                case 4:
                                    root.controller.moveCursor(-1);
                                    break;
                                case 5:
                                    root.controller.moveCursor(1);
                                    break;
                                case 6:
                                    root.controller.clipboardHistoryEnabled = !root.controller.clipboardHistoryEnabled;
                                    break;
                                case 7:
                                    root.controller.clearClipboardHistory();
                                    break;
                                }
                            }
                        }
                    }
                }

                ListView {
                    id: clipboardHistory
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: clipboardActions.bottom
                    anchors.bottom: parent.bottom
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.canUseTools && root.controller.clipboardHistoryEnabled ? root.controller.clipboardHistory : []

                    delegate: Keycap {
                        required property string modelData
                        required property int index
                        theme: glass
                        width: clipboardHistory.width
                        height: 46
                        label: modelData.replace(/\s+/g, " ").slice(0, 70)
                        accessibleLabel: qsTr("粘贴历史条目 %1").arg(index + 1)
                        secondary: true
                        preferScroll: true
                        onActivated: root.controller.pasteHistory(index)
                    }
                }

                Text {
                    anchors.centerIn: clipboardHistory
                    visible: clipboardHistory.count === 0
                    text: root.controller.clipboardHistoryEnabled ? qsTr("暂无剪贴板历史") : qsTr("剪贴板历史已关闭")
                    color: glass.textSecondary
                    font.pixelSize: Math.round(14 * glass.fontScale)
                }
            }

            Item {
                anchors.fill: parent
                visible: panelState.tray === "emoji"

                Row {
                    id: emojiTabs
                    width: parent.width
                    height: 44
                    Repeater {
                        model: [
                            {
                                label: qsTr("最近"),
                                value: "recent"
                            },
                            {
                                label: qsTr("表情"),
                                value: "faces"
                            },
                            {
                                label: qsTr("手势"),
                                value: "people"
                            },
                            {
                                label: qsTr("事物"),
                                value: "things"
                            },
                            {
                                label: panelState.emojiQuery || qsTr("搜索"),
                                value: "search"
                            }
                        ]
                        Keycap {
                            required property var modelData
                            theme: glass
                            width: emojiTabs.width / 5
                            height: emojiTabs.height
                            label: modelData.label
                            secondary: true
                            selected: modelData.value === panelState.emojiCategory && panelState.emojiQuery === ""
                            onActivated: {
                                if (modelData.value === "search") {
                                    panelState.searchingEmoji = true;
                                } else {
                                    panelState.emojiCategory = modelData.value;
                                    panelState.emojiQuery = "";
                                }
                            }
                        }
                    }
                }

                GridView {
                    id: emojiGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: emojiTabs.bottom
                    anchors.bottom: parent.bottom
                    cellHeight: 45
                    cellWidth: Math.max(44, Math.floor(width / Math.max(1, Math.floor(width / 48))))
                    model: visible ? root.visibleEmoji : []
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Keycap {
                        required property string modelData
                        theme: glass
                        width: emojiGrid.cellWidth
                        height: emojiGrid.cellHeight
                        label: modelData
                        preferScroll: true
                        transparentRest: true
                        onActivated: root.chooseEmoji(modelData)
                    }
                }

                Text {
                    anchors.centerIn: emojiGrid
                    visible: emojiGrid.count === 0
                    text: panelState.emojiQuery ? qsTr("没有匹配的表情") : qsTr("暂无最近使用的表情")
                    color: glass.textPrimary
                    font.pixelSize: Math.round(16 * glass.fontScale)
                }
            }

            Item {
                anchors.fill: parent
                visible: panelState.tray === "ink" && root.canUseTools && root.handwritingAvailable

                ListView {
                    id: inkResults
                    anchors.left: parent.left
                    anchors.right: clearInk.left
                    height: 46
                    model: inkPad.submitted ? root.handwritingCandidates : []
                    orientation: ListView.Horizontal
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    delegate: Keycap {
                        required property string modelData
                        theme: glass
                        width: 52
                        height: inkResults.height
                        label: modelData
                        preferScroll: true
                        onActivated: {
                            if (root.controller.active && root.canUseTools) {
                                root.recognitionTextAccepted(label);
                                inkPad.clear();
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: inkResults
                    visible: inkPad.submitted && inkResults.count === 0
                    text: root.handwritingBusy ? qsTr("正在识别") : qsTr("未识别，请继续书写")
                    color: glass.textSecondary
                    font.pixelSize: Math.round(14 * glass.fontScale)
                }

                Keycap {
                    id: clearInk
                    anchors.right: parent.right
                    width: 64
                    height: 46
                    theme: glass
                    label: qsTr("清空")
                    secondary: true
                    enabled: inkPad.strokes.length > 0
                    onActivated: inkPad.clear()
                }

                InkPad {
                    id: inkPad
                    objectName: "keyboardInkPad"
                    theme: glass
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: inkResults.bottom
                    anchors.bottom: parent.bottom
                    onTraceReady: (strokes, width, height) => root.handwritingRequested(strokes, width, height)
                    onRecognitionInvalidated: root.handwritingCancelled()
                }
            }
        }
    }

    Item {
        id: alternateDismissRegion
        anchors.fill: parent
        z: 19
        visible: alternatesTray.visible && !panelState.alternateTracking
        containmentMask: QtObject {
            function contains(position: point): bool {
                return !alternatesTray.contains(Qt.point(position.x - alternatesTray.x, position.y - alternatesTray.y));
            }
        }
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: root.clearAlternates()
        }
    }

    Item {
        id: alternatesTray
        objectName: "keyboardAlternates"
        visible: panelState.alternateAnchor !== null
        z: 20
        width: Math.max(0, Math.min(root.width - 2 * root.sidePadding, 46 * panelState.alternateValues.length))
        height: 49
        x: {
            if (!panelState.alternateAnchor)
                return 0;
            const source = panelState.alternateAnchor;
            const center = source.mapToItem(root, source.width / 2, 0);
            return Math.min(root.width - root.sidePadding - width, Math.max(root.sidePadding, center.x - width / 2));
        }
        y: panelState.alternateAnchor ? Math.max(0, panelState.alternateAnchor.mapToItem(root, 0, 0).y - height + 4) : 0

        Rectangle {
            anchors.fill: parent
            radius: 16
            color: glass.popupFill
            border.color: glass.keyBorder
            border.width: root.highContrast ? 2 : 1
            antialiasing: true
        }

        ListView {
            id: alternateList
            anchors.fill: parent
            orientation: ListView.Horizontal
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            model: panelState.alternateValues
            delegate: Keycap {
                required property string modelData
                required property int index
                theme: glass
                width: 46
                height: alternateList.height
                label: modelData
                transparentRest: true
                preferScroll: true
                highlighted: panelState.alternateIndex === index
                onActivated: root.perform("text", label)
            }
        }
    }

    Repeater {
        model: 9
        delegate: Item {
            id: candidateShortcut
            required property int index

            Shortcut {
                sequence: "Alt+" + (candidateShortcut.index + 1)
                autoRepeat: false
                enabled: root.visible && root.controller.active && root.hasCandidates && candidateShortcut.index < root.controller.candidates.length
                onActivated: root.chooseCandidate(root.controller.candidates[candidateShortcut.index].id)
            }
        }
    }

    Shortcut {
        sequence: "Alt+Down"
        enabled: root.visible && root.controller.active && root.hasCandidates
        onActivated: {
            panelState.showAllCandidates = true;
            panelState.keyboardCandidate = Math.min(root.controller.candidates.length - 1, panelState.keyboardCandidate + 1);
            expandedCandidates.positionViewAtIndex(panelState.keyboardCandidate, ListView.Contain);
        }
    }

    Shortcut {
        sequence: "Alt+Up"
        enabled: root.visible && root.controller.active && panelState.showAllCandidates
        onActivated: {
            panelState.keyboardCandidate = Math.max(0, panelState.keyboardCandidate - 1);
            expandedCandidates.positionViewAtIndex(panelState.keyboardCandidate, ListView.Contain);
        }
    }

    Shortcut {
        sequences: ["Alt+Return", "Alt+Enter"]
        enabled: root.visible && root.controller.active && root.hasCandidates && panelState.keyboardCandidate >= 0
        onActivated: root.chooseCandidate(root.controller.candidates[panelState.keyboardCandidate].id)
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: root.visible && root.controller.active
        autoRepeat: false
        onActivated: {
            if (panelState.alternateAnchor)
                root.clearAlternates();
            else if (panelState.tray || panelState.showAllCandidates)
                root.clearPanels();
            else if (root.controller.preedit)
                root.controller.cancelComposition();
            else
                root.closePanel();
        }
    }
}
