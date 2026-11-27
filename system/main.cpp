#include "waylandkeyboard.h"
#include "platformfonts.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("QT_IM_MODULE");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("VeloraIME"));
    QCoreApplication::setApplicationName(QStringLiteral("VeloraIME Weston"));
    configureVeloraFonts();
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));

    WaylandKeyboard keyboard;
    if (!keyboard.initialize()) {
        qCritical("Weston input method could not bind its panel, seat and output");
        return 1;
    }
    QQmlEngine engine;
    const QDir binary(QCoreApplication::applicationDirPath());
    const QString installDir(QStringLiteral(VELORA_QML_INSTALL_DIR));
    const QString importDir = QDir::isAbsolutePath(installDir) ? installDir
        : binary.absoluteFilePath(QStringLiteral("../") + installDir);
    engine.addImportPath(QStringLiteral(VELORA_BUILD_IMPORT_DIR));
    engine.addImportPath(importDir);
    QQmlComponent component(&engine, QStringLiteral("Rk.TouchKeyboard"),
                            QStringLiteral("KeyboardPanel"), QQmlComponent::PreferSynchronous);
    std::unique_ptr<QObject> scene(component.createWithInitialProperties({
        {QStringLiteral("graphicsMode"), 3}
    }));
    auto *panel = qobject_cast<QQuickItem *>(scene.get());
    if (!panel) {
        qCritical().noquote() << component.errorString();
        return 1;
    }
    auto *controller = panel->property("controller").value<KeyboardController *>();
    if (!controller)
        return 1;
    controller->setInputTarget(&keyboard);
    QObject::connect(panel, SIGNAL(closeRequested()), &keyboard, SLOT(hidePanel()));
    keyboard.attach(panel);
    return app.exec();
}
