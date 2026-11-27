#include "../lexiconpackmanager.h"
#include "../lexiconpackstore_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_LINUX)
#  include <csignal>
#  include <sys/resource.h>
#endif

using Velora::LexiconPackManager;
using namespace Velora::Packs;

namespace {

QDir fixtures()
{
    return QDir(qEnvironmentVariable("VELORA_PACK_TEST_FIXTURES"));
}

QByteArray publicKey()
{
    QFile file(fixtures().filePath(QStringLiteral("public.pem")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void configureManager(LexiconPackManager &manager, const QString &storage)
{
    manager.setStorageDirectory(storage);
    manager.setPackId(QStringLiteral("test-zh"));
    manager.setTrustedKeys({{"test-1", publicKey()}});
}

QByteArray bytesAt(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

#if defined(Q_OS_LINUX)
using SignalHandler = void (*)(int);

class FileSizeLimit
{
public:
    explicit FileSizeLimit(SignalHandler handler)
    {
        if (getrlimit(RLIMIT_FSIZE, &m_previous) != 0 || m_previous.rlim_cur < limit
            || m_previous.rlim_max < limit)
            return;
        m_previousSignal = std::signal(SIGXFSZ, handler);
        if (m_previousSignal == SIG_ERR)
            return;
        auto reduced = m_previous;
        reduced.rlim_cur = limit;
        m_applied = setrlimit(RLIMIT_FSIZE, &reduced) == 0;
    }

    ~FileSizeLimit()
    {
        if (m_applied)
            setrlimit(RLIMIT_FSIZE, &m_previous);
        if (m_previousSignal != SIG_ERR)
            std::signal(SIGXFSZ, m_previousSignal);
    }

    explicit operator bool() const { return m_applied; }
    static constexpr rlim_t limit = 4096;

private:
    rlimit m_previous{};
    SignalHandler m_previousSignal = SIG_ERR;
    bool m_applied = false;
};
#endif

} // namespace

class PackTests : public QObject
{
    Q_OBJECT

private slots:
    void validatesImportAndRollback();
    void managerExposesVerifiedSnapshot();
    void recoversVerifiedPreviousVersionOnRestart();
    void preservesSnapshotOnPartialWriteFailure();
    void recoversAfterUpdaterTerminatesMidWrite();
    void stopsExposingExpiredEntries();
    void updatesOverHttps();
};

void PackTests::validatesImportAndRollback()
{
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    const auto input = fixtures();
    const Config config{installed.path(), QStringLiteral("test-zh"), {{"test-1", publicKey()}}};
    QVERIFY(!publicKey().isEmpty());
    QCOMPARE(load(config).active.version, 0);

    const auto first = importDirectory(config, input.filePath(QStringLiteral("first")));
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    QCOMPARE(first.active.version, 1);
    QCOMPARE(first.entries.constFirst().toMap().value("code").toString(), QStringLiteral("velora"));
    QCOMPARE(first.entries.constFirst().toMap().value("locale").toString(), QStringLiteral("zh-CN"));
    QVERIFY(first.entries.constFirst().toMap().value("expiresAt").toDateTime().isValid());

    auto rejectUnchanged = [&](const QString &directory) {
        QVERIFY(!importDirectory(config, input.filePath(directory)).error.isEmpty());
        QCOMPARE(load(config).active.version, 1);
    };
    rejectUnchanged(QStringLiteral("invalid-signature"));
    rejectUnchanged(QStringLiteral("invalid-digest"));
    rejectUnchanged(QStringLiteral("bad-version-range"));
    rejectUnchanged(QStringLiteral("expired"));
    rejectUnchanged(QStringLiteral("future"));
    rejectUnchanged(QStringLiteral("oversize"));
    rejectUnchanged(QStringLiteral("first"));

    const auto second = importDirectory(config, input.filePath(QStringLiteral("second")));
    QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
    QCOMPARE(second.active.version, 2);
    QCOMPARE(second.previous, 1);

    const QDir packDir(QDir(installed.path()).filePath("test-zh"));
    // A staged file can remain if the process is interrupted before state.json is committed.
    QFile orphan(packDir.filePath("v99.pack"));
    QVERIFY(orphan.open(QIODevice::WriteOnly));
    orphan.write("interrupted");
    orphan.close();
    QCOMPARE(load(config).active.version, 2);

    const auto restored = rollback(config);
    QVERIFY2(restored.error.isEmpty(), qPrintable(restored.error));
    QCOMPARE(restored.active.version, 1);
    QCOMPARE(restored.previous, 0);
    QCOMPARE(restored.highest, 2);
    const auto replay = importDirectory(config, input.filePath(QStringLiteral("second")));
    QVERIFY(!replay.error.isEmpty());
    QCOMPARE(load(config).active.version, 1);

    QFile unwritableRoot(QDir(installed.path()).filePath("regular-file"));
    QVERIFY(unwritableRoot.open(QIODevice::WriteOnly));
    unwritableRoot.close();
    auto invalidStorage = config;
    invalidStorage.root = unwritableRoot.fileName();
    QVERIFY(!importDirectory(invalidStorage, input.filePath(QStringLiteral("first"))).error.isEmpty());
}

void PackTests::managerExposesVerifiedSnapshot()
{
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    const auto input = fixtures();
    LexiconPackManager manager;
    configureManager(manager, installed.path());
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
    QVERIFY(!manager.enabled());
    manager.importDirectory(QUrl::fromLocalFile(input.filePath(QStringLiteral("first"))));
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 1, 3000);
    QCOMPARE(manager.entries().size(), 1);
    QVERIFY(!manager.enabled());

    manager.importDirectory(QUrl::fromLocalFile(input.filePath(QStringLiteral("second"))));
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 2, 3000);
    QCOMPARE(manager.previousVersion(), 1);
    manager.rollback();
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 1, 3000);
    QCOMPARE(manager.previousVersion(), 0);
    QCOMPARE(manager.entries().size(), 1);
}

void PackTests::recoversVerifiedPreviousVersionOnRestart()
{
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    const auto input = fixtures();
    const Config config{installed.path(), QStringLiteral("test-zh"), {{"test-1", publicKey()}}};
    QVERIFY(importDirectory(config, input.filePath(QStringLiteral("first"))).error.isEmpty());
    QVERIFY(importDirectory(config, input.filePath(QStringLiteral("second"))).error.isEmpty());

    QFile damaged(QDir(installed.path()).filePath(QStringLiteral("test-zh/v2.pack")));
    QVERIFY(damaged.open(QIODevice::Append));
    QVERIFY(damaged.write("damaged") > 0);
    damaged.close();

    LexiconPackManager restarted;
    configureManager(restarted, installed.path());
    QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(), 3000);
    QVERIFY(!restarted.enabled());
    QCOMPARE(restarted.activeVersion(), 1);
    QCOMPARE(restarted.previousVersion(), 0);
    QCOMPARE(restarted.entries().size(), 1);
    QCOMPARE(load(config).highest, 2);
    QVERIFY(!importDirectory(config, input.filePath(QStringLiteral("second"))).error.isEmpty());
}

