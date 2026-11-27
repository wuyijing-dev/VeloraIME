#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {
constexpr auto phraseCode = u"du li ci tiao";
constexpr auto phraseText = u"独立应用词条";
constexpr auto recentEmoji = u"🌟";

QQuickItem *visibleKey(QQuickItem *parent, const QString &name)
{
    if (parent->isVisible() && parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *key = visibleKey(child, name))
            return key;
    }
    return nullptr;
}

QString candidateId(QObject *controller, const QString &text)
{
    for (const auto &entry : controller->property("candidates").toList()) {
        const auto candidate = entry.toMap();
        if (candidate.value(u"text"_s).toString() == text)
            return candidate.value(u"id"_s).toString();
    }
    return {};
}
}

class InstalledQuickHostTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_importRoot = QFileInfo(qEnvironmentVariable("VELORA_EXAMPLE_IMPORT_ROOT")).canonicalFilePath();
        QVERIFY2(!m_importRoot.isEmpty(), "Set VELORA_EXAMPLE_IMPORT_ROOT to an installed QML import root.");
        const QDir installedModule(QDir(m_importRoot).filePath(u"Rk/TouchKeyboard"_s));
        QVERIFY2(installedModule.exists(u"qmldir"_s), qPrintable(installedModule.path()));
        m_pluginPath = QFileInfo(installedModule.filePath(u"libVeloraIMEPlugin.so"_s)).canonicalFilePath();
        m_modulePath = QFileInfo(installedModule.filePath(u"libVeloraIME.so"_s)).canonicalFilePath();
        QVERIFY(!m_pluginPath.isEmpty());
        QVERIFY(!m_modulePath.isEmpty());
        m_touchDevice = QTest::createTouchDevice();
    }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setImportPathList({m_importRoot, QLibraryInfo::path(QLibraryInfo::QmlImportsPath),
                                     u"qrc:/qt/qml"_s, u"qrc:/qt-project.org/imports"_s});
        m_engine->load(QUrl(u"qrc:/example/Main.qml"_s));
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        QTRY_VERIFY(m_window->isExposed());
        m_keyboard = m_window->findChild<QQuickItem *>(u"hostKeyboard"_s);
        m_note = m_window->findChild<QQuickItem *>(u"hostNote"_s);
        QVERIFY(m_keyboard);
        QVERIFY(m_note);
        m_controller = m_keyboard->property("controller").value<QObject *>();
        QVERIFY(m_controller);
        m_note->forceActiveFocus();
        QTRY_VERIFY(m_controller->property("active").toBool());

        QFile processMappings(u"/proc/self/maps"_s);
        QVERIFY(processMappings.open(QIODevice::ReadOnly));
        const auto loaded = processMappings.readAll();
        QVERIFY2(loaded.contains(QFile::encodeName(m_pluginPath)), qPrintable(m_pluginPath));
        QVERIFY2(loaded.contains(QFile::encodeName(m_modulePath)), qPrintable(m_modulePath));
    }

    void cleanup()
    {
        m_engine.reset();
    }

    void touchFocusAndFieldHints()
    {
        QTRY_VERIFY(visibleKey(m_keyboard, u"key-text-q"_s));
        QVERIFY(tapKey(u"key-text-q"_s));
        QVERIFY(tapKey(u"key-text-w"_s));
        QTRY_COMPARE(m_note->property("text").toString(), u"qw"_s);
        QVERIFY(m_note->hasActiveFocus());
        QVERIFY(tapKey(u"key-backspace"_s));
        QTRY_COMPARE(m_note->property("text").toString(), u"q"_s);

        auto *email = m_window->findChild<QQuickItem *>(u"hostEmail"_s);
        auto *url = m_window->findChild<QQuickItem *>(u"hostUrl"_s);
        auto *amount = m_window->findChild<QQuickItem *>(u"hostAmount"_s);
        auto *pin = m_window->findChild<QQuickItem *>(u"hostPin"_s);
        QVERIFY(email && url && amount && pin);

        email->forceActiveFocus();
        QTRY_COMPARE(m_controller->property("inputMode").toString(), u"email"_s);
        QTRY_VERIFY(visibleKey(m_keyboard, u"key-text-@"_s));
        QVERIFY(tapKey(u"key-text-@"_s));
        QTRY_COMPARE(email->property("text").toString(), u"@"_s);
        QVERIFY(email->hasActiveFocus());
        QCOMPARE(m_controller->property("enterLabel").toString(), u"Next"_s);
        QVERIFY(tapKey(u"key-enter"_s));
        QTRY_VERIFY(url->hasActiveFocus());
        QTRY_COMPARE(m_controller->property("inputMode").toString(), u"url"_s);
        QTRY_VERIFY(visibleKey(m_keyboard, u"key-text-/"_s));
        QVERIFY(tapKey(u"key-text-/"_s));
        QTRY_COMPARE(url->property("text").toString(), u"/"_s);

        amount->forceActiveFocus();
        QTRY_COMPARE(m_controller->property("inputMode").toString(), u"decimal"_s);
        QTRY_VERIFY(visibleKey(m_keyboard, u"key-text-1"_s));
        QVERIFY(tapKey(u"key-text-1"_s));
        QVERIFY(tapKey(u"key-text-."_s));
        QVERIFY(tapKey(u"key-text-2"_s));
        QTRY_COMPARE(amount->property("text").toString(), u"1.2"_s);
        QVERIFY(amount->hasActiveFocus());

        pin->forceActiveFocus();
        QTRY_COMPARE(m_controller->property("inputMode").toString(), u"number"_s);
        QTRY_VERIFY(m_controller->property("sensitive").toBool());
        QCOMPARE(m_controller->property("enterLabel").toString(), u"Done"_s);
        QTRY_VERIFY(visibleKey(m_keyboard, u"key-text-4"_s));
        QVERIFY(!visibleKey(m_keyboard, u"keyboardCandidates"_s));
        QVERIFY(!visibleKey(m_keyboard, u"keyboardClipboard"_s));
        QVERIFY(tapKey(u"key-text-4"_s));
        QVERIFY(QMetaObject::invokeMethod(m_controller, "inputDirect", Q_ARG(QString, u"a"_s)));
        QTRY_COMPARE(pin->property("text").toString(), u"4"_s);
        QTRY_VERIFY(pin->property("displayText").toString() != u"4"_s);
        QVERIFY(pin->hasActiveFocus());
        QCOMPARE(email->property("text").toString(), u"@"_s);
        QCOMPARE(m_note->property("text").toString(), u"q"_s);

        m_note->forceActiveFocus();
        QTRY_VERIFY(!m_controller->property("sensitive").toBool());
        QTRY_VERIFY(visibleKey(m_keyboard, u"hideKeyboard"_s));
        const auto *viewport = m_window->findChild<QQuickItem *>(u"hostViewport"_s);
        QVERIFY(viewport);
        QTRY_COMPARE(viewport->height(), m_window->height() - m_keyboard->height());
        QVERIFY(tapKey(u"hideKeyboard"_s));
        QTRY_VERIFY(!m_keyboard->isVisible());
        QVERIFY(m_note->hasActiveFocus());
        QTRY_COMPARE(viewport->height(), m_window->height());
        tap(m_note);
        QTRY_VERIFY(m_keyboard->isVisible());
        QVERIFY(m_note->hasActiveFocus());
    }

    void writeUpgradeProfile()
    {
        QVERIFY(m_controller->setProperty("language", u"zh-CN"_s));
        QVERIFY(QMetaObject::invokeMethod(m_controller, "addUserPhrase",
                                         Q_ARG(QString, QString(phraseCode)), Q_ARG(QString, QString(phraseText))));
        QVERIFY(QMetaObject::invokeMethod(m_controller, "input", Q_ARG(QString, u"nihao"_s)));
        QTRY_VERIFY(!candidateId(m_controller, u"你好"_s).isEmpty());
        const auto id = candidateId(m_controller, u"你好"_s);
        QVERIFY(QMetaObject::invokeMethod(m_controller, "selectCandidate", Q_ARG(QString, id)));
        QTRY_COMPARE(m_note->property("text").toString(), u"你好"_s);
        QTRY_VERIFY(m_controller->property("canUndoCandidate").toBool());
        QVERIFY(QMetaObject::invokeMethod(m_controller, "inputDirect", Q_ARG(QString, QString(recentEmoji))));
        QTRY_COMPARE(m_note->property("text").toString(), u"你好🌟"_s);
        QVERIFY(QMetaObject::invokeMethod(m_controller, "recordEmoji", Q_ARG(QString, QString(recentEmoji))));
        verifyProfileContent();
    }

    void readUpgradeProfile()
    {
        QVERIFY(m_controller->setProperty("language", u"zh-CN"_s));
        verifyProfileContent();
        QVERIFY(QMetaObject::invokeMethod(m_controller, "input", Q_ARG(QString, u"dulicitiao"_s)));
        QTRY_VERIFY(!candidateId(m_controller, QString(phraseText)).isEmpty());
    }

