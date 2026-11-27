#include "keyboardcontroller.h"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <memory>

using namespace Qt::StringLiterals;

namespace {
QQuickItem *editor(QQuickWindow &window, const char *name)
{
    return window.findChild<QQuickItem *>(QString::fromLatin1(name));
}

QString findCandidate(const KeyboardController &controller, const QString &word)
{
    for (const auto &value : controller.candidates()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(u"text"_s) == word)
            return candidate.value(u"id"_s).toString();
    }
    return {};
}
}

class EngineIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void nativeEditingAndLimits();
    void cancelOnFocusOrHide();
    void pinyinModes();
    void privacyAndClipboard();
    void profileAndSupplementalPacks();
    void swipeAndRecognitionDelivery();
    void clipboardHistoryAndRecentEmoji();

private:
    QTemporaryDir m_data;
    QQmlEngine m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<KeyboardController> m_controller;
};

void EngineIntegration::initTestCase()
{
    QVERIFY(m_data.isValid());
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_data.path()));
    QCoreApplication::setApplicationName(u"VeloraIME-tests"_s);
}

void EngineIntegration::init()
{
    m_controller.reset();
    m_window.reset();

    QQmlComponent component(&m_engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 600
            height: 300
            visible: true
            TextInput {
                objectName: "first"
                width: 200
                height: 40
                EnterKey.type: Qt.EnterKeySearch
            }
            TextInput {
                objectName: "second"
                x: 220
                width: 200
                height: 40
            }
            TextInput {
                objectName: "password"
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                x: 440
                width: 120
                height: 40
            }
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    m_window.reset(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY(m_window != nullptr);
    m_window->requestActivate();
    editor(*m_window, "first")->forceActiveFocus();
    QTRY_COMPARE(QGuiApplication::focusObject(), editor(*m_window, "first"));

    m_controller = std::make_unique<KeyboardController>();
    m_controller->setWindow(m_window.get());
    m_controller->setProfileId(QUuid::createUuid().toString());
    QVERIFY(m_controller->active());
}

void EngineIntegration::nativeEditingAndLimits()
{
    auto first = editor(*m_window, "first");
    auto &key = *m_controller;
    QSignalSpy action(&key, &KeyboardController::enterActionRequested);
    QCOMPARE(key.enterLabel(), u"Search"_s);
    key.enter();
    QCOMPARE(action.count(), 1);
    QCOMPARE(action.front().front().value<Qt::EnterKeyType>(), Qt::EnterKeySearch);

    key.input(u"world"_s);
    QCOMPARE(first->property("text").toString(), u"world"_s);
    key.selectAll();
    key.input(u"hello"_s);
    QCOMPARE(first->property("text").toString(), u"hello"_s);
    key.input(u"👩‍👩‍👧‍👦"_s);
    key.backspace();
    QCOMPARE(first->property("text").toString(), u"hello"_s);
    key.input(u"e\u0301"_s);
    key.backspace();
    QCOMPARE(first->property("text").toString(), u"hello"_s);

    key.selectAll();
    key.input(u"helo"_s);
    QTRY_VERIFY(!findCandidate(key, u"hello"_s).isEmpty());
    key.selectCandidate(findCandidate(key, u"hello"_s));
    QCOMPARE(first->property("text").toString(), u"hello"_s);
    QVERIFY(key.canUndoCandidate());
    key.undoCandidate();
    QCOMPARE(first->property("text").toString(), u"helo"_s);

    first->setProperty("maximumLength", 5);
    key.input(u"test"_s);
    QCOMPARE(first->property("text").toString().size(), 5);
    first->setProperty("readOnly", true);
    QVERIFY(!key.active());
    key.backspace();
    QCOMPARE(first->property("text").toString().size(), 5);
}

void EngineIntegration::cancelOnFocusOrHide()
{
    auto first = editor(*m_window, "first");
    auto second = editor(*m_window, "second");
    auto &key = *m_controller;
    key.setLanguage(u"zh-CN"_s);
    key.input(u"nihao"_s);
    QCOMPARE(key.preedit(), u"nihao"_s);
    QCOMPARE(first->property("text").toString(), QString());
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    const auto previous = findCandidate(key, u"你好"_s);
    second->forceActiveFocus();
    QCOMPARE(key.preedit(), QString());
    QCOMPARE(first->property("text").toString(), QString());
    key.selectCandidate(previous);
    QCOMPARE(second->property("text").toString(), QString());

    key.input(u"ni"_s);
    key.setEnabled(false);
    QCOMPARE(key.preedit(), QString());
    QCOMPARE(second->property("preeditText").toString(), QString());
    key.input(u"hao"_s);
    QCOMPARE(second->property("text").toString(), QString());
    key.setEnabled(true);
    QVERIFY(key.active());

    second->setProperty("text", u"selection"_s);
    key.selectAll();
    key.input(u"nihao"_s);
    key.cancelComposition();
    QCOMPARE(second->property("text").toString(), u"selection"_s);
    QCOMPARE(second->property("selectedText").toString(), u"selection"_s);

    first->forceActiveFocus();
    key.setLanguage(u"en-US"_s);
    key.input(u"helo"_s);
    QTRY_VERIFY(!findCandidate(key, u"hello"_s).isEmpty());
    key.selectCandidate(findCandidate(key, u"hello"_s));
    QVERIFY(key.canUndoCandidate());
    const auto changeFocus = connect(&key, &KeyboardController::undoCandidateChanged, this, [&] {
        if (!key.canUndoCandidate())
            second->forceActiveFocus();
    });
    key.undoCandidate();
    disconnect(changeFocus);
    QCOMPARE(first->property("text").toString(), u"hello"_s);
    QCOMPARE(second->property("text").toString(), u"selection"_s);
}

void EngineIntegration::pinyinModes()
{
    auto first = editor(*m_window, "first");
    auto &key = *m_controller;
    key.setLanguage(u"zh-CN"_s);
    key.input(u"nihao"_s);
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    key.selectCandidate(findCandidate(key, u"你好"_s));
    QCOMPARE(first->property("text").toString(), u"你好"_s);
    QVERIFY(key.canUndoCandidate());
    key.undoCandidate();
    QCOMPARE(key.preedit(), u"nihao"_s);
    QCOMPARE(first->property("text").toString(), QString());
    key.cancelComposition();

    key.input(u"xi'an"_s);
    QTRY_VERIFY(!findCandidate(key, u"西安"_s).isEmpty());
    QVERIFY(findCandidate(key, u"先"_s).isEmpty());
    key.cancelComposition();

    key.setPinyinMode(u"t9"_s);
    key.input(u"64426"_s);
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    key.cancelComposition();
    key.inputDirect(u"5"_s);
    QCOMPARE(first->property("text").toString(), u"5"_s);
    QVERIFY(key.preedit().isEmpty());

    key.setPinyinMode(u"double"_s);
    key.input(u"nihc"_s);
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    key.cancelComposition();

    key.setPinyinMode(u"full"_s);
    key.setLanguage(u"zh-TW"_s);
    key.input(u"zhongguo"_s);
    QTRY_VERIFY(!findCandidate(key, u"中國"_s).isEmpty());
    key.cancelComposition();

    key.setFuzzyPinyin(true);
    key.input(u"zong"_s);
    QTRY_VERIFY(!findCandidate(key, u"中"_s).isEmpty());
}

void EngineIntegration::privacyAndClipboard()
{
    auto password = editor(*m_window, "password");
    auto &key = *m_controller;
    key.setLanguage(u"zh-CN"_s);
    password->forceActiveFocus();
    QVERIFY(key.sensitive());
    QCOMPARE(key.inputMode(), u"number"_s);
    key.input(u"nihao89"_s);
    QCOMPARE(password->property("text").toString(), u"89"_s);
    QVERIFY(key.candidates().isEmpty());
    QVERIFY(key.preedit().isEmpty());
    key.selectAll();
    QVERIFY(password->property("selectedText").toString().isEmpty());

    QSignalSpy ops(&key, &KeyboardController::userDataOperationFinished);
    key.addUserPhrase(u"test"_s, u"测试"_s);
    QCOMPARE(ops.size(), 1);
    QCOMPARE(ops.first().at(1).toBool(), false);

    auto first = editor(*m_window, "first");
    first->forceActiveFocus();
    key.setPrivateMode(true);
    key.input(u"test"_s);
    QCOMPARE(first->property("text").toString(), u"test"_s);
    QVERIFY(key.candidates().isEmpty());
    key.setPrivateMode(false);
}

void EngineIntegration::profileAndSupplementalPacks()
{
    auto &key = *m_controller;
    QSignalSpy ops(&key, &KeyboardController::userDataOperationFinished);
    const auto profile = key.profileId();
    key.setLanguage(u"zh-CN"_s);
    key.refreshUserPhrases();
    key.addUserPhrase(u"ni hao"_s, u"我的自定义词"_s);
    QTRY_COMPARE(ops.size(), 1);
    QVERIFY(ops.first().at(1).toBool());
    QTRY_COMPARE(key.userPhrases().size(), 1);
    QCOMPARE(key.userPhrases().first().toMap().value(u"text"_s).toString(), u"我的自定义词"_s);

    QVariantMap sample{{u"code"_s, u"ni hao"_s}, {u"text"_s, u"包内词"_s},
                       {u"locale"_s, u"zh-CN"_s}, {u"weight"_s, 100},
                       {u"expiresAt"_s, QDateTime::currentDateTimeUtc().addDays(1)}};
    key.setSupplementalEntries({sample});
    key.input(u"nihao"_s);
    QTRY_VERIFY(!findCandidate(key, u"包内词"_s).isEmpty());
    QVERIFY(!findCandidate(key, u"我的自定义词"_s).isEmpty());
    key.selectCandidate(findCandidate(key, u"包内词"_s));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto exported = QUrl::fromLocalFile(directory.filePath(u"profile.json"_s));
    key.exportUserData(exported);
    QTRY_COMPARE(ops.size(), 2);
    QVERIFY(ops.last().at(1).toBool());
    QFile persisted(exported.toLocalFile());
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    QVERIFY(persisted.readAll().contains(QString(u"包内词").toUtf8()));

    key.setProfileId(profile + u"2");
    QVERIFY(key.userPhrases().isEmpty());
    key.setSupplementalEntries({});
    key.input(u"nihao"_s);
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    QVERIFY(findCandidate(key, u"我的自定义词"_s).isEmpty());

    key.importUserData(exported);
    QTRY_COMPARE(ops.size(), 3);
    QVERIFY(ops.last().at(1).toBool());
    QTRY_VERIFY(!findCandidate(key, u"我的自定义词"_s).isEmpty());
    QTRY_COMPARE(key.userPhrases().size(), 1);
    key.clearLearning();
    QTRY_COMPARE(ops.size(), 4);
    QVERIFY(ops.last().at(1).toBool());
    QTRY_VERIFY(!findCandidate(key, u"我的自定义词"_s).isEmpty());
    QVERIFY(findCandidate(key, u"包内词"_s).isEmpty());
    key.removeUserPhrase(u"ni hao"_s, u"我的自定义词"_s);
    QTRY_COMPARE(ops.size(), 5);
    QVERIFY(ops.last().at(1).toBool());
    QTRY_VERIFY(key.userPhrases().isEmpty());

    key.cancelComposition();
    sample[u"expiresAt"_s] = QDateTime::currentDateTimeUtc().addDays(-1);
    key.setSupplementalEntries({sample});
    key.input(u"nihao"_s);
    QTRY_VERIFY(!findCandidate(key, u"你好"_s).isEmpty());
    QVERIFY(findCandidate(key, u"包内词"_s).isEmpty());
}

void EngineIntegration::swipeAndRecognitionDelivery()
{
    auto first = editor(*m_window, "first");
    auto second = editor(*m_window, "second");
    auto &key = *m_controller;
    key.inputSwipe(u"hedlo"_s);
    QTRY_VERIFY(!findCandidate(key, u"hello"_s).isEmpty());
    QCOMPARE(key.preedit(), u"hello"_s);
    QCOMPARE(first->property("text").toString(), QString());
    key.space();
    QCOMPARE(first->property("text").toString(), u"hello "_s);
    QVERIFY(key.canUndoCandidate());
    key.undoCandidate();
    QCOMPARE(first->property("text").toString(), QString());

    const auto previous = key.sessionToken();
    second->forceActiveFocus();
    QVERIFY(!key.inputLiteral(u"old"_s, previous));
    QVERIFY(key.inputLiteral(u"new"_s, key.sessionToken()));
    QCOMPARE(second->property("text").toString(), u"new"_s);
    key.setPrivateMode(true);
    QVERIFY(!key.inputLiteral(u"private"_s, key.sessionToken()));
    QCOMPARE(second->property("text").toString(), u"new"_s);
    key.setPrivateMode(false);
    key.setLanguage(u"en-GB"_s);
    key.selectAll();
    key.input(u"color"_s);
    QTRY_VERIFY(!findCandidate(key, u"colour"_s).isEmpty());
    key.selectCandidate(findCandidate(key, u"colour"_s));
    QCOMPARE(second->property("text").toString(), u"colour"_s);

    key.selectAll();
    key.input(u"velorakeyword"_s);
    key.space();
    key.input(u"velora"_s);
    QTRY_VERIFY(!findCandidate(key, u"velorakeyword"_s).isEmpty());
}

void EngineIntegration::clipboardHistoryAndRecentEmoji()
{
    auto first = editor(*m_window, "first");
    auto &key = *m_controller;
    const auto profile = key.profileId();
    const QString emoji = u"🙌"_s;
    QVERIFY(!key.clipboardHistoryEnabled());
    key.input(u"alpha"_s);
    key.selectAll();
    key.copy();
    QVERIFY(key.clipboardHistory().isEmpty());
    key.setClipboardHistoryEnabled(true);
    key.copy();
    QCOMPARE(key.clipboardHistory(), QStringList{u"alpha"_s});
    key.backspace();
    key.pasteHistory(0);
    QCOMPARE(first->property("text").toString(), u"alpha"_s);
    key.clearClipboardHistory();
    QVERIFY(key.clipboardHistory().isEmpty());

    key.input(emoji);
    key.recordEmoji(emoji);
    QTRY_COMPARE(key.recentEmoji(), QStringList{emoji});
    key.setProfileId(profile + u"other");
    QVERIFY(key.recentEmoji().isEmpty());
    key.setProfileId(profile);
    QTRY_COMPARE(key.recentEmoji(), QStringList{emoji});
    key.setPrivateMode(true);
    QVERIFY(key.recentEmoji().isEmpty());
}

QTEST_MAIN(EngineIntegration)
#include "engine_integration.moc"
