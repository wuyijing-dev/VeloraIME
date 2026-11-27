#include "voicerecognizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtEndian>

#include <csignal>
#include <unistd.h>

namespace {
constexpr auto bundledModel = "sherpa-onnx-rk3568-10-seconds-sense-voice-zh-en-ja-ko-yue-2025-09-09";
constexpr int samplesPerSecond = 16000;
constexpr int sampleBytes = 2;
constexpr int maximumAudioBytes = 10 * samplesPerSecond * sampleBytes;
constexpr int maximumWaveBytes = maximumAudioBytes + 64 * 1024;

bool validWave(QByteArrayView bytes)
{
    if (bytes.size() < 44 || bytes.first(4) != "RIFF" || bytes.sliced(8, 4) != "WAVE"
        || qFromLittleEndian<quint32>(bytes.data() + 4) != quint32(bytes.size() - 8)) {
        return false;
    }

    bool hasFormat = false;
    bool hasAudio = false;
    qsizetype index = 12;
    while (index + 8 <= bytes.size()) {
        const auto type = bytes.sliced(index, 4);
        const auto size = qFromLittleEndian<quint32>(bytes.data() + index + 4);
        index += 8;
        const qint64 paddedSize = qint64(size) + (size & 1);
        if (paddedSize > bytes.size() - index)
            return false;

        const char *data = bytes.data() + index;
        if (type == "fmt ") {
            if (hasFormat || size < 16 || qFromLittleEndian<quint16>(data) != 1
                || qFromLittleEndian<quint16>(data + 2) != 1
                || qFromLittleEndian<quint32>(data + 4) != samplesPerSecond
                || qFromLittleEndian<quint32>(data + 8) != samplesPerSecond * sampleBytes
                || qFromLittleEndian<quint16>(data + 12) != sampleBytes
                || qFromLittleEndian<quint16>(data + 14) != 16) {
                return false;
            }
            hasFormat = true;
        } else if (type == "data") {
            if (!hasFormat || hasAudio || size < 3200 || size > maximumAudioBytes
                || size % sampleBytes != 0) {
                return false;
            }
            hasAudio = true;
        }
        index += paddedSize;
    }
    return hasAudio && index == bytes.size();
}
}

VoiceRecognizer::VoiceRecognizer(QObject *parent)
    : QObject(parent),
      m_runtimeRoot(qEnvironmentVariable("VELORA_SPEECH_ROOT", QStringLiteral("/userdata/rk3568-voice"))),
      m_captureDevice(qEnvironmentVariable("VELORA_CAPTURE_DEVICE", QStringLiteral("plughw:0,0")))
{
    const QDir root(m_runtimeRoot);
    const auto modelDirectory = root.filePath(QStringLiteral("models/") + QString::fromLatin1(bundledModel));
    m_decoder = root.filePath(QStringLiteral("bin/sherpa-onnx-offline"));
    m_model = qEnvironmentVariable("VELORA_SPEECH_MODEL", modelDirectory + QStringLiteral("/model.rknn"));
    m_tokens = qEnvironmentVariable("VELORA_SPEECH_TOKENS", modelDirectory + QStringLiteral("/tokens.txt"));
    m_provider = qEnvironmentVariable("VELORA_SPEECH_PROVIDER",
                                      m_model.endsWith(u".rknn") ? QStringLiteral("rknn") : QStringLiteral("cpu"));
    m_recorder = QStandardPaths::findExecutable(QStringLiteral("arecord"));

    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        cancel();
        if (m_phase == Idle || m_phase == Cancelling)
            emit failed(tr("离线语音识别超时"));
    });
    connect(&m_process, &QProcess::finished, this, &VoiceRecognizer::finished);
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_phase == Recording && m_stopped)
            ::kill(pid_t(m_process.processId()), SIGINT);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const bool cancelled = m_phase == Cancelling;
            const auto generation = reset();
            if (!cancelled && generation == m_generation)
                emit failed(tr("无法启动离线录音或识别程序"));
        }
    });
}

VoiceRecognizer::~VoiceRecognizer()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool VoiceRecognizer::available() const
{
    return !m_recorder.isEmpty() && decoderAvailable();
}

bool VoiceRecognizer::decoderAvailable() const
{
    return QFileInfo(m_decoder).isExecutable()
        && QFileInfo(m_model).isFile() && QFileInfo(m_tokens).isFile();
}

bool VoiceRecognizer::prepareAudio(const QString &sessionToken)
{
    m_audio = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/velora-audio-XXXXXX"));
    if (!m_audio->isValid()) {
        reset();
        emit failed(tr("无法为录音分配临时目录"));
        return false;
    }
    m_sessionToken = sessionToken;
    ++m_generation;
    return true;
}

