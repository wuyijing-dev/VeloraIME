import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Rk.TouchKeyboard

ApplicationWindow {
    id: window

    width: 1024
    height: 600
    minimumWidth: 360
    minimumHeight: 420
    visible: true
    title: "VeloraIME"

    required property var trustedPackKeys
    property bool dark: false
    property bool keyboardOpen: true
    property bool solid: false
    property bool highContrast: false
    property bool lowMotion: false
    property string inkSession: ""
    property string recognitionError: ""
    property string dictionaryStatus: ""
    property bool managingPhrases: false
    readonly property color ink: dark ? "#f0f4ff" : "#172137"
    readonly property color inputFill: dark ? "#29354a" : "#ffffff"

    Settings {
        id: preferences
        category: "Keyboard"
        property bool cloudEnabled: false
        property bool learn: true
        property bool clipboardHistory: false
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

    VoiceRecognizer {
        id: voice
        onRecognized: (text, sessionToken) => panel.controller.inputLiteral(text, sessionToken)
        onFailed: reason => {
            window.recognitionError = reason
            recognitionNotice.restart()
        }
    }

    HandwritingRecognizer {
        id: handwriting
    }

    Timer {
        id: recognitionNotice
        interval: 3500
        onTriggered: window.recognitionError = ""
    }
    LexiconPackManager {
        id: dictionary
        packId: "zh-dictionary"
        manifestUrl: "https://wuyijing-dev.github.io/VeloraIME/dictionary/manifest.json"
        trustedKeys: window.trustedPackKeys
        enabled: preferences.cloudEnabled
    }

    Item {
        id: content

        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: window.dark ? "#151e32" : "#e9f0ff" }
                GradientStop { position: 0.55; color: window.dark ? "#303751" : "#d9e7fb" }
                GradientStop { position: 1; color: window.dark ? "#2d344a" : "#dceeea" }
            }
        }

        Flickable {
            id: editorView

            anchors.fill: parent
            anchors.bottomMargin: panel.visible ? panel.height : 0
            clip: true
            contentWidth: width
            contentHeight: fields.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds

            function reveal(editor) {
                const y = editor.mapToItem(contentItem, 0, 0).y
                if (y < contentY)
                    contentY = y
                else if (y + editor.height > contentY + height)
                    contentY = y + editor.height - height
                returnToBounds()
            }

            ColumnLayout {
                id: fields

                width: Math.min(editorView.width - 32, 760)
                y: 20
                x: (editorView.width - width) / 2
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: window.managingPhrases ? "我的短语" : "VeloraIME"
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        color: window.ink
                    }
                    Button {
                        text: window.managingPhrases ? "完成" : "设置"
                        onClicked: {
                            if (window.managingPhrases) {
                                window.managingPhrases = false
                                window.showFor(note)
                            } else {
                                settings.open()
                            }
                        }
                    }
                }

                TextArea {
                    id: note
                    objectName: "velora-primary-editor"
                    visible: !window.managingPhrases

                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
                    placeholderText: "输入中文或英文"
                    wrapMode: TextEdit.Wrap
                    color: window.ink
                    placeholderTextColor: window.dark ? "#bdc8da" : "#465471"
                    background: Rectangle { radius: 12; color: window.inputFill }
                    Accessible.name: "正文"
                    onActiveFocusChanged: if (activeFocus) window.showFor(note)
                    TapHandler { onTapped: window.showFor(note) }
                }

                TextField {
                    id: email

                    visible: !window.managingPhrases
                    Layout.fillWidth: true
                    placeholderText: "邮箱"
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                    color: window.ink
                    placeholderTextColor: note.placeholderTextColor
                    background: Rectangle { radius: 12; color: window.inputFill }
                    Accessible.name: "邮箱"
                    onActiveFocusChanged: if (activeFocus) window.showFor(email)
                    TapHandler { onTapped: window.showFor(email) }
                }

                TextField {
                    id: pin

                    visible: !window.managingPhrases
                    Layout.fillWidth: true
                    placeholderText: "PIN"
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
                    echoMode: TextInput.Password
                    color: window.ink
                    placeholderTextColor: note.placeholderTextColor
                    background: Rectangle { radius: 12; color: window.inputFill }
                    Accessible.name: "PIN"
                    onActiveFocusChanged: if (activeFocus) window.showFor(pin)
                    TapHandler { onTapped: window.showFor(pin) }
                }

                ColumnLayout {
                    visible: window.managingPhrases
                    Layout.fillWidth: true
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: phraseCode
                            Layout.fillWidth: true
                            placeholderText: "编码"
                            inputMethodHints: Qt.ImhLatinOnly | Qt.ImhNoPredictiveText
                            onActiveFocusChanged: if (activeFocus) window.showFor(phraseCode)
                            TapHandler { onTapped: window.showFor(phraseCode) }
                        }
                        TextField {
                            id: phraseText
                            Layout.fillWidth: true
                            placeholderText: "短语"
                            onActiveFocusChanged: if (activeFocus) window.showFor(phraseText)
                            TapHandler { onTapped: window.showFor(phraseText) }
                        }
                        Button {
                            text: "添加"
                            enabled: phraseCode.text.trim().length > 0 && phraseText.text.trim().length > 0
                            onClicked: panel.controller.addUserPhrase(phraseCode.text, phraseText.text)
                        }
                    }
                    Label {
                        visible: window.dictionaryStatus.length > 0
                        color: window.ink
                        text: window.dictionaryStatus
                    }
                    ListView {
                        id: phraseList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 220)
                        clip: true
                        model: window.managingPhrases ? panel.controller.userPhrases : []
                        delegate: RowLayout {
                            required property var modelData
                            width: phraseList.width
                            Label {
                                Layout.fillWidth: true
                                text: modelData.text
                                color: window.ink
                                elide: Text.ElideRight
                            }
                            Label { text: modelData.code; color: window.ink }
                            Button {
                                text: "删除"
                                onClicked: panel.controller.removeUserPhrase(modelData.code, modelData.text)
                            }
                        }
                    }
                }
            }
        }
    }

    function showFor(editor) {
        if (!editor.activeFocus)
            editor.forceActiveFocus()
        keyboardOpen = true
        Qt.callLater(() => editorView.reveal(editor))
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
        voiceAvailable: voice.available
        listening: voice.listening
        handwritingAvailable: handwriting.available
        handwritingBusy: handwriting.busy
        handwritingCandidates: handwriting.candidates
        controller.supplementalEntries: dictionary.entries.concat(hotwords.entries)
        controller.learningEnabled: preferences.learn
        controller.clipboardHistoryEnabled: preferences.clipboardHistory
        onCloseRequested: window.keyboardOpen = false
        onPreferencesRequested: settings.open()
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
        anchors.bottom: panel.top
        anchors.bottomMargin: 8
        width: Math.min(implicitWidth, parent.width - 32)
        padding: 9
        wrapMode: Text.Wrap
        text: window.recognitionError
        visible: text.length > 0
        color: window.ink
        background: Rectangle { radius: 10; color: window.inputFill }
    }

    Dialog {
        id: settings

        anchors.centerIn: parent
        modal: true
        title: "键盘设置"
        standardButtons: Dialog.Close
        width: Math.min(parent.width - 24, 450)
        height: Math.min(implicitHeight, parent.height - 24)
        onOpened: window.keyboardOpen = false

        contentItem: ScrollView {
            contentWidth: availableWidth
            contentHeight: settingsLayout.implicitHeight
            ColumnLayout {
            id: settingsLayout
            width: parent.width
            Switch { text: "深色"; checked: window.dark; onToggled: window.dark = checked }
            Switch { text: "减少透明度"; checked: window.solid; onToggled: window.solid = checked }
            Switch { text: "提高对比度"; checked: window.highContrast; onToggled: window.highContrast = checked }
            Switch { text: "减少动态效果"; checked: window.lowMotion; onToggled: window.lowMotion = checked }
            Switch { text: "自学习"; checked: preferences.learn; onToggled: preferences.learn = checked }
            Switch {
                text: "剪贴板历史"
                checked: preferences.clipboardHistory
                onToggled: preferences.clipboardHistory = checked
            }
            RowLayout {
                Button {
                    text: "我的短语"
                    onClicked: {
                        settings.close()
                        window.managingPhrases = true
                        panel.controller.refreshUserPhrases()
                        Qt.callLater(() => window.showFor(phraseCode))
                    }
                }
                Button {
                    text: "清除学习"
                    onClicked: clearLearning.open()
                }
            }
            RowLayout {
                Button { text: "导出个人词库"; onClicked: exportProfile.open() }
                Button { text: "导入个人词库"; onClicked: importProfile.open() }
            }
            Switch {
                text: "自动下载公开词包"
                checked: preferences.cloudEnabled
                onToggled: preferences.cloudEnabled = checked
            }
            RowLayout {
                Label {
                    Layout.fillWidth: true
                    text: "扩展词库 · " + (dictionary.activeVersion || "未安装")
                }
                Button {
                    text: "导入"
                    enabled: !dictionary.busy
                    onClicked: {
                        importPack.targetPack = dictionary
                        importPack.open()
                    }
                }
                Button {
                    text: "回滚"
                    enabled: !dictionary.busy && dictionary.previousVersion > 0
                    onClicked: dictionary.rollback()
                }
            }
            RowLayout {
                Label {
                    Layout.fillWidth: true
                    text: "热词 · " + (hotwords.activeVersion || "未安装")
                }
                Button {
                    text: "导入"
                    enabled: !hotwords.busy
                    onClicked: {
                        importPack.targetPack = hotwords
                        importPack.open()
                    }
                }
                Button {
                    text: "回滚"
                    enabled: !hotwords.busy && hotwords.previousVersion > 0
                    onClicked: hotwords.rollback()
                }
            }
            Button {
                text: dictionary.busy || hotwords.busy ? "正在检查" : "立即检查词包"
                enabled: preferences.cloudEnabled && !dictionary.busy && !hotwords.busy
                onClicked: {
                    dictionary.checkForUpdates()
                    hotwords.checkForUpdates()
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                visible: text.length > 0
                wrapMode: Text.Wrap
                text: dictionary.lastError || hotwords.lastError
            }
            Label {
                visible: window.dictionaryStatus.length > 0
                text: window.dictionaryStatus
            }
            }
        }
    }

    Dialog {
        id: clearLearning
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 360)
        title: "清除学习记录"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "清除此用户配置的学习权重。" }
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
