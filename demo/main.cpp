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
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VeloraIME"));
    QCoreApplication::setApplicationName(QStringLiteral("VeloraIME Demo"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    configureVeloraFonts();

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
                                       QStringLiteral("Exit after loading the demo."));
    const QCommandLineOption inputTest(QStringLiteral("verify-input"),
                                       QStringLiteral("Exercise the visible keyboard and exit."));
    const QCommandLineOption snapshot(QStringLiteral("snapshot"), QStringLiteral(
                                    "Save a screenshot after input verification."), QStringLiteral("path"));
    parser.addOption(smokeTest);
    parser.addOption(inputTest);
    parser.addOption(snapshot);
    parser.process(app);

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
    if (parser.isSet(inputTest)) {
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
