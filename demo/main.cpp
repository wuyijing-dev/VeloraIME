#include "../src/platformfonts.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>

namespace {
QQuickItem *findVisibleItem(QQuickItem *parent, const QString &name)
{
    if (parent->isVisible() && parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *match = findVisibleItem(child, name))
            return match;
    }
    return nullptr;
}

bool clickKey(QQuickWindow *window, QQuickItem *panel, const QString &key)
{
    const auto *item = findVisibleItem(panel, key);
    if (!item || !item->isEnabled())
        return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const auto global = window->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress, point, point, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return true;
}

void verifyInput(QQuickWindow *window, const QString &snapshot)
{
    auto *editor = window->findChild<QQuickItem *>(QStringLiteral("velora-primary-editor"));
    auto *panel = window->findChild<QQuickItem *>(QStringLiteral("velora-keyboard-panel"));
    if (!editor || !panel) {
        qCritical("Demo editor or keyboard could not be found");
        QCoreApplication::exit(1);
        return;
    }
    auto *controller = panel->property("controller").value<QObject *>();
    if (!controller) {
        QCoreApplication::exit(1);
        return;
    }
    editor->forceActiveFocus();
    controller->setProperty("language", QStringLiteral("en-US"));
    QTimer::singleShot(300, window, [window, editor, panel, snapshot] {
        const bool tapped = clickKey(window, panel, QStringLiteral("key-text-q"))
            && clickKey(window, panel, QStringLiteral("key-text-w"));
        const bool inserted = editor->property("text").toString() == QStringLiteral("qw");
        const bool deleted = clickKey(window, panel, QStringLiteral("key-backspace"))
            && editor->property("text").toString() == QStringLiteral("q");
        const bool focused = editor->hasActiveFocus();
        editor->setProperty("text", QString{});
        const bool captured = snapshot.isEmpty() || window->grabWindow().save(snapshot);
        qInfo("VeloraIME input verification: tap=%d insertion=%d deletion=%d focus=%d snapshot=%d",
              tapped, inserted, deleted, focused, captured);
        QCoreApplication::exit(tapped && inserted && deleted && focused && captured ? 0 : 1);
    });
}

