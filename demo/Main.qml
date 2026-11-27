pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
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
    readonly property int sideInset: width < 520 ? 16 : 24
    readonly property string displayFamily: Qt.fontFamilies().includes("Nunito") ? "Nunito" : font.family
    readonly property color ink: highContrast ? (dark ? "#ffffff" : "#0b111b")
                                               : (dark ? "#f4f6fa" : "#1c222d")
    readonly property color secondaryInk: dark ? "#bdc8d8" : "#535f70"
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

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: window.dark ? "#272a33" : "#f5f6f8" }
                GradientStop { position: 0.55; color: window.dark ? "#1a1d26" : "#e2e7ef" }
                GradientStop { position: 1; color: window.dark ? "#303b4d" : "#bac9dc" }
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: !window.solid
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: window.dark ? "#42394d6e" : "#6af6f8fb" }
                GradientStop { position: 0.65; color: "transparent" }
                GradientStop { position: 1; color: window.dark ? "#302a233f" : "#3ba2b6d0" }
            }
        }

        RowLayout {
            id: chrome
            anchors.top: parent.top
            anchors.topMargin: window.compactHeight ? 8 : 17
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 2 * window.sideInset, 740)
            height: window.compactHeight ? 46 : 56
            spacing: 8

            Label {
                Layout.leftMargin: 3
                Layout.fillWidth: true
                color: window.ink
                text: window.page === "phrases" ? "我的短语"
                      : window.page === "fields" ? "专用字段" : "VeloraIME"
                font.family: window.displayFamily
                font.pixelSize: window.scaled(window.compactHeight ? 25 : 31)
                font.weight: Font.Bold
                font.letterSpacing: window.page === "editor" ? -0.65 : 0
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }

            GlassButton {
                objectName: "velora-settings-button"
                text: window.page === "editor" ? "设置" : "完成"
                onClicked: {
                    if (window.page === "editor")
                        window.openSettings()
                    else
                        window.openPage("editor", note)
                }
            }
        }

        Flickable {
            id: editorView
            anchors.top: chrome.bottom
            anchors.topMargin: window.compactHeight ? 7 : 11
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
                        radius: window.compactHeight ? 18 : 24
                        color: window.paper
                        border.color: note.activeFocus && window.highContrast ? window.accentInk : window.paperEdge
                        border.width: window.highContrast ? 2 : 1
                    }

                    TextArea {
                        id: note
                        objectName: "velora-primary-editor"

                        padding: window.compactHeight ? 13 : 18
                        placeholderText: "写点什么…"
                        wrapMode: TextEdit.Wrap
                        color: window.ink
                        placeholderTextColor: window.secondaryInk
                        font.pixelSize: window.scaled(18)
                        background: null
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

                    WorkspaceField {
                        id: pin
                        objectName: "velora-pin-editor"
                        Layout.fillWidth: true
                        placeholderText: "PIN"
                        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
                        echoMode: TextInput.Password
                        Accessible.name: "PIN"
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

                GlassButton {
                    Layout.fillWidth: true
                    objectName: "velora-special-fields-button"
                    text: "打开邮箱与 PIN 字段"
                    onClicked: window.openPage("fields", email)
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