void PackTests::preservesSnapshotOnPartialWriteFailure()
{
#if defined(Q_OS_LINUX)
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    const auto input = fixtures();
    const auto update = QDir(input.filePath(QStringLiteral("large-update")));
    const auto versionOne = QUrl::fromLocalFile(input.filePath(QStringLiteral("first")));
    const auto versionThree = QUrl::fromLocalFile(update.path());
    const QDir cache(QDir(installed.path()).filePath(QStringLiteral("test-zh")));
    const auto statePath = cache.filePath(QStringLiteral("state.json"));
    const auto manifest = bytesAt(update.filePath(QStringLiteral("manifest.json")));
    QVERIFY(!manifest.isEmpty() && manifest.size() < qint64(FileSizeLimit::limit));
    QVERIFY(QFileInfo(update.filePath(QStringLiteral("pack-3.json"))).size()
            > qint64(FileSizeLimit::limit));

    LexiconPackManager manager;
    configureManager(manager, installed.path());
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
    manager.importDirectory(versionOne);
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 1, 3000);
    const auto originalEntries = manager.entries();
    QVERIFY(!originalEntries.isEmpty());
    const auto originalState = bytesAt(statePath);
    QVERIFY(!originalState.isEmpty());

    {
        // QSaveFile commits the small manifest but the kernel rejects the rest of the payload.
        const FileSizeLimit boundedStorage(SIG_IGN);
        QVERIFY2(bool(boundedStorage), "Could not set a process-local file size limit");
        manager.importDirectory(versionThree);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
        QCOMPARE(manager.lastError(), QStringLiteral("Could not save pack"));
        QCOMPARE(manager.activeVersion(), 1);
        QCOMPARE(manager.entries(), originalEntries);
        QCOMPARE(bytesAt(cache.filePath(QStringLiteral("v3.manifest"))), manifest);
        QVERIFY(!cache.exists(QStringLiteral("v3.pack")));
        QCOMPARE(bytesAt(statePath), originalState);

        LexiconPackManager restarted;
        configureManager(restarted, installed.path());
        QTRY_COMPARE_WITH_TIMEOUT(restarted.activeVersion(), 1, 3000);
        QCOMPARE(restarted.entries(), originalEntries);
    }

    manager.importDirectory(versionThree);
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 3, 3000);
    QCOMPARE(manager.previousVersion(), 1);
    QVERIFY(manager.entries() != originalEntries);
    QVERIFY(manager.lastError().isEmpty());
