#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class VoiceRecognizer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)

public:
    explicit VoiceRecognizer(QObject *parent = nullptr);
    ~VoiceRecognizer() override;

    bool available() const;
    bool decoderAvailable() const;
    bool listening() const { return m_phase == Recording; }
    bool busy() const { return m_phase != Idle; }

    Q_INVOKABLE void start(const QString &sessionToken);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void cancel();

    // Decode an explicitly selected local PCM file without opening the microphone.
    void decodeFile(const QUrl &audioFile, const QString &sessionToken);

signals:
    void stateChanged();
    void recognized(const QString &text, const QString &sessionToken);
    void failed(const QString &reason);

private:
    enum Phase { Idle, Recording, Recognizing, Cancelling };

    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    bool prepareAudio(const QString &sessionToken);
    void decode();
    quint64 reset();
    void setPhase(Phase phase);

    QString m_runtimeRoot;
    QString m_model;
    QString m_tokens;
    QString m_decoder;
    QString m_recorder;
    QString m_captureDevice;
    QString m_provider;
    QString m_sessionToken;
    std::unique_ptr<QTemporaryDir> m_audio;
    QProcess m_process;
    QTimer m_timeout;
    quint64 m_generation = 0;
    Phase m_phase = Idle;
    bool m_stopped = false;
};
