#pragma once

#include "lexiconpackstore_p.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace Velora {

class LexiconPackManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString packId READ packId WRITE setPackId NOTIFY packIdChanged)
    Q_PROPERTY(QUrl manifestUrl READ manifestUrl WRITE setManifestUrl NOTIFY manifestUrlChanged)
    Q_PROPERTY(QVariantMap trustedKeys READ trustedKeys WRITE setTrustedKeys NOTIFY trustedKeysChanged)
    Q_PROPERTY(QString storageDirectory READ storageDirectory WRITE setStorageDirectory
               NOTIFY storageDirectoryChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int activeVersion READ activeVersion NOTIFY versionsChanged)
    Q_PROPERTY(int previousVersion READ previousVersion NOTIFY versionsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit LexiconPackManager(QObject *parent = nullptr);

    QString packId() const { return m_config.packId; }
    void setPackId(const QString &value);
    QUrl manifestUrl() const { return m_manifestUrl; }
    void setManifestUrl(const QUrl &value);
    QVariantMap trustedKeys() const { return m_config.trustedKeys; }
    void setTrustedKeys(const QVariantMap &value);
    QString storageDirectory() const { return m_config.root; }
    void setStorageDirectory(const QString &value);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool value);
    QVariantList entries() const { return m_entries; }
    int activeVersion() const { return m_active.active.version; }
    int previousVersion() const { return m_active.previous; }
    bool busy() const { return m_busy; }
    QString lastError() const { return m_error; }

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void importDirectory(const QUrl &directory);
    Q_INVOKABLE void rollback();

signals:
    void packIdChanged();
    void manifestUrlChanged();
    void trustedKeysChanged();
    void storageDirectoryChanged();
    void enabledChanged();
    void entriesChanged();
    void versionsChanged();
    void busyChanged();
    void lastErrorChanged();

private:
    using DownloadFinished = std::function<void(QByteArray, QByteArray, int)>;
    enum class Operation { Load, LocalChange, NetworkInstall };

    void scheduleReload();
    void runStorage(std::function<Packs::Result()> work, Operation operation);
    void adopt(Packs::Result result);
    void refreshEntries();
    void updateBusy();
    void setError(const QString &error);
    void completeCheck(const QString &error = {});
    void scheduleCheck(int seconds);
    void cancelDownload();
    void fetch(const QUrl &url, qint64 limit, const QByteArray &etag, DownloadFinished finished);

    Packs::Config m_config;
    Packs::Result m_active;
    QVariantList m_entries;
    QUrl m_manifestUrl;
    QTimer m_reloadTimer;
    QTimer m_checkTimer;
    QTimer m_expiryTimer;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_etag;
    QByteArray m_receivedEtag;
    QString m_error;
    int m_backoff = 60 * 5;
    quint64 m_generation = 0;
    quint64 m_sourceGeneration = 0;
    bool m_enabled = false;
    bool m_storageBusy = false;
    bool m_loadPending = false;
    bool m_busy = false;
};

} // namespace Velora