#else
    QSKIP("This scenario uses the Linux file size limit");
#endif
}

void PackTests::recoversAfterUpdaterTerminatesMidWrite()
{
#if defined(Q_OS_LINUX)
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    const auto input = fixtures();
    const QDir cache(QDir(installed.path()).filePath(QStringLiteral("test-zh")));
    const QString stagedUpdate = input.filePath(QStringLiteral("large-update"));
    const auto statePath = cache.filePath(QStringLiteral("state.json"));
    const Config config{installed.path(), QStringLiteral("test-zh"), {{"test-1", publicKey()}}};
    const auto first = importDirectory(config, input.filePath(QStringLiteral("first")));
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    const auto originalState = bytesAt(statePath);
    QVERIFY(!originalState.isEmpty());

    QProcess updater;
    updater.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--interrupted-import"), installed.path(), stagedUpdate});
    QVERIFY(updater.waitForStarted());
    QVERIFY(updater.waitForFinished(5000));
    QCOMPARE(updater.exitStatus(), QProcess::CrashExit);
    QCOMPARE(updater.exitCode(), SIGXFSZ);
    QCOMPARE(bytesAt(cache.filePath(QStringLiteral("v3.manifest"))),
             bytesAt(QDir(stagedUpdate).filePath(QStringLiteral("manifest.json"))));
    QVERIFY(!cache.exists(QStringLiteral("v3.pack")));
    QCOMPARE(bytesAt(statePath), originalState);

    LexiconPackManager restarted;
    configureManager(restarted, installed.path());
    QTRY_COMPARE_WITH_TIMEOUT(restarted.activeVersion(), 1, 3000);
    QCOMPARE(restarted.entries(), first.entries);

    restarted.importDirectory(QUrl::fromLocalFile(stagedUpdate));
    QTRY_COMPARE_WITH_TIMEOUT(restarted.activeVersion(), 3, 3000);
    QCOMPARE(restarted.previousVersion(), 1);
    QVERIFY(restarted.lastError().isEmpty());
#else
    QSKIP("This scenario uses a fatal Linux file size signal");
#endif
}

