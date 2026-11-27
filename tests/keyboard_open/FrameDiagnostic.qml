import QtQuick
import QtQuick.Controls
import Rk.TouchKeyboard 1.0

ApplicationWindow {
    id: root
    width: 414
    height: 736
    visible: true
    title: "VeloraIME frame probe"

    required property int diagnosticGraphicsMode

    Item {
        id: backdrop
        anchors.fill: parent
        clip: true

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#eef1fb" }
                GradientStop { position: 1; color: "#719abe" }
            }
        }
        Rectangle {
            width: parent.width * 0.74
            height: parent.height * 0.75
            y: parent.height * 0.38
            radius: width / 3
            color: "#e0b0c9"
            rotation: -22
            SequentialAnimation on x {
                loops: Animation.Infinite
                NumberAnimation { from: -90; to: backdrop.width * 0.57; duration: 1380 }
                NumberAnimation { to: -90; duration: 1380 }
            }
        }
        Rectangle {
            width: parent.width * 0.60
            height: parent.height * 0.52
            y: parent.height * 0.52
            radius: width * 0.40
            color: "#9fc4e7"
            rotation: 23
            SequentialAnimation on x {
                loops: Animation.Infinite
                NumberAnimation { from: backdrop.width * 0.60; to: -70; duration: 1790 }
                NumberAnimation { to: backdrop.width * 0.60; duration: 1790 }
            }
        }

        TextArea {
            objectName: "hostNote"
            x: 14
            y: 24
            width: parent.width - 28
            height: 86
            placeholderText: "候选栏帧采样"
            focus: true
        }
    }

    KeyboardPanel {
        objectName: "hostKeyboard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        backgroundSource: backdrop
        graphicsMode: root.diagnosticGraphicsMode
        controller.language: "zh-CN"
    }
}
