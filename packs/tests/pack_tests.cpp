#include "../lexiconpackmanager.h"
#include "../lexiconpackstore_p.h"

#include <QDir>
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>

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

} // namespace

class PackTests : public QObject
{
    Q_OBJECT

private slots:
    void validatesImportAndRollback();
    void managerExposesVerifiedSnapshot();
    void recoversVerifiedPreviousVersionOnRestart();
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

QTEST_GUILESS_MAIN(PackTests)
#include "pack_tests.moc"