void PackTests::stopsExposingExpiredEntries()
{
    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    LexiconPackManager manager;
    configureManager(manager, installed.path());
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
    manager.importDirectory(QUrl::fromLocalFile(fixtures().filePath(QStringLiteral("expiring-entry"))));
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 3, 3000);
    QCOMPARE(manager.entries().size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(manager.entries().isEmpty(), 15000);
    QCOMPARE(manager.activeVersion(), 3);
    QVERIFY(!manager.enabled());

    LexiconPackManager restarted;
    configureManager(restarted, installed.path());
    QTRY_COMPARE_WITH_TIMEOUT(restarted.activeVersion(), 3, 3000);
    QVERIFY(restarted.entries().isEmpty());
}

void PackTests::updatesOverHttps()
{
    if (qEnvironmentVariableIsEmpty("VELORA_PACK_TEST_URL"))
        QSKIP("The local HTTPS fixture server is not configured");
    QVERIFY(QSslSocket::supportsSsl());
    const auto ca = QSslCertificate::fromPath(fixtures().filePath(QStringLiteral("tls.crt")));
    QCOMPARE(ca.size(), 1);
    auto sslConfiguration = QSslConfiguration::defaultConfiguration();
    sslConfiguration.setCaCertificates(ca);
    QSslConfiguration::setDefaultConfiguration(sslConfiguration);

    QTemporaryDir installed;
    QVERIFY(installed.isValid());
    LexiconPackManager manager;
    configureManager(manager, installed.path());
    const QUrl origin(qEnvironmentVariable("VELORA_PACK_TEST_URL"));
    manager.setManifestUrl(origin.resolved(QUrl("offline-disabled/manifest.json")));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
    QCOMPARE(manager.activeVersion(), 0);
    QVERIFY(!manager.enabled());
    manager.checkForUpdates();
    QTest::qWait(200);
    QVERIFY(!manager.busy());
    manager.setManifestUrl(origin.resolved(QUrl("first/manifest.json")));
    manager.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 1, 10'000);
    QCOMPARE(manager.entries().size(), 1);
    QVERIFY2(manager.lastError().isEmpty(), qPrintable(manager.lastError()));

    manager.checkForUpdates();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 3000);
    QCOMPARE(manager.activeVersion(), 1);
    QVERIFY2(manager.lastError().isEmpty(), qPrintable(manager.lastError()));

    manager.setManifestUrl(origin.resolved(QUrl("invalid-signature/manifest.json")));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.lastError().isEmpty(), 3000);
    QCOMPARE(manager.activeVersion(), 1);

    manager.setManifestUrl(origin.resolved(QUrl("invalid-digest/manifest.json")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.lastError(),
                              QStringLiteral("Pack digest or entries are invalid"), 3000);
    QCOMPARE(manager.activeVersion(), 1);

    manager.setManifestUrl(origin.resolved(QUrl("second/manifest.json")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.activeVersion(), 2, 3000);
    QCOMPARE(manager.previousVersion(), 1);
    manager.setEnabled(false);

    LexiconPackManager offline;
    configureManager(offline, installed.path());
    offline.setManifestUrl(origin.resolved(QUrl("offline-disabled/manifest.json")));
    QTRY_COMPARE_WITH_TIMEOUT(offline.activeVersion(), 2, 3000);
    QCOMPARE(offline.entries().size(), 1);
    QVERIFY(!offline.enabled());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#if defined(Q_OS_LINUX)
    const auto arguments = app.arguments();
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--import-fixture")) {
        const Config config{arguments.at(2), QStringLiteral("test-zh"), {{"test-1", publicKey()}}};
        const auto result = importDirectory(config, arguments.at(3));
        if (!result.error.isEmpty())
            qWarning().noquote() << result.error;
        return result.error.isEmpty() ? 0 : 1;
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--interrupted-import")) {
        const rlimit noCoreDump{0, 0};
        if (setrlimit(RLIMIT_CORE, &noCoreDump) != 0)
            return 2;
        const FileSizeLimit boundedStorage(SIG_DFL);
        if (!boundedStorage)
            return 3;
        const Config config{arguments.at(2), QStringLiteral("test-zh"), {{"test-1", publicKey()}}};
        importDirectory(config, arguments.at(3));
        return 4;
    }
#endif
    PackTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "pack_tests.moc"
