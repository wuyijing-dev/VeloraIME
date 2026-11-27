#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <optional>

namespace Velora {

class HandwritingRecognizer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QStringList candidates READ candidates NOTIFY candidatesChanged FINAL)

public:
    explicit HandwritingRecognizer(QObject *parent = nullptr);
    ~HandwritingRecognizer() override;

    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QStringList candidates() const { return m_candidates; }

    Q_INVOKABLE void request(const QVariantList &strokes, double width, double height);
    Q_INVOKABLE void cancel();

signals:
    void availableChanged();
    void busyChanged();
    void candidatesChanged();

private:
    struct Backend;
    using Strokes = QList<QList<QPoint>>;

    void startNext();
    void setCandidates(QStringList candidates);
    void setBusy(bool busy);

    std::shared_ptr<Backend> m_backend;
    std::optional<Strokes> m_pending;
    QStringList m_candidates;
    quint64 m_generation = 0;
    bool m_available = false;
    bool m_busy = false;
    bool m_running = false;
};

} // namespace Velora
