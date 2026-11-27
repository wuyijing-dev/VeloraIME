#include "lexiconpackmanager.h"

#include <QDir>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QtConcurrentRun>

#include <memory>
#include <utility>

namespace Velora {
namespace {

constexpr int checkInterval = 24 * 60 * 60;
constexpr int retryStart = 5 * 60;

bool isHttps(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}

} // namespace

LexiconPackManager::LexiconPackManager(QObject *parent)
    : QObject(parent)
{
    m_config.root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                        .filePath(QStringLiteral("word-packs"));

    m_reloadTimer.setSingleShot(true);
    m_checkTimer.setSingleShot(true);
    m_expiryTimer.setSingleShot(true);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        if (m_storageBusy)
            return;
        m_loadPending = false;
        const auto config = m_config;
        if (Packs::validPackId(config.packId) && !config.trustedKeys.isEmpty())
            runStorage([config] { return Packs::load(config); }, Operation::Load);
        else
            updateBusy();
    });
    connect(&m_checkTimer, &QTimer::timeout, this, &LexiconPackManager::checkForUpdates);
    connect(&m_expiryTimer, &QTimer::timeout, this, &LexiconPackManager::refreshEntries);
}

void LexiconPackManager::setPackId(const QString &value)
{
    if (m_config.packId == value)
        return;
    m_config.packId = value;
    scheduleReload();
    emit packIdChanged();
}

void LexiconPackManager::setManifestUrl(const QUrl &value)
{
    if (m_manifestUrl == value)
        return;
    m_manifestUrl = value;
    ++m_sourceGeneration;
    m_etag.clear();
    m_receivedEtag.clear();
    m_checkTimer.stop();
    cancelDownload();
    if (m_enabled)
        scheduleCheck(0);
    emit manifestUrlChanged();
}

void LexiconPackManager::setTrustedKeys(const QVariantMap &value)
{
    if (m_config.trustedKeys == value)
        return;
    m_config.trustedKeys = value;
    scheduleReload();
    emit trustedKeysChanged();
}

void LexiconPackManager::setStorageDirectory(const QString &value)
{
    if (m_config.root == value)
        return;
    m_config.root = value;
    scheduleReload();
    emit storageDirectoryChanged();
}

void LexiconPackManager::setEnabled(bool value)
{
    if (m_enabled == value)
        return;
    m_enabled = value;
    if (value) {
        m_backoff = retryStart;
        scheduleCheck(0);
    } else {
        m_checkTimer.stop();
        cancelDownload();
    }
    emit enabledChanged();
}

void LexiconPackManager::scheduleReload()
{
    ++m_generation;
    m_etag.clear();
    m_receivedEtag.clear();
    m_checkTimer.stop();
    cancelDownload();
    m_loadPending = true;
    adopt({});
    setError({});
    m_reloadTimer.start(0);
    updateBusy();
}

