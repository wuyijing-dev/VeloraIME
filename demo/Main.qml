pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes
import QtCore
import Rk.TouchKeyboard

ApplicationWindow {
    id: window

    width: 414
    height: 736
    minimumWidth: 320
    minimumHeight: 360
    visible: true
    title: "VeloraIME"

    required property var trustedPackKeys
    property bool dark: false
    property bool solid: false
    property bool highContrast: false
    property bool lowMotion: false
    property bool keyboardOpen: true
    property string page: "editor"
    property Item settingsEditor: null
    property string inkSession: ""
    property string recognitionError: ""
    property string dictionaryStatus: ""

    readonly property bool compactHeight: height < 510
    readonly property int sideInset: width < 380 ? 16 : (width < 520 ? 22 : 26)
    readonly property string displayFamily: Qt.fontFamilies().includes("Nunito") ? "Nunito" : font.family
    readonly property color ink: highContrast ? (dark ? "#ffffff" : "#0b111b")
                                               : (dark ? "#f4f6fa" : "#1c222d")
    readonly property color secondaryInk: dark ? "#cbd0e1" : "#4e5562"
    readonly property color accentInk: dark ? "#9dccff" : "#185bad"
    readonly property color accentLabel: dark ? "#101c30" : "#ffffff"
    readonly property color paper: dark ? "#23262f" : "#fbfcfe"
    readonly property color paperEdge: highContrast ? (dark ? "#a4b6cd" : "#4d5d75")
                                                    : (dark ? "#596475" : "#aab7c9")
    readonly property color panelFill: dark ? "#2b303a" : "#f5f7fa"
    readonly property color glassStroke: highContrast ? paperEdge : (dark ? "#758092" : "#a5b0c1")
    readonly property color glassShade: dark ? "#4512161e" : "#268399b3"
    readonly property color glassTop: dark ? "#e8545c6b" : "#efffffff"
    readonly property color glassBottom: dark ? "#ed303641" : "#d0e9eef5"
    readonly property color pressedGlass: dark ? "#596880" : "#dbe8fc"

    palette.windowText: window.ink
    palette.window: window.panelFill
    palette.base: window.paper
    palette.text: window.ink
    palette.button: window.panelFill
    palette.buttonText: window.ink
    palette.highlight: window.accentInk
    palette.highlightedText: window.accentLabel

    function scaled(size) {
        return Math.round(size * preferences.textScale)
    }

    function showFor(editor) {
        if (!editor.visible || settings.visible || clearLearning.visible)
            return
        if (!editor.activeFocus)
            editor.forceActiveFocus()
        keyboardOpen = true
        Qt.callLater(() => editorView.reveal(editor))
    }

    function openSettings() {
        if (settings.visible)
            return
        settingsEditor = keyboardOpen ? activeFocusItem : null
        keyboardOpen = false
        settings.open()
    }

    function openPage(destination, editor) {
        if (page === "pin" && destination !== "pin")
            pin.clear()
        page = destination
        if (settings.visible) {
            settingsEditor = editor
            settings.close()
        } else {
            Qt.callLater(() => window.showFor(editor))
        }
    }

    component ChromeSurface: Rectangle {
        id: material
        property bool dense: false

        radius: Math.min(height / 2, 24)
        border.width: window.highContrast ? 2 : 1
        border.color: window.glassStroke

        function tint(value) {
            if (window.solid)
                return window.panelFill
            return dense ? Qt.rgba(value.r, value.g, value.b, 0.99) : value
        }

        gradient: Gradient {
            GradientStop { position: 0; color: material.tint(window.glassTop) }
            GradientStop { position: 0.47; color: material.tint(window.glassBottom) }
            GradientStop { position: 1; color: window.panelFill }
        }
    }

    component GlassButton: Button {
        id: button
        property bool emphasized: false

        leftPadding: window.width < 380 ? 10 : 15
        rightPadding: leftPadding
        topPadding: 9
        bottomPadding: 9
        implicitWidth: Math.max(44, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(44, implicitContentHeight + topPadding + bottomPadding)
        font.pixelSize: window.scaled(15)
        font.weight: Font.DemiBold
        focusPolicy: Qt.TabFocus

        contentItem: Label {
            text: button.text
            font: button.font
            color: button.emphasized ? window.accentLabel
                                     : (button.enabled ? window.ink : window.secondaryInk)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 3
                radius: height / 2
                color: window.glassShade
                visible: !window.solid && !button.down
            }

            ChromeSurface {
                anchors.fill: parent
                anchors.bottomMargin: 2
                border.color: button.visualFocus ? window.accentInk : window.glassStroke
                border.width: button.visualFocus || window.highContrast ? 2 : 1
                opacity: button.enabled ? 1 : 0.65

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: parent.radius
                    color: button.emphasized
                           ? (button.down ? Qt.darker(window.accentInk, 1.15) : window.accentInk)
                           : window.pressedGlass
                    opacity: button.emphasized || button.down ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: window.lowMotion ? 0 : 125 }
                    }
                }
            }
        }
    }

    component HeaderGlyph: Canvas {
        id: glyph
        property bool locked: false
        property color inkColor: window.ink
        implicitWidth: locked ? 70 : 52
        implicitHeight: 24

        onInkColorChanged: requestPaint()
        onLockedChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.translate((width - implicitWidth) / 2, (height - implicitHeight) / 2)
            ctx.strokeStyle = inkColor
            ctx.lineWidth = 1.65
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            if (locked) {
                ctx.beginPath()
                ctx.moveTo(6, 12)
                ctx.lineTo(6, 9.5)
                ctx.bezierCurveTo(6, 3.35, 15, 3.35, 15, 9.5)
                ctx.lineTo(15, 12)
                ctx.stroke()
                ctx.beginPath()
                ctx.roundedRect(3.8, 11.6, 13.4, 10.1, 2.2, 2.2)
                ctx.stroke()

                ctx.fillStyle = window.dark ? "#655c73" : "#99897b"
                ctx.beginPath()
                ctx.roundedRect(27, 2, 42, 20, 10, 10)
                ctx.fill()
                ctx.fillStyle = window.dark ? "#f3f2fa" : "#ffffff"
                ctx.beginPath()
                ctx.arc(58.5, 12, 8, 0, 2 * Math.PI)
                ctx.fill()
                return
            }

            const radii = [8, 8, 10.1, 10.1, 8, 8]
            ctx.beginPath()
            for (let tooth = 0; tooth < 8; ++tooth) {
                for (let point = 0; point < radii.length; ++point) {
                    const angle = -Math.PI / 2 + (tooth + point / radii.length) * Math.PI / 4
                    const x = 13 + Math.cos(angle) * radii[point]
                    const y = 12 + Math.sin(angle) * radii[point]
                    if (tooth === 0 && point === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
            }
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(13, 12, 3.25, 0, 2 * Math.PI)
            ctx.stroke()

            ctx.globalAlpha = 0.55
            ctx.lineWidth = 0.7
            ctx.beginPath()
            ctx.moveTo(33, 5)
            ctx.lineTo(33, 19)
            ctx.stroke()
            ctx.globalAlpha = 1
            ctx.lineWidth = 1.7
            ctx.beginPath()
            ctx.moveTo(42, 7.5)
            ctx.lineTo(46.5, 12)
            ctx.lineTo(42, 16.5)
            ctx.stroke()
        }
    }

    component SettingSwitch: Switch {
        id: toggle

        implicitHeight: Math.max(44, contentItem.implicitHeight + 12)
        font.pixelSize: window.scaled(15)
        leftPadding: 0
        rightPadding: 2
        spacing: 12

        contentItem: Label {
            text: toggle.text
            font: toggle.font
            color: window.ink
            verticalAlignment: Text.AlignVCenter
            rightPadding: toggle.indicator.width + toggle.spacing
            wrapMode: Text.Wrap
        }

        indicator: Rectangle {
            x: toggle.width - width - toggle.rightPadding
            y: (toggle.height - height) / 2
            width: 49
            height: 29
            radius: height / 2
            color: toggle.checked ? window.accentInk : (window.dark ? "#414b5d" : "#dae0e8")
            border.width: window.highContrast || toggle.visualFocus ? 2 : 1
            border.color: toggle.visualFocus ? window.ink : window.glassStroke

            Rectangle {
                x: toggle.checked ? parent.width - width - 4 : 4
                y: (parent.height - height) / 2
                width: 21
                height: 21
                radius: height / 2
                color: window.dark && toggle.checked ? "#15243b" : "#ffffff"
                Behavior on x { NumberAnimation { duration: window.lowMotion ? 0 : 150 } }
            }
        }
    }

    component SettingCard: Pane {
        padding: 12
        Layout.fillWidth: true
        background: Rectangle {
            radius: 18
            color: window.paper
            border.color: window.paperEdge
            border.width: window.highContrast ? 2 : 1
        }
    }

    component SectionTitle: Label {
        leftPadding: 4
        color: window.secondaryInk
        font.pixelSize: window.scaled(14)
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }

    component WorkspaceField: TextField {
        id: field
        implicitHeight: Math.max(48, contentHeight + 22)
        leftPadding: 14
        rightPadding: 14
        color: window.ink
        placeholderTextColor: window.secondaryInk
        font.pixelSize: window.scaled(17)
        background: Rectangle {
            radius: 16
            color: window.paper
            border.color: field.activeFocus ? window.accentInk : window.paperEdge
            border.width: field.activeFocus || window.highContrast ? 2 : 1
        }
        onActiveFocusChanged: if (activeFocus) window.showFor(field)
        TapHandler { onTapped: window.showFor(field) }
    }

    component PackRow: RowLayout {
        id: pack
        required property LexiconPackManager manager
        required property string name

        spacing: 7
        Layout.fillWidth: true
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Label {
                text: pack.name
                color: window.ink
                font.pixelSize: window.scaled(15)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: pack.manager.activeVersion > 0 ? "版本 " + pack.manager.activeVersion : "未安装"
                color: window.secondaryInk
                font.pixelSize: window.scaled(13)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
        GlassButton {
            text: "导入"
            Accessible.name: "导入" + pack.name
            enabled: !pack.manager.busy
            onClicked: {
                importPack.targetPack = pack.manager
                importPack.open()
            }
        }
        GlassButton {
            text: "回滚"
            Accessible.name: "回滚" + pack.name
            enabled: !pack.manager.busy && pack.manager.previousVersion > 0
            onClicked: pack.manager.rollback()
        }
    }

    Settings {
        id: preferences
        category: "Keyboard"
        property bool cloudEnabled: false
        property bool learn: true
        property bool clipboardHistory: false
        property real textScale: 1
        property alias dark: window.dark
        property alias solid: window.solid
        property alias highContrast: window.highContrast
        property alias lowMotion: window.lowMotion
    }

    LexiconPackManager {
        id: hotwords
        packId: "zh-hotwords"
        manifestUrl: "https://wuyijing-dev.github.io/VeloraIME/hotwords/manifest.json"
        trustedKeys: window.trustedPackKeys
        enabled: preferences.cloudEnabled
    }

    LexiconPackManager {
        id: dictionary
        packId: "zh-dictionary"
        manifestUrl: "https://wuyijing-dev.github.io/VeloraIME/dictionary/manifest.json"
        trustedKeys: window.trustedPackKeys
        enabled: preferences.cloudEnabled
    }

    VoiceRecognizer {
        id: voice
        onRecognized: (text, sessionToken) => panel.controller.inputLiteral(text, sessionToken)
        onFailed: reason => {
            window.recognitionError = reason
            recognitionNotice.restart()
        }
    }

    HandwritingRecognizer { id: handwriting }

    Timer {
        id: recognitionNotice
        interval: 3500
        onTriggered: window.recognitionError = ""
    }

    Item {
        id: content
        anchors.fill: parent
        clip: true

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: window.dark ? "#191a2b" : (window.page === "pin" ? "#faf0e9" : "#fbf6f0")
                }
                GradientStop {
                    position: 0.57
                    color: window.dark ? "#222239" : (window.page === "pin" ? "#fbf5f1" : "#f8f2ed")
                }
                GradientStop {
                    position: 1
                    color: window.dark ? "#333052" : (window.page === "pin" ? "#eadbd0" : "#d5deed")
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: !window.dark && !window.solid && !window.highContrast
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: window.page === "pin" ? "#39f8e7da" : "#38fff5e7" }
                GradientStop { position: 0.47; color: "transparent" }
                GradientStop { position: 1; color: window.page === "pin" ? "#66f7e5da" : "#b5bed3f2" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: window.dark && !window.solid && !window.highContrast

            ShapePath {
                startX: 0
                startY: content.height * 0.455
                strokeColor: "#248e76c9"
                strokeWidth: 1
                fillGradient: LinearGradient {
                    x1: 0
                    y1: content.height * 0.455
                    x2: content.width * 0.26
                    y2: content.height * 0.70
                    GradientStop { position: 0; color: "#674b4075" }
                    GradientStop { position: 1; color: "#0c27223d" }
                }
                PathCubic {
                    x: content.width * 0.53
                    y: content.height * 0.77
                    control1X: content.width * 0.19
                    control1Y: content.height * 0.42
                    control2X: content.width * 0.37
                    control2Y: content.height * 0.58
                }
                PathLine { x: 0; y: content.height * 0.8 }
                PathLine { x: 0; y: content.height * 0.455 }
            }

            ShapePath {
                startX: content.width
                startY: content.height * 0.37
                strokeColor: "#235c79bb"
                strokeWidth: 1
                fillGradient: LinearGradient {
                    x1: content.width
                    y1: content.height * 0.38
                    x2: content.width * 0.67
                    y2: content.height * 0.71
                    GradientStop { position: 0; color: "#3742527e" }
                    GradientStop { position: 1; color: "#0525263b" }
                }
                PathCubic {
                    x: content.width * 0.35
                    y: content.height * 0.78
                    control1X: content.width * 0.69
                    control1Y: content.height * 0.43
                    control2X: content.width * 0.50
                    control2Y: content.height * 0.58
                }
                PathLine { x: content.width; y: content.height * 0.80 }
                PathLine { x: content.width; y: content.height * 0.37 }
            }
        }

        RowLayout {
            id: chrome
            anchors.top: parent.top
            anchors.topMargin: window.compactHeight ? 7 : 20
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 2 * window.sideInset, 740)
            height: window.compactHeight ? 44 : 46
            spacing: 8

            Label {
                Layout.fillWidth: true
                color: window.ink
                text: window.page === "phrases" ? "我的短语"
                      : window.page === "fields" ? "专用字段" : "VeloraIME"
                font.family: window.displayFamily
                font.pixelSize: window.scaled(window.compactHeight ? 24 : 28)
                font.weight: Font.Bold
                font.letterSpacing: window.page === "editor" ? -0.65 : 0
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }

            GlassButton {
                objectName: "velora-settings-button"
                visible: window.page === "editor" || window.page === "pin"
                Layout.preferredWidth: window.page === "pin" ? 105 : 81
                Layout.preferredHeight: 44
                text: window.page === "pin" ? "结束 PIN 输入" : "设置"
                Accessible.name: text
                contentItem: HeaderGlyph { locked: window.page === "pin" }
                onClicked: {
                    if (window.page === "pin")
                        window.openPage("editor", note)
                    else
                        window.openSettings()
                }
            }

            GlassButton {
                visible: window.page !== "editor" && window.page !== "pin"
                text: "完成"
                onClicked: window.openPage("editor", note)
            }
        }

        Flickable {
            id: editorView
            anchors.top: chrome.bottom
            anchors.topMargin: window.compactHeight ? 7
                               : (window.page === "editor" ? (window.dark ? 24 : 42) : 14)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: panel.visible ? panel.height : 10
            clip: true
            contentWidth: width
            contentHeight: workspace.implicitHeight + 12
            boundsBehavior: Flickable.StopAtBounds

            function reveal(editor) {
                const top = editor.mapToItem(contentItem, 0, 0).y
                if (top < contentY)
                    contentY = top
                else if (top + Math.min(editor.height, height) > contentY + height)
                    contentY = top + Math.min(editor.height, height) - height
                returnToBounds()
            }

            ColumnLayout {
                id: workspace
                width: Math.min(editorView.width - 2 * window.sideInset, 740)
                x: (editorView.width - width) / 2
                spacing: 10

                ScrollView {
                    id: noteScroll
                    visible: window.page === "editor"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(window.compactHeight ? 66 : 124, editorView.height - 13)
                    clip: true
                    padding: 1
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    background: Rectangle {
                        radius: 14
                        color: "transparent"
                        border.color: note.activeFocus ? window.accentInk : window.paperEdge
                        border.width: window.highContrast ? 2 : 0
                        visible: window.highContrast
                    }

                    TextArea {
                        id: note
                        objectName: "velora-primary-editor"

                        readonly property bool chinese: panel.controller.language.startsWith("zh")
                        padding: 5
                        placeholderText: chinese ? "一起把想法写下来。" : "A little room for ideas."
                        wrapMode: TextEdit.Wrap
                        color: window.ink
                        placeholderTextColor: window.secondaryInk
                        font.pixelSize: window.scaled(window.compactHeight ? 18 : (chinese ? 23 : 21))
                        background: null
                        cursorDelegate: Rectangle { width: 2; radius: 1; color: window.accentInk }
                        Accessible.name: "正文"
                        onActiveFocusChanged: if (activeFocus) window.showFor(note)
                        TapHandler { onTapped: window.showFor(note) }
                    }
                }

                ColumnLayout {
                    visible: window.page === "fields"
                    Layout.fillWidth: true
                    spacing: 10

                    WorkspaceField {
                        id: email
                        objectName: "velora-email-editor"
                        Layout.fillWidth: true
                        placeholderText: "邮箱"
                        inputMethodHints: Qt.ImhEmailCharactersOnly
                        Accessible.name: "邮箱"
                    }

                }

                Item {
                    visible: window.page === "pin"
                    Layout.fillWidth: true
                    Layout.topMargin: window.compactHeight ? 0 : Math.min(73, editorView.height * 0.18)
                    Layout.preferredHeight: pinCard.height + 5

                    Rectangle {
                        anchors.fill: pinCard
                        anchors.topMargin: 6
                        anchors.bottomMargin: -3
                        radius: pinCard.radius
                        visible: !window.solid
                        color: window.dark ? "#550c0c16" : "#239c7360"
                    }

                    ChromeSurface {
                        id: pinCard
                        width: Math.min(parent.width, 264)
                        height: window.compactHeight ? 82 : 130
                        anchors.horizontalCenter: parent.horizontalCenter
                        radius: window.compactHeight ? 20 : 30
                        border.color: window.highContrast ? window.paperEdge
                                      : (window.dark ? "#78778d" : "#fffdfa")
                        gradient: Gradient {
                            GradientStop {
                                position: 0
                                color: window.dark ? "#e13b3b50" : "#f6fffbf8"
                            }
                            GradientStop {
                                position: 1
                                color: window.solid ? window.paper
                                       : (window.dark ? "#df2b2b3e" : "#edf5ebe5")
                            }
                        }

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: window.compactHeight ? 6 : 16
                            text: "PIN"
                            font.family: window.displayFamily
                            font.pixelSize: window.scaled(window.compactHeight ? 15 : 18)
                            font.weight: Font.DemiBold
                            color: window.secondaryInk
                        }

                        WorkspaceField {
                            id: pin
                            objectName: "velora-pin-editor"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: window.compactHeight ? 4 : 16
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            horizontalAlignment: TextInput.AlignHCenter
                            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
                                              | Qt.ImhNoPredictiveText
                            echoMode: TextInput.Password
                            passwordMaskDelay: 0
                            passwordCharacter: "\u2022"
                            maximumLength: 4
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                            font.pixelSize: window.scaled(window.compactHeight ? 22 : 29)
                            font.letterSpacing: window.compactHeight ? 6 : 10
                            background: null
                            cursorDelegate: Rectangle {
                                width: 2
                                radius: 1
                                color: window.accentInk
                                opacity: pin.length < pin.maximumLength ? 1 : 0
                            }
                            Accessible.name: "四位数字 PIN"
                        }
                    }
                }

                ColumnLayout {
                    visible: window.page === "phrases"
                    Layout.fillWidth: true
                    spacing: 9

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        WorkspaceField {
                            id: phraseCode
                            Layout.fillWidth: true
                            placeholderText: "编码"
                            inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText
                            Accessible.name: "词语编码"
                        }
                        WorkspaceField {
                            id: phraseText
                            Layout.fillWidth: true
                            placeholderText: "短语"
                            Accessible.name: "短语"
                        }
                        GlassButton {
                            text: "添加"
                            emphasized: true
                            enabled: phraseCode.text.trim().length > 0 && phraseText.text.trim().length > 0
                            onClicked: panel.controller.addUserPhrase(phraseCode.text, phraseText.text)
                        }
                    }

                    Label {
                        visible: window.dictionaryStatus.length > 0
                        color: window.secondaryInk
                        font.pixelSize: window.scaled(14)
                        text: window.dictionaryStatus
                    }

                    ListView {
                        id: phraseList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 224)
                        spacing: 4
                        clip: true
                        model: window.page === "phrases" ? panel.controller.userPhrases : []

                        delegate: RowLayout {
                            id: phrase
                            required property var modelData
                            width: phraseList.width
                            Label {
                                Layout.fillWidth: true
                                text: phrase.modelData.text
                                color: window.ink
                                font.pixelSize: window.scaled(17)
                                elide: Text.ElideRight
                            }
                            Label {
                                text: phrase.modelData.code
                                color: window.secondaryInk
                                font.pixelSize: window.scaled(13)
                                Layout.maximumWidth: 82
                                elide: Text.ElideRight
                            }
                            GlassButton {
                                text: "删除"
                                Accessible.name: "删除 " + phrase.modelData.text
                                onClicked: panel.controller.removeUserPhrase(phrase.modelData.code, phrase.modelData.text)
                            }
                        }
                    }
                }
            }
        }
    }

    KeyboardPanel {
        id: panel
        objectName: "velora-keyboard-panel"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: window.keyboardOpen
        backgroundSource: content
        dark: window.dark
        reduceTransparency: window.solid
        highContrast: window.highContrast
        reduceMotion: window.lowMotion
        fontScale: preferences.textScale
        voiceAvailable: voice.available
        listening: voice.listening
        handwritingAvailable: handwriting.available
        handwritingBusy: handwriting.busy
        handwritingCandidates: handwriting.candidates
        controller.supplementalEntries: dictionary.entries.concat(hotwords.entries)
        controller.learningEnabled: preferences.learn
        controller.clipboardHistoryEnabled: preferences.clipboardHistory
        onCloseRequested: window.keyboardOpen = false
        onPreferencesRequested: window.openSettings()
        onVoiceRequested: start => {
            if (start && !controller.sensitive)
                voice.start(controller.sessionToken)
            else
                voice.stop()
        }
        onVoiceCancelled: voice.cancel()
        onHandwritingRequested: (strokes, width, height) => {
            if (!panel.controller.sensitive) {
                window.inkSession = panel.controller.sessionToken
                handwriting.request(strokes, width, height)
            }
        }
        onHandwritingCancelled: {
            window.inkSession = ""
            handwriting.cancel()
        }
        onRecognitionTextAccepted: text => panel.controller.inputLiteral(text, window.inkSession)
    }

    Connections {
        target: panel.controller
        function onSessionTokenChanged() {
            voice.cancel()
            window.inkSession = ""
            handwriting.cancel()
        }
        function onUserDataOperationFinished(operation, success) {
            window.dictionaryStatus = success ? "已保存" : "词库操作未完成"
            if (success && operation === "add") {
                phraseCode.clear()
                phraseText.clear()
            }
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: panel.visible ? panel.top : parent.bottom
        anchors.bottomMargin: 8
        width: Math.min(implicitWidth, parent.width - 2 * window.sideInset)
        padding: 11
        wrapMode: Text.Wrap
        text: window.recognitionError
        visible: text.length > 0
        color: window.ink
        font.pixelSize: window.scaled(15)
        background: ChromeSurface { }
    }

    Dialog {
        id: settings
        objectName: "velora-settings-dialog"
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: 14
        width: Math.min(window.width - 22, 464)
        height: Math.min(window.height - 22, settingsLayout.implicitHeight + settingsHead.height + 30)
        title: "键盘设置"
        onClosed: {
            const editor = window.settingsEditor
            window.settingsEditor = null
            if (editor)
                Qt.callLater(() => window.showFor(editor))
        }

        Overlay.modal: Rectangle { color: window.dark ? "#9906080d" : "#7c15212d" }
        background: ChromeSurface { radius: 28; dense: true }

        header: RowLayout {
            id: settingsHead
            height: 61
            spacing: 8
            Label {
                Layout.leftMargin: 18
                Layout.fillWidth: true
                text: settings.title
                color: window.ink
                font.pixelSize: window.scaled(22)
                font.weight: Font.Bold
                Accessible.role: Accessible.Heading
            }
            GlassButton {
                Layout.rightMargin: 12
                text: "完成"
                onClicked: settings.close()
            }
        }

        contentItem: Flickable {
            id: settingsView
            clip: true
            contentWidth: width
            contentHeight: settingsLayout.implicitHeight + 5
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: settingsLayout
                width: settingsView.width
                spacing: 8

                SectionTitle { text: "外观" }
                SettingCard {
                    contentItem: ColumnLayout {
                        spacing: 1
                        SettingSwitch { Layout.fillWidth: true; text: "深色"; checked: window.dark; onToggled: window.dark = checked }
                        SettingSwitch { Layout.fillWidth: true; text: "减少透明度"; checked: window.solid; onToggled: window.solid = checked }
                        SettingSwitch { Layout.fillWidth: true; text: "提高对比度"; checked: window.highContrast; onToggled: window.highContrast = checked }
                        SettingSwitch { Layout.fillWidth: true; text: "减少动态效果"; checked: window.lowMotion; onToggled: window.lowMotion = checked }
                        SettingSwitch {
                            Layout.fillWidth: true
                            text: "较大文字"
                            checked: preferences.textScale > 1
                            onToggled: preferences.textScale = checked ? 1.23 : 1
                        }
                    }
                }

                SectionTitle { text: "个人词库" }
                SettingCard {
                    contentItem: ColumnLayout {
                        spacing: 4
                        SettingSwitch { Layout.fillWidth: true; text: "自学习"; checked: preferences.learn; onToggled: preferences.learn = checked }
                        SettingSwitch {
                            Layout.fillWidth: true
                            text: "剪贴板历史"
                            checked: preferences.clipboardHistory
                            onToggled: preferences.clipboardHistory = checked
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: settingsView.width < 290 && preferences.textScale > 1 ? 1 : 2
                            rowSpacing: 5
                            columnSpacing: 6
                            GlassButton {
                                Layout.fillWidth: true
                                objectName: "velora-user-phrases-button"
                                text: "我的短语"
                                onClicked: {
                                    panel.controller.refreshUserPhrases()
                                    window.openPage("phrases", phraseCode)
                                }
                            }
                            GlassButton { Layout.fillWidth: true; text: "清除学习"; onClicked: clearLearning.open() }
                            GlassButton { Layout.fillWidth: true; text: "导出个人词库"; onClicked: exportProfile.open() }
                            GlassButton { Layout.fillWidth: true; text: "导入个人词库"; onClicked: importProfile.open() }
                        }
                        Label {
                            visible: window.dictionaryStatus.length > 0
                            text: window.dictionaryStatus
                            color: window.secondaryInk
                            font.pixelSize: window.scaled(14)
                        }
                    }
                }

                SectionTitle { text: "公开词包" }
                SettingCard {
                    contentItem: ColumnLayout {
                        spacing: 5
                        SettingSwitch {
                            Layout.fillWidth: true
                            text: "自动下载公开词包"
                            checked: preferences.cloudEnabled
                            onToggled: preferences.cloudEnabled = checked
                        }
                        PackRow { name: "扩展词库"; manager: dictionary }
                        PackRow { name: "热词"; manager: hotwords }
                        GlassButton {
                            Layout.fillWidth: true
                            text: dictionary.busy || hotwords.busy ? "正在检查" : "检查词包更新"
                            enabled: preferences.cloudEnabled && !dictionary.busy && !hotwords.busy
                            onClicked: {
                                dictionary.checkForUpdates()
                                hotwords.checkForUpdates()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            wrapMode: Text.Wrap
                            color: window.ink
                            font.pixelSize: window.scaled(14)
                            text: dictionary.lastError || hotwords.lastError
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: settingsView.width < 285 && preferences.textScale > 1 ? 1 : 2
                    columnSpacing: 6
                    rowSpacing: 5
                    GlassButton {
                        Layout.fillWidth: true
                        objectName: "velora-special-fields-button"
                        text: "邮箱输入示例"
                        onClicked: window.openPage("fields", email)
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        objectName: "velora-secure-example-button"
                        text: "PIN 输入示例"
                        onClicked: window.openPage("pin", pin)
                    }
                }
            }
        }
    }

    Dialog {
        id: clearLearning
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 24, 340)
        title: "清除学习记录"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: ChromeSurface { radius: 22; dense: true }
        Label {
            width: clearLearning.availableWidth
            wrapMode: Text.Wrap
            text: "清除此用户配置的学习权重。"
            color: window.ink
            font.pixelSize: window.scaled(16)
        }
        onAccepted: panel.controller.clearLearning()
    }

    FileDialog {
        id: exportProfile
        title: "导出个人词库"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON (*.json)"]
        onAccepted: panel.controller.exportUserData(selectedFile)
    }

    FileDialog {
        id: importProfile
        title: "导入个人词库"
        nameFilters: ["JSON (*.json)"]
        onAccepted: panel.controller.importUserData(selectedFile)
    }

    FolderDialog {
        id: importPack
        title: "选择包含签名清单的词包目录"
        property LexiconPackManager targetPack
        onAccepted: if (targetPack) targetPack.importDirectory(selectedFolder)
    }

    Component.onCompleted: note.forceActiveFocus()
}
