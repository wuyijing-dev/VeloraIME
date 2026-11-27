#include "handwritingrecognizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJSEngine>
#include <QThreadPool>
#include <QTimer>

using Velora::HandwritingRecognizer;

namespace {

bool waitForRecognition(HandwritingRecognizer &recognizer)
{
    if (!recognizer.busy())
        return true;

    QEventLoop loop;
    QObject::connect(&recognizer, &HandwritingRecognizer::busyChanged, &loop, [&] {
        if (!recognizer.busy())
            loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return !recognizer.busy();
}

int failure(const QString &message)
{
    qCritical().noquote() << message;
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    HandwritingRecognizer recognizer;
    if (!recognizer.available())
        return failure(QStringLiteral("Simplified Chinese Zinnia model unavailable"));

    QJSEngine engine;
    QJSEngine::setObjectOwnership(&recognizer, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("recognizer"), engine.newQObject(&recognizer));
    const auto init = engine.evaluate(QStringLiteral(R"(
        const horizontal = [{x:30,y:150},{x:100,y:150},{x:220,y:150},{x:270,y:150}]
        const vertical = [{x:140,y:25},{x:141,y:80},{x:142,y:150},{x:142,y:255}]
        function cross() { recognizer.request([horizontal, vertical], 300, 300) }
        function line() { recognizer.request([horizontal], 300, 300) }
    )"));
    if (init.isError())
        return failure(init.toString());

    QElapsedTimer time;
    time.start();
    if (engine.evaluate(QStringLiteral("cross()")).isError() || !waitForRecognition(recognizer)
        || recognizer.candidates().value(0) != QStringLiteral("十")) {
        return failure(QStringLiteral("Cold handwriting request did not recognize 十 first"));
    }
    qInfo() << "Cold 十" << time.elapsed() << "ms" << recognizer.candidates();

    time.restart();
    if (engine.evaluate(QStringLiteral("cross(); line()")).isError() || !waitForRecognition(recognizer)
        || recognizer.candidates().value(0) != QStringLiteral("一")) {
        return failure(QStringLiteral("New handwriting request did not supersede the first"));
    }
    qInfo() << "Queued 一" << time.elapsed() << "ms" << recognizer.candidates();

    engine.evaluate(QStringLiteral("cross(); recognizer.cancel()"));
    if (!QThreadPool::globalInstance()->waitForDone(10000))
        return failure(QStringLiteral("Cancelled worker did not finish"));
    QCoreApplication::processEvents();
    if (recognizer.busy() || !recognizer.candidates().isEmpty())
        return failure(QStringLiteral("A cancelled request published handwriting candidates"));

    qputenv("VELORA_HANDWRITING_MODEL", QByteArrayLiteral("/nonexistent/velora-handwriting.model"));
    HandwritingRecognizer unavailable;
    if (unavailable.available())
        return failure(QStringLiteral("Handwriting was available without a model"));
    qInfo() << "Cancellation and missing model checks passed";
    return 0;
}
