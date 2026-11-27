#include "voicerecognizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

namespace {

bool waitForDecoder(VoiceRecognizer &voice)
{
    if (!voice.busy())
        return true;
    QEventLoop loop;
    QObject::connect(&voice, &VoiceRecognizer::stateChanged, &loop, [&] {
        if (!voice.busy())
            loop.quit();
    });
    QTimer::singleShot(24000, &loop, &QEventLoop::quit);
    loop.exec();
    return !voice.busy();
}

int fail(const char *message)
{
    qCritical("%s", message);
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 3 || arguments.at(2).trimmed().isEmpty())
        return fail("Usage: velora-voice-fixture-probe <local PCM WAV> <expected text fragment>");

    const auto wav = QUrl::fromLocalFile(QFileInfo(arguments.at(1)).absoluteFilePath());
    const auto expected = arguments.at(2).trimmed();
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return fail("Could not create the isolated fixture directory");
    const QDir scratchDirectory(scratch.path());
    qputenv("TMPDIR", scratch.path().toLocal8Bit());

    VoiceRecognizer voice;
    if (!voice.decoderAvailable())
        return fail("SenseVoice decoder, model, or tokens are unavailable");

    QString transcript;
    QString token;
    int recognized = 0;
    int failures = 0;
    bool everListening = false;
    QObject::connect(&voice, &VoiceRecognizer::recognized, &app,
                     [&](const QString &text, const QString &session) {
        transcript = text;
        token = session;
        ++recognized;
    });
    QObject::connect(&voice, &VoiceRecognizer::failed, &app, [&](const QString &) { ++failures; });
    QObject::connect(&voice, &VoiceRecognizer::stateChanged, &app, [&] {
        everListening = everListening || voice.listening();
    });

    QElapsedTimer time;
    time.start();
    voice.decodeFile(wav, QStringLiteral("fixture-field-1"));
    voice.decodeFile(wav, QStringLiteral("unexpected-concurrent-field"));
    if (!waitForDecoder(voice) || recognized != 1 || failures || everListening
        || token != QStringLiteral("fixture-field-1")
        || !transcript.contains(expected, Qt::CaseInsensitive)) {
        return fail("The decoder did not return the expected fixture fragment for the original field");
    }
    if (!scratchDirectory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        return fail("A temporary voice recording was retained after successful decoding");
    qInfo("Offline WAV recognized with the expected fragment in %lld ms", time.elapsed());

    voice.decodeFile(wav, QStringLiteral("cancelled-field"));
    if (!voice.busy())
        return fail("Second decoding request did not start");
    voice.cancel();
    if (!waitForDecoder(voice) || recognized != 1 || failures)
        return fail("A cancelled voice session published a result or error");

    // QML reacts to stateChanged synchronously when it changes focus.
    const auto revokeOnStart = QObject::connect(&voice, &VoiceRecognizer::stateChanged, &app, [&] {
        if (voice.busy())
            voice.cancel();
    });
    voice.decodeFile(wav, QStringLiteral("focus-changed-at-start"));
    QObject::disconnect(revokeOnStart);
    if (voice.busy() || recognized != 1 || failures || everListening
        || !scratchDirectory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        return fail("The immediately invalidated session retained work or audio");
    }

    voice.decodeFile(QUrl(QStringLiteral("https://example.invalid/audio.wav")),
                     QStringLiteral("nonlocal"));
    if (voice.busy() || failures != 1 || recognized != 1)
        return fail("The recognizer did not reject a nonlocal WAV without starting");
    qInfo("Concurrent request, cancellation, session invalidation, and local-only checks passed");
    return 0;
}