private:
    bool tapKey(const QString &name)
    {
        QSignalSpy rendered(m_window, &QQuickWindow::afterRendering);
        m_window->requestUpdate();
        if (rendered.isEmpty() && !rendered.wait(1000))
            return false;
        const auto *key = visibleKey(m_keyboard, name);
        if (!key || !key->isEnabled())
            return false;
        tap(key);
        return true;
    }

    void tap(const QQuickItem *item)
    {
        const QPoint point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::touchEvent(m_window, m_touchDevice).press(0, point, m_window);
        QTest::touchEvent(m_window, m_touchDevice).release(0, point, m_window);
    }

    void verifyProfileContent()
    {
        QVERIFY(QMetaObject::invokeMethod(m_controller, "refreshUserPhrases"));
        QTRY_VERIFY(m_controller->property("recentEmoji").toStringList().contains(QString(recentEmoji)));
        QTRY_VERIFY(hasCustomPhrase());

        QTemporaryDir output;
        QVERIFY(output.isValid());
        const QString exportFile = QDir(output.path()).filePath(u"profile.json"_s);
        QSignalSpy exported(m_controller, SIGNAL(userDataOperationFinished(QString,bool)));
        QVERIFY(exported.isValid());
        QVERIFY(QMetaObject::invokeMethod(m_controller, "exportUserData", Q_ARG(QUrl, QUrl::fromLocalFile(exportFile))));
        QTRY_VERIFY(!exported.isEmpty());
        QVERIFY(exported.constFirst().at(1).toBool());
        QFile snapshot(exportFile);
        QVERIFY(snapshot.open(QIODevice::ReadOnly));
        const auto entries = QJsonDocument::fromJson(snapshot.readAll()).object().value(u"entries"_s).toArray();
        QVERIFY(std::any_of(entries.begin(), entries.end(), [](const QJsonValue &entry) {
            const auto word = entry.toObject();
            return word.value(u"text"_s).toString() == u"你好"_s && word.value(u"hits"_s).toInt() > 0;
        }));
    }

    bool hasCustomPhrase() const
    {
        const auto phrases = m_controller->property("userPhrases").toList();
        return std::any_of(phrases.cbegin(), phrases.cend(), [](const QVariant &phrase) {
            return phrase.toMap().value(u"text"_s).toString() == QString(phraseText);
        });
    }

    QString m_importRoot;
    QString m_pluginPath;
    QString m_modulePath;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QPointingDevice *m_touchDevice = nullptr;
    QQuickWindow *m_window = nullptr;
    QQuickItem *m_keyboard = nullptr;
    QQuickItem *m_note = nullptr;
    QObject *m_controller = nullptr;
};

int main(int argc, char **argv)
{
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return 1;
    const auto dataPath = qEnvironmentVariable("VELORA_EXAMPLE_PROFILE_HOME",
                                                QDir(scratch.path()).filePath(u"data"_s));
    if (QDir::isRelativePath(dataPath) || !QDir().mkpath(dataPath))
        return 1;
    qputenv("XDG_DATA_HOME", QFile::encodeName(dataPath));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(QDir(scratch.path()).filePath(u"config"_s)));
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"VeloraIME Examples"_s);
    QCoreApplication::setApplicationName(u"Quick Host Integration Test"_s);
    QQuickStyle::setStyle(u"Basic"_s);
    InstalledQuickHostTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "integration_test.moc"
