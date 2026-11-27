import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 440
    height: 156
    visibility: Window.Maximized
    title: pin ? "Velora PIN Client" : "Velora Text Client"
    readonly property bool pin: Qt.application.arguments.includes("pin")
    Component.onCompleted: console.log("CLIENT_READY")

    onActiveChanged: {
        if (active) {
            field.forceActiveFocus()
            Qt.inputMethod.show()
        }
    }

    TextField {
        id: field
        x: 16
        y: 25
        width: parent.width - 32
        placeholderText: window.pin ? "PIN probe" : "Cross application probe"
        focus: true
        echoMode: window.pin ? TextInput.Password : TextInput.Normal
        inputMethodHints: window.pin ? Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhHiddenText : Qt.ImhNone

        onActiveFocusChanged: if (activeFocus) Qt.inputMethod.show()
        onTextChanged: {
            if (window.pin && text === "12" && displayText !== text)
                console.log("PIN_MASKED")
            if (!window.pin && text === "q")
                console.log("TEXT_Q")
            if (!window.pin && text === "qw")
                console.log("TEXT_QW")
        }

        TapHandler {
            onTapped: {
                field.forceActiveFocus()
                Qt.inputMethod.show()
            }
        }
    }
}