void LexiconPackManager::runStorage(std::function<Packs::Result()> work, Operation operation)
{
    m_storageBusy = true;
    updateBusy();
    const quint64 generation = m_generation;
    const quint64 sourceGeneration = m_sourceGeneration;
    auto *watcher = new QFutureWatcher<Packs::Result>(this);
    connect(watcher, &QFutureWatcher<Packs::Result>::finished, this,
            [this, watcher, generation, sourceGeneration, operation] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_storageBusy = false;
        if (generation == m_generation) {
            const QString error = result.error;
            if (error.isEmpty() || operation == Operation::Load)
                adopt(std::move(result));

            if (operation == Operation::NetworkInstall && sourceGeneration == m_sourceGeneration) {
                if (error.isEmpty())
                    m_etag = std::exchange(m_receivedEtag, {});
                completeCheck(error);
            } else {
                setError(error);
                if (operation == Operation::Load)
                    scheduleCheck(0);
            }
        }
        if (m_loadPending)
            m_reloadTimer.start(0);
        updateBusy();
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

void LexiconPackManager::adopt(Packs::Result result)
{
    const bool changed = result.active.version != activeVersion()
        || result.previous != previousVersion();
    m_active = std::move(result);
    refreshEntries();
    if (changed)
        emit versionsChanged();
}

void LexiconPackManager::refreshEntries()
{
    QVariantList entries;
    m_expiryTimer.stop();
    if (m_active.active.version > 0) {
        const auto now = QDateTime::currentDateTimeUtc();
        const auto publication = m_active.active.publishedAt;
        const auto expiration = m_active.active.expiresAt;
        qint64 delayMs = 60'000;
        if (now.addSecs(300) >= publication && now < expiration) {
            entries.reserve(m_active.entries.size());
            delayMs = qMin(delayMs, now.msecsTo(expiration));
            for (const auto &item : m_active.entries) {
                const auto expiresAt = item.toMap().value("expiresAt").toDateTime();
                if (expiresAt > now) {
                    entries.append(item);
                    delayMs = qMin(delayMs, now.msecsTo(expiresAt));
                }
            }
        }
        m_expiryTimer.start(int(qMax<qint64>(1, delayMs)));
    }
    if (entries != m_entries) {
        m_entries = std::move(entries);
        emit entriesChanged();
    }
}

void LexiconPackManager::updateBusy()
{
    const bool busy = m_storageBusy || m_reply || m_loadPending;
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void LexiconPackManager::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        emit lastErrorChanged();
    }
}

void LexiconPackManager::scheduleCheck(int seconds)
{
    if (m_enabled)
        m_checkTimer.start(seconds * 1000);
}

void LexiconPackManager::completeCheck(const QString &error)
{
    setError(error);
    if (error.isEmpty()) {
        m_backoff = retryStart;
        scheduleCheck(checkInterval);
    } else {
        scheduleCheck(m_backoff);
        m_backoff = qMin(m_backoff * 2, checkInterval);
    }
    updateBusy();
}

void LexiconPackManager::cancelDownload()
{
    if (!m_reply)
        return;
    auto *reply = std::exchange(m_reply, nullptr);
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    updateBusy();
}

void LexiconPackManager::fetch(const QUrl &url, qint64 limit, const QByteArray &etag,
                              DownloadFinished finished)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(15'000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setRawHeader("User-Agent", "VeloraIME-Pack/1");
    request.setRawHeader("Accept", "application/json");
    if (!etag.isEmpty())
        request.setRawHeader("If-None-Match", etag);

    auto *reply = m_network.get(request);
    m_reply = reply;
    reply->setReadBufferSize(qMin<qint64>(limit + 1, 64 * 1024));
    QTimer::singleShot(30'000, reply, [reply] { reply->abort(); });
    auto bytes = std::make_shared<QByteArray>();
    const auto drain = [reply, bytes, limit] {
        const qint64 available = limit + 1 - bytes->size();
        if (available > 0)
            bytes->append(reply->read(available));
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, limit, drain] {
        drain();
        if (bytes->size() > limit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, limit, drain, finished] {
        drain();
        auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (bytes->size() > limit || reply->error() != QNetworkReply::NoError)
            status = 0;
        const auto etag = reply->rawHeader("ETag");
        m_reply = nullptr;
        reply->deleteLater();
        finished(std::move(*bytes), etag.size() <= 256 ? etag : QByteArray{}, status);
        updateBusy();
    });
    updateBusy();
}

void LexiconPackManager::checkForUpdates()
{
    if (!m_enabled)
        return;
    if (m_busy) {
        scheduleCheck(1);
        return;
    }
    if (!isHttps(m_manifestUrl) || !Packs::validPackId(m_config.packId)
        || m_config.trustedKeys.isEmpty()) {
        setError(tr("Set an HTTPS manifest URL, pack ID, and trusted keys"));
        return;
    }

    setError({});
    fetch(m_manifestUrl, Packs::maxManifestBytes, m_etag,
          [this](QByteArray envelope, QByteArray etag, int status) {
        if (status == 304 && !m_etag.isEmpty()) {
            completeCheck();
            return;
        }
        if (status != 200) {
            completeCheck(tr("Manifest download failed"));
            return;
        }

        Packs::Manifest manifest;
        QString error;
        const auto config = m_config;
        if (!Packs::readManifest(config, envelope, &manifest, &error)) {
            completeCheck(error);
            return;
        }
        if (manifest.version <= m_active.highest) {
            if (manifest.version == m_active.highest) {
                m_etag = etag;
                completeCheck();
            } else {
                completeCheck(tr("The server offered an older pack"));
            }
            return;
        }
        const QUrl url = m_manifestUrl.resolved(QUrl(manifest.fileName));
        fetch(url, manifest.size, {},
              [this, config, envelope = std::move(envelope), etag](QByteArray payload, QByteArray,
                                                                int packStatus) {
            if (packStatus != 200) {
                completeCheck(tr("Pack download failed"));
                return;
            }
            m_receivedEtag = etag;
            runStorage([config, envelope, payload = std::move(payload)] {
                return Packs::install(config, envelope, payload);
            }, Operation::NetworkInstall);
        });
    });
}

void LexiconPackManager::importDirectory(const QUrl &directory)
{
    if (m_busy)
        return;
    if (!directory.isLocalFile() || directory.toLocalFile().isEmpty()) {
        setError(tr("Choose a local pack directory"));
        return;
    }
    const auto config = m_config;
    const auto path = directory.toLocalFile();
    setError({});
    runStorage([config, path] { return Packs::importDirectory(config, path); }, Operation::LocalChange);
}

void LexiconPackManager::rollback()
{
    if (m_busy)
        return;
    const auto config = m_config;
    setError({});
    runStorage([config] { return Packs::rollback(config); }, Operation::LocalChange);
}

} // namespace Velora
