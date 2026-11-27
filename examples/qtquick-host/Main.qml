import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rk.TouchKeyboard 1.0

ApplicationWindow {
    id: window
    width: 480
    height: 760
    minimumWidth: 340
    visible: true
    title: qsTr("Qt Quick 编辑示例")

    property bool keyboardOpen: true
    property Item activeEditor: note

    function showKeyboard(editor: Item): void {
        activeEditor = editor;
        keyboardOpen = true;
        Qt.callLater(revealEditor);
    }

    function revealEditor(): void {
        if (!activeEditor || !keyboardOpen)
            return;
        const fieldTop = activeEditor.mapToItem(form, 0, 0).y + form.y;
        const fieldBottom = fieldTop + activeEditor.height;
        if (fieldTop < viewport.contentY)
            viewport.contentY = fieldTop - 8;
        else if (fieldBottom > viewport.contentY + viewport.height)
            viewport.contentY = fieldBottom - viewport.height + 8;
        viewport.returnToBounds();
    }

    Item {
        id: page
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#f6f8fc" }
                GradientStop { position: 1; color: "#dfe8f5" }
            }
        }

        Flickable {
            id: viewport
            objectName: "hostViewport"
            anchors.fill: parent
            anchors.bottomMargin: keyboard.keyboardRectangle.height
            clip: true
            contentWidth: width
            contentHeight: form.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: form
                x: 16
                y: 16
                width: viewport.width - 32
                spacing: 10

                Label {
                    text: qsTr("编辑表单")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }

                TextArea {
                    id: note
                    objectName: "hostNote"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    placeholderText: qsTr("备注")
                    focus: true
                    wrapMode: TextEdit.Wrap
                    Accessible.name: qsTr("备注")
                    onActiveFocusChanged: if (activeFocus) window.showKeyboard(note)
                    TapHandler { onTapped: window.showKeyboard(note) }
                }

                TextField {
                    id: email
                    objectName: "hostEmail"
                    Layout.fillWidth: true
                    placeholderText: qsTr("邮箱")
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                    EnterKey.type: Qt.EnterKeyNext
                    Accessible.name: qsTr("邮箱")
                    onActiveFocusChanged: if (activeFocus) window.showKeyboard(email)
                    TapHandler { onTapped: window.showKeyboard(email) }
                }

                TextField {
                    id: address
                    objectName: "hostUrl"
                    Layout.fillWidth: true
                    placeholderText: qsTr("网址")
                    inputMethodHints: Qt.ImhUrlCharactersOnly
                    Accessible.name: qsTr("网址")
                    onActiveFocusChanged: if (activeFocus) window.showKeyboard(address)
                    TapHandler { onTapped: window.showKeyboard(address) }
                }

                TextField {
                    id: amount
                    objectName: "hostAmount"
                    Layout.fillWidth: true
                    placeholderText: qsTr("金额")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("金额")
                    onActiveFocusChanged: if (activeFocus) window.showKeyboard(amount)
                    TapHandler { onTapped: window.showKeyboard(amount) }
                }

                TextField {
                    id: pin
                    objectName: "hostPin"
                    Layout.fillWidth: true
                    placeholderText: "PIN"
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
                    echoMode: TextInput.Password
                    EnterKey.type: Qt.EnterKeyDone
                    Accessible.name: "PIN"
                    onActiveFocusChanged: if (activeFocus) window.showKeyboard(pin)
                    TapHandler { onTapped: window.showKeyboard(pin) }
                }
            }
        }
    }

    KeyboardPanel {
        id: keyboard
        objectName: "hostKeyboard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: window.keyboardOpen
        backgroundSource: page
        controller.profileId: "example-editor"
        onCloseRequested: window.keyboardOpen = false
        onKeyboardRectangleChanged: Qt.callLater(window.revealEditor)
    }
}