void VoiceRecognizer::start(const QString &sessionToken)
{
    if (busy() || !available() || sessionToken.isEmpty() || !prepareAudio(sessionToken))
        return;

    m_stopped = false;
    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.setStandardErrorFile(QProcess::nullDevice());
    const auto generation = m_generation;
    setPhase(Recording);
    if (generation != m_generation)
        return;
    m_timeout.start(12000);
    m_process.start(m_recorder, {QStringLiteral("-q"), QStringLiteral("-D"), m_captureDevice,
                                 QStringLiteral("-t"), QStringLiteral("wav"), QStringLiteral("-f"),
                                 QStringLiteral("S16_LE"), QStringLiteral("-r"), QStringLiteral("16000"),
                                 QStringLiteral("-c"), QStringLiteral("1"), QStringLiteral("-d"),
                                 QStringLiteral("8"), m_audio->filePath(QStringLiteral("input.wav"))});
}

void VoiceRecognizer::decodeFile(const QUrl &audioFile, const QString &sessionToken)
{
    if (busy() || !decoderAvailable() || sessionToken.isEmpty())
        return;

    const QString path = audioFile.toLocalFile();
    QFile input(path);
    if (!audioFile.isLocalFile() || !QFileInfo(path).isFile() || !input.open(QIODevice::ReadOnly)) {
        emit failed(tr("请选择本地 PCM WAV 音频"));
        return;
    }
    const auto audio = input.read(maximumWaveBytes + 1);
    if (audio.size() > maximumWaveBytes || !validWave(audio)) {
        emit failed(tr("只支持不超过 10 秒的 16 kHz 单声道 PCM WAV"));
        return;
    }
    if (!prepareAudio(sessionToken))
        return;
    QFile file(m_audio->filePath(QStringLiteral("input.wav")));
    if (!file.open(QIODevice::WriteOnly) || file.write(audio) != audio.size() || !file.flush()) {
        file.close();
        reset();
        emit failed(tr("无法准备音频文件"));
        return;
    }
    file.close();
    decode();
}

void VoiceRecognizer::stop()
{
    if (m_phase != Recording)
        return;
    m_stopped = true;
    if (m_process.state() == QProcess::Running)
        ::kill(pid_t(m_process.processId()), SIGINT);
}

void VoiceRecognizer::cancel()
{
    if (m_phase == Idle || m_phase == Cancelling)
        return;
    m_timeout.stop();
    setPhase(Cancelling);
    if (m_process.state() == QProcess::NotRunning)
        reset();
    else
        m_process.kill();
}

void VoiceRecognizer::decode()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString originalPath = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    const QString libraryPath = QDir(m_runtimeRoot).filePath(QStringLiteral("lib"));
    environment.insert(QStringLiteral("LD_LIBRARY_PATH"),
                       originalPath.isEmpty() ? libraryPath : libraryPath + u':' + originalPath);
    m_process.setProcessEnvironment(environment);
    m_process.setStandardOutputFile({});
    m_process.setStandardErrorFile({});
    const auto generation = m_generation;
    setPhase(Recognizing);
    if (generation != m_generation)
        return;
    m_timeout.start(20000);
    m_process.start(m_decoder, {QStringLiteral("--num-threads=1"),
                                QStringLiteral("--provider=") + m_provider,
                                QStringLiteral("--tokens=") + m_tokens,
                                QStringLiteral("--sense-voice-model=") + m_model,
                                QStringLiteral("--sense-voice-language=auto"),
                                QStringLiteral("--sense-voice-use-itn=true"),
                                m_audio->filePath(QStringLiteral("input.wav"))});
}

void VoiceRecognizer::finished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_phase == Cancelling) {
        reset();
        return;
    }
    if (m_phase == Recording) {
        const QFileInfo recording(m_audio->filePath(QStringLiteral("input.wav")));
        if ((exitCode == 0 || m_stopped) && recording.size() > 3200) {
            decode();
            return;
        }
        const auto generation = reset();
        if (generation == m_generation)
            emit failed(tr("录音过短或音频设备不可用"));
        return;
    }
    if (m_phase != Recognizing)
        return;

    QString text;
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        // Earlier sherpa-onnx releases wrote the recognition JSON to stderr.
        const QByteArray output = m_process.readAllStandardOutput() + '\n'
            + m_process.readAllStandardError();
        const auto lines = output.split('\n');
        for (const auto &line : lines) {
            if (line.trimmed().startsWith('{'))
                text = QJsonDocument::fromJson(line).object().value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty())
                break;
        }
    }
    const QString token = m_sessionToken;
    const auto generation = reset();
    if (generation != m_generation)
        return;
    if (text.isEmpty())
        emit failed(tr("未识别到可提交的文字"));
    else
        emit recognized(text, token);
}

quint64 VoiceRecognizer::reset()
{
    const auto generation = ++m_generation;
    m_timeout.stop();
    m_audio.reset();
    m_sessionToken.clear();
    setPhase(Idle);
    return generation;
}

void VoiceRecognizer::setPhase(Phase phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    emit stateChanged();
}
