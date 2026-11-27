import QtQuick
import QtQuick.Controls
import Rk.TouchKeyboard

ApplicationWindow {
    id: probe
    width: 414
    height: 736
    visible: true
    title: "VeloraIME input test"
    required property string scenario
    property bool loadKeyboard: true

    Rectangle {
        id: backdrop
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#f9f2eb" }
            GradientStop { position: 1; color: "#d9e3f6" }
        }
    }

    onScenarioChanged: {
        if (scenario === "pin")
            pin.forceActiveFocus()
        else
            editor.forceActiveFocus()
    }

    TextArea {
        id: editor
        objectName: "probeEditor"
        width: parent.width - 24
        height: 140
        x: 12
        y: 12
        focus: true
        visible: probe.scenario !== "pin"
        Accessible.name: "Test editor"
    }

    TextField {
        id: pin
        objectName: "probePin"
        x: 12
        y: 12
        width: parent.width - 24
        height: 48
        visible: probe.scenario === "pin"
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
        maximumLength: 4
        Accessible.name: "Test PIN"
    }

    Loader {
        anchors.bottom: parent.bottom
        width: parent.width
        active: probe.loadKeyboard
        sourceComponent: KeyboardPanel {
            objectName: "probeKeyboard"
            backgroundSource: backdrop
            controller.language: probe.scenario === "pinyin" ? "zh-CN" : "en-US"
            controller.learningEnabled: false
        }
    }

    Component.onCompleted: {
        if (scenario === "pin")
            pin.forceActiveFocus()
        else
            editor.forceActiveFocus()
    }
}
