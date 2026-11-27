#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VeloraIME Examples"));
    QCoreApplication::setApplicationName(QStringLiteral("Quick Host"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/example/Main.qml")));
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
