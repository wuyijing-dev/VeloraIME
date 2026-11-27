import QtQuick
import QtQuick.Controls
import QtTest
import Rk.TouchKeyboard

TestCase {
    id: testCase
    name: "TouchKeyboard"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Item {
        anchors.fill: parent

        TextField {
            id: editor
            width: 400
        }

        TextField {
            id: pin
            anchors.top: editor.bottom
            width: editor.width
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData
        }

        KeyboardPanel {
            id: keyboard
            width: parent.width
            anchors.bottom: parent.bottom
            graphicsMode: 3
            reduceMotion: true
            onCloseRequested: visible = false
        }
    }

    SignalSpy {
        id: inkRequests
        target: keyboard
        signalName: "handwritingRequested"
    }

    SignalSpy {
        id: voiceRequests
        target: keyboard
        signalName: "voiceRequested"
    }

    function init() {
        testCase.width = 640;
        testCase.height = 480;
        keyboard.visible = true;
        keyboard.controller.cancelComposition();
        keyboard.controller.language = "en-US";
        keyboard.controller.pinyinMode = "full";
        keyboard.controller.learningEnabled = false;
        keyboard.oneHanded = false;
        keyboard.alignment = "center";
        keyboard.voiceAvailable = false;
        keyboard.handwritingAvailable = false;
        inkRequests.clear();
        voiceRequests.clear();
        editor.text = "";
        pin.text = "";
        editor.forceActiveFocus();
        tryVerify(() => keyboard.controller.active);
        verify(waitForPolish(keyboard.Window.window));
    }

    function key(name) {
        const found = findChild(keyboard, name);
        verify(found && found.visible && found.enabled, name);
        return found;
    }

    function test_multitouchRetainsFocus() {
        const q = key("key-text-q");
        const w = key("key-text-w");
        const touch = touchEvent(keyboard);
        touch.press(0, q).press(1, w).commit();
        touch.release(0, q).commit();
        touch.release(1, w).commit();
        compare(editor.text, "qw");
        verify(editor.activeFocus);
    }

    function test_phonePortraitTouchGeometry() {
        testCase.width = 414;
        testCase.height = 736;
        verify(waitForPolish(keyboard.Window.window));
        compare(keyboard.width, 414);
        verify(keyboard.height >= 275 && keyboard.height <= 325);
        verify(keyboard.needsCompactQwerty);

        const q = key("key-text-q");
        const p = key("key-text-p");
        const qLocation = q.mapToItem(keyboard, 0, 0);
        const pLocation = p.mapToItem(keyboard, p.width, 0);
        verify(q.width >= 38 && q.width < 44);
        verify(q.height >= 48);
        verify(qLocation.x >= 7 && pLocation.x <= keyboard.width - 7);
        for (const name of ["keyboardLanguage", "keyboardLayouts", "keyboardEmoji", "keyboardClipboard", "hideKeyboard", "key-backspace", "key-space", "key-enter", "key-symbol"])
            verify(key(name).width >= 44 && key(name).height >= 44, name);

        keyboard.controller.language = "zh-CN";
        keyboard.controller.pinyinMode = "t9";
        verify(waitForPolish(keyboard.Window.window));
        const two = key("key-text-2");
        verify(two.width >= 44 && two.height >= 44);
        mouseClick(two);
        compare(keyboard.controller.preedit, "2");
        verify(editor.activeFocus);
    }

    function test_shiftOnlyAffectsNextLetter() {
        mouseClick(key("key-shift"));
        mouseClick(key("key-text-q"));
        mouseClick(key("key-text-w"));
        compare(editor.text, "Qw");
        verify(editor.activeFocus);
    }

    function test_spaceDragMovesCursorWithoutInsertingSpace() {
        editor.text = "abcdef";
        editor.cursorPosition = 4;
        const space = key("key-space");
        const middle = space.width / 2;
        const row = space.height / 2;
        const touch = touchEvent(keyboard);
        touch.press(0, space, middle, row).commit();
        touch.move(0, space, middle - 40, row).commit();
        wait(26);
        touch.move(0, space, middle - 70, row).commit();
        wait(26);
        touch.release(0, space, middle - 70, row).commit();
        verify(editor.cursorPosition < 4);
        compare(editor.text, "abcdef");
        verify(editor.activeFocus);
    }

    function test_englishSwipeOffersWords() {
        const h = key("key-text-h");
        const e = key("key-text-e");
        const l = key("key-text-l");
        const o = key("key-text-o");
        const touch = touchEvent(keyboard);
        touch.press(0, h).commit();
        touch.move(0, e).commit();
        wait(26);
        touch.move(0, l).commit();
        wait(26);
        touch.move(0, o).commit();
        wait(26);
        touch.release(0, o).commit();
        tryVerify(() => keyboard.controller.candidates.some(candidate => candidate.text === "hello"));
        compare(editor.text, "");
        keyboard.controller.selectCandidate(keyboard.controller.candidates.find(candidate => candidate.text === "hello").id);
        compare(editor.text.trim(), "hello");
        verify(editor.activeFocus);
    }

    function test_inkSubmitsStrokesWithoutChangingFocus() {
        keyboard.handwritingAvailable = true;
        verify(waitForPolish(keyboard.Window.window));
        mouseClick(key("keyboardInk"));
        verify(waitForPolish(keyboard.Window.window));
        const pad = key("keyboardInkPad");
        mousePress(pad, 36, 40);
        mouseMove(pad, 90, 65, 30);
        mouseRelease(pad, 120, 80);
        inkRequests.wait(1500);
        compare(inkRequests.count, 1);
        const [strokes, width, height] = inkRequests.signalArguments[0];
        compare(strokes.length, 1);
        verify(strokes[0].length >= 2);
        verify(width > 100 && height > 90);
        verify(editor.activeFocus);
    }

    function test_voiceToolHiddenInPrivacy() {
        keyboard.voiceAvailable = true;
        verify(waitForPolish(keyboard.Window.window));
        mouseClick(key("keyboardVoice"));
        compare(voiceRequests.count, 1);
        compare(voiceRequests.signalArguments[0][0], true);
        pin.forceActiveFocus();
        tryCompare(keyboard.controller, "sensitive", true);
        verify(!findChild(keyboard, "keyboardVoice").visible);
    }

    function test_numericPrivacyHidesTools() {
        pin.forceActiveFocus();
        tryCompare(keyboard.controller, "sensitive", true);
        tryCompare(keyboard, "inputMode", "number");
        verify(waitForPolish(keyboard.Window.window));
        verify(!findChild(keyboard, "keyboardClipboard").visible);
        verify(!findChild(keyboard, "keyboardEmoji").visible);
        mouseClick(key("key-text-1"));
        compare(pin.text, "1");
        compare(keyboard.controller.candidates.length, 0);
        verify(pin.activeFocus);
    }

    function test_nineKeySymbolPageInsertsDigitsDirectly() {
        keyboard.controller.language = "zh-CN";
        keyboard.controller.pinyinMode = "t9";
        verify(waitForPolish(keyboard.Window.window));
        mouseClick(key("key-symbol"));
        verify(waitForPolish(keyboard.Window.window));
        mouseClick(key("key-text-5"));
        compare(editor.text, "5");
        compare(keyboard.controller.preedit, "");
    }

    function test_longPressDoesNotCommitBaseKey() {
        const a = key("key-text-a");
        mousePress(a);
        wait(520);
        const alternatives = findChild(keyboard, "keyboardAlternates");
        tryCompare(alternatives, "visible", true);
        mouseRelease(a);
        compare(editor.text, "");
        mouseClick(alternatives, 16, 24);
        compare(editor.text, "à");
        verify(editor.activeFocus);
    }

    function test_deleteLongPressRepeats() {
        editor.text = "abcdef";
        editor.cursorPosition = editor.length;
        const backspace = key("key-backspace");
        mousePress(backspace);
        wait(620);
        mouseRelease(backspace);
        verify(editor.length < 4);
        verify(editor.activeFocus);
    }

    function test_closeRequestsHide() {
        mouseClick(key("hideKeyboard"));
        verify(!keyboard.visible);
        compare(keyboard.keyboardRectangle.height, 0);
    }
}