void captureReferenceState(QQuickWindow *window, const QString &state, const QString &snapshot)
{
    const bool secure = state == QStringLiteral("secure-numeric");
    const bool chinese = state == QStringLiteral("light-pinyin");
    window->setProperty("dark", state == QStringLiteral("dark-english"));
    window->setProperty("solid", false);
    window->setProperty("highContrast", false);
    window->setProperty("page", secure ? QStringLiteral("pin") : QStringLiteral("editor"));
    window->setProperty("keyboardOpen", true);

    const auto editorName = secure ? QStringLiteral("velora-pin-editor") : QStringLiteral("velora-primary-editor");
    auto *editor = findVisibleItem(window->contentItem(), editorName);
    auto *panel = findVisibleItem(window->contentItem(), QStringLiteral("velora-keyboard-panel"));
    auto *controller = panel ? panel->property("controller").value<QObject *>() : nullptr;
    if (!editor || !controller) {
        qCritical("Could not open the requested reference state");
        QCoreApplication::exit(1);
        return;
    }

    editor->forceActiveFocus();
    if (!secure) {
        controller->setProperty("language", chinese ? QStringLiteral("zh-CN") : QStringLiteral("en-US"));
        controller->setProperty("pinyinMode", QStringLiteral("full"));
        editor->setProperty("text", chinese ? QStringLiteral("一起把想法写下来。\n")
                                            : QStringLiteral("A little room for ideas.\n"));
        editor->setProperty("cursorPosition", editor->property("text").toString().size());
    }
    const auto input = secure ? QStringLiteral("4728")
                              : (chinese ? QStringLiteral("nihao") : QStringLiteral("hel"));
    for (const auto character : input)
        QMetaObject::invokeMethod(controller, "input", Q_ARG(QString, QString(character)));

    auto *poll = new QTimer(window);
    poll->setInterval(100);
    QObject::connect(poll, &QTimer::timeout, window,
                     [window, editor, controller, snapshot, secure, chinese, attempts = 0]() mutable {
        const auto candidates = controller->property("candidates").toList();
        const bool sensitive = controller->property("sensitive").toBool();
        const bool composing = !chinese || controller->property("preedit").toString() == QStringLiteral("nihao");
        const bool ready = secure ? (sensitive && candidates.isEmpty() && editor->property("length").toInt() == 4)
                                  : (!sensitive && composing && !candidates.isEmpty());
        if (!ready && ++attempts < 40)
            return;
        const auto frame = window->grabWindow();
        const bool captured = frame.save(snapshot);
        qInfo("VeloraIME reference state: ready=%d, candidates=%lld, sensitive=%d, capture=%dx%d",
              ready, static_cast<long long>(candidates.size()), sensitive, frame.width(), frame.height());
        QCoreApplication::exit(ready && captured ? 0 : 1);
    });
    poll->start();
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VeloraIME"));
    QCoreApplication::setApplicationName(QStringLiteral("VeloraIME Demo"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/demo/fonts/Nunito.ttf"));
    configureVeloraFonts();

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
                                       QStringLiteral("Exit after loading the demo."));
    const QCommandLineOption inputTest(QStringLiteral("verify-input"),
                                       QStringLiteral("Exercise the visible keyboard and exit."));
    const QCommandLineOption snapshot(QStringLiteral("snapshot"), QStringLiteral(
                                    "Save a screenshot after input verification."), QStringLiteral("path"));
    const QCommandLineOption kiosk(QStringLiteral("kiosk"),
                                    QStringLiteral("Run fullscreen on a dedicated touch display."));
    const QCommandLineOption previewState(QStringLiteral("preview-state"),
        QStringLiteral("Capture a live reference state: light-pinyin, dark-english, or secure-numeric."),
        QStringLiteral("state"));
    parser.addOption(smokeTest);
    parser.addOption(inputTest);
    parser.addOption(snapshot);
    parser.addOption(kiosk);
    parser.addOption(previewState);
    parser.process(app);
    const QString state = parser.value(previewState);
    if (parser.isSet(previewState)
        && (!parser.isSet(snapshot) || parser.isSet(inputTest) || parser.isSet(smokeTest)
            || (state != QStringLiteral("light-pinyin") && state != QStringLiteral("dark-english")
                && state != QStringLiteral("secure-numeric")))) {
        qCritical("Reference state needs an allowed state and a snapshot path");
        return 2;
    }
    if (parser.isSet(previewState))
        QCoreApplication::setApplicationName(QStringLiteral("VeloraIME Reference Preview"));

    const QDir executableDirectory(QCoreApplication::applicationDirPath());
    const QString installedImportPath = executableDirectory.absoluteFilePath(
        QStringLiteral("../") + QStringLiteral(VELORA_QML_INSTALL_DIR));

    QQmlApplicationEngine engine;
    engine.addImportPath(executableDirectory.filePath(QStringLiteral("qml")));
    engine.addImportPath(QDir::isAbsolutePath(QStringLiteral(VELORA_QML_INSTALL_DIR))
                             ? QStringLiteral(VELORA_QML_INSTALL_DIR) : installedImportPath);
    QFile packPublicKey(QStringLiteral(":/demo/cloud-production-1.pub.pem"));
    if (!packPublicKey.open(QIODevice::ReadOnly))
        return 1;
    const QVariantMap trustedKeys{{QStringLiteral("production-1"), packPublicKey.readAll()}};
    engine.setInitialProperties({{QStringLiteral("trustedPackKeys"), trustedKeys}});
    engine.load(QUrl(QStringLiteral("qrc:/demo/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (parser.isSet(kiosk)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (!window)
            return 1;
        window->setCursor(Qt::BlankCursor);
        window->showFullScreen();
        QTimer::singleShot(800, window, [window] {
            const auto scale = window->devicePixelRatio();
            qInfo("VeloraIME display: %dx%d logical, %dx%d physical, scale %.4f",
                  window->width(), window->height(), qRound(window->width() * scale),
                  qRound(window->height() * scale), double(scale));
        });
    }
    if (parser.isSet(previewState)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (!window)
            return 1;
        QTimer::singleShot(850, window, [window, state, path = parser.value(snapshot)] {
            captureReferenceState(window, state, path);
        });
    } else if (parser.isSet(inputTest)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (!window)
            return 1;
        QTimer::singleShot(800, window, [window, path = parser.value(snapshot)] {
            verifyInput(window, path);
        });
    } else if (parser.isSet(smokeTest)) {
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    } else if (parser.isSet(snapshot)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (!window)
            return 1;
        QTimer::singleShot(1200, window, [window, path = parser.value(snapshot)] {
            if (!window->grabWindow().save(path))
                qWarning("Could not save the live keyboard screenshot");
        });
    }
    return app.exec();
}
