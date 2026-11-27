#include "lexiconpackstore_p.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <memory>

namespace Velora::Packs {
namespace {

constexpr int maxEntries = 20000;
constexpr int protocolVersion = 1;
constexpr int clientVersion = 1;
constexpr qint64 maxStateBytes = 4096;

struct State {
    int current = 0;
    int previous = 0;
    int highest = 0;
};

Result failure(const QString &error)
{
    Result result;
    result.error = error;
    return result;
}

QDateTime parseTime(const QJsonValue &value)
{
    const QString time = value.toString();
    if (!time.endsWith(u'Z'))
        return {};
    return QDateTime::fromString(time, Qt::ISODate);
}

bool usableNow(const Manifest &manifest)
{
    const auto now = QDateTime::currentDateTimeUtc();
    return now.addSecs(300) >= manifest.publishedAt && now < manifest.expiresAt;
}

bool verify(const QByteArray &trustedPem, const QByteArray &payload, const QByteArray &signature)
{
    if (trustedPem.isEmpty() || trustedPem.size() > 1024 || signature.size() != 64)
        return false;

    const std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(trustedPem.constData(), static_cast<int>(trustedPem.size())), BIO_free);
    if (!bio)
        return false;

    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && context && EVP_PKEY_id(key.get()) == EVP_PKEY_ED25519
        && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1
        && EVP_DigestVerify(context.get(),
                            reinterpret_cast<const unsigned char *>(signature.constData()),
                            static_cast<size_t>(signature.size()),
                            reinterpret_cast<const unsigned char *>(payload.constData()),
                            static_cast<size_t>(payload.size())) == 1;
}

bool parseEntries(const Manifest &manifest, const QByteArray &bytes, QVariantList *entries)
{
    if (bytes.size() != manifest.size
        || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != manifest.digest)
        return false;

    const auto document = QJsonDocument::fromJson(bytes);
    const auto values = document.array();
    if (!document.isArray() || values.isEmpty() || values.size() > maxEntries)
        return false;

    static const QRegularExpression pinyinPattern(
        QStringLiteral("^[a-z]+(?:'[a-z]+)*\\z"), QRegularExpression::DontCaptureOption);
    static const QRegularExpression controlPattern(
        QStringLiteral("[\\p{Cc}\\p{Cf}\\p{Zl}\\p{Zp}]"));
    QSet<QPair<QString, QString>> seen;
    entries->reserve(values.size());
    for (const auto &value : values) {
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        const auto text = object.value("text").toString();
        const auto pinyin = object.value("pinyin").toString();
        const int weight = object.value("weight").toInt();
        const auto expiresValue = object.value("expiresAt");
        const auto expiresAt = parseTime(expiresValue);
        const auto identity = qMakePair(text, pinyin);
        if (text.isEmpty() || text.size() > 48 || text.trimmed() != text
            || !text.isValidUtf16()
            || text.normalized(QString::NormalizationForm_C) != text
            || controlPattern.match(text).hasMatch()
            || pinyin.size() > 64 || !pinyinPattern.match(pinyin).hasMatch()
            || weight < 1 || weight > 100 || seen.contains(identity)
            || (!expiresValue.isUndefined() && (!expiresAt.isValid()
                                                || expiresAt <= manifest.publishedAt))) {
            return false;
        }
        seen.insert(identity);
        entries->append(QVariantMap{{"text", text},
                                    {"code", pinyin},
                                    {"kind", manifest.kind},
                                    {"locale", manifest.locale},
                                    {"weight", weight},
                                    {"expiresAt", expiresAt.isValid()
                                         ? qMin(expiresAt, manifest.expiresAt)
                                         : manifest.expiresAt}});
    }
    return true;
}

QByteArray readBounded(const QString &path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {};
    const auto content = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || content.size() > limit)
        return {};
    return content;
}

bool writeAtomic(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size()
        && file.commit();
}

QString slotFileName(int version, QStringView suffix)
{
    return QStringLiteral("v%1%2").arg(version).arg(suffix);
}

QDir packDir(const Config &config)
{
    return QDir(QDir(config.root).filePath(config.packId));
}

bool readState(const QDir &dir, State *state)
{
    static const auto name = QStringLiteral("state.json");
    if (!dir.exists(name))
        return true;

    const auto document = QJsonDocument::fromJson(readBounded(dir.filePath(name), maxStateBytes));
    if (!document.isObject())
        return false;

    const auto object = document.object();
    state->current = object.value("current").toInt(-1);
    state->previous = object.value("previous").toInt(-1);
    state->highest = object.value("highest").toInt(-1);
    return state->current > 0 && state->previous >= 0 && state->highest >= state->current
        && state->previous < state->current;
}

bool writeState(const QDir &dir, const State &state)
{
    const QJsonObject value{{"current", state.current}, {"previous", state.previous},
                            {"highest", state.highest}};
    return writeAtomic(dir.filePath(QStringLiteral("state.json")),
                       QJsonDocument(value).toJson(QJsonDocument::Compact));
}

Result loadSlot(const Config &config, const QDir &dir, int version)
{
    const auto envelope = readBounded(dir.filePath(slotFileName(version, u".manifest")),
                                     maxManifestBytes);
    Result result;
    if (!readManifest(config, envelope, &result.active, &result.error)
        || result.active.version != version)
        return failure(QStringLiteral("Cached manifest is invalid"));

    const auto payload = readBounded(dir.filePath(slotFileName(version, u".pack")), maxPackBytes);
    if (!parseEntries(result.active, payload, &result.entries))
        return failure(QStringLiteral("Cached pack is invalid"));

    return result;
}

void removeUnusedSlots(const QDir &dir, const State &state)
{
    static const QRegularExpression slotPattern(QStringLiteral("^v([1-9][0-9]*)\\.(manifest|pack)\\z"));
    for (const auto &file : dir.entryList(QDir::Files)) {
        const auto match = slotPattern.match(file);
        if (!match.hasMatch())
            continue;
        const int version = match.captured(1).toInt();
        if (version != state.current && version != state.previous)
            QFile::remove(dir.filePath(file));
    }
}

} // namespace

bool validPackId(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,39}\\z"));
    return pattern.match(value).hasMatch();
}

bool readManifest(const Config &config, const QByteArray &envelope, Manifest *manifest,
                  QString *error)
{
    *error = QStringLiteral("Invalid pack manifest");
    if (!validPackId(config.packId) || envelope.isEmpty() || envelope.size() > maxManifestBytes)
        return false;

    const auto outer = QJsonDocument::fromJson(envelope).object();
    const auto payload = QByteArray::fromBase64Encoding(outer.value("signed").toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    const auto signature = QByteArray::fromBase64Encoding(
        outer.value("signature").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!payload || !signature || payload.decoded.isEmpty())
        return false;

    const auto fields = QJsonDocument::fromJson(payload.decoded).object();
    const auto keyId = fields.value("keyId").toString();
    if (!validPackId(keyId)
        || !verify(config.trustedKeys.value(keyId).toByteArray(), payload.decoded,
                   signature.decoded)) {
        *error = QStringLiteral("Untrusted or invalid pack signature");
        return false;
    }

    Manifest verified;
    verified.packId = fields.value("packId").toString();
    verified.kind = fields.value("kind").toString();
    verified.locale = fields.value("locale").toString();
    verified.version = fields.value("version").toInt();
    verified.size = fields.value("size").toInt();
    verified.fileName = fields.value("url").toString();
    verified.publishedAt = parseTime(fields.value("publishedAt"));
    verified.expiresAt = parseTime(fields.value("expiresAt"));
    const auto digest = fields.value("sha256").toString();
    static const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}\\z"));
    const QString expectedFile = QStringLiteral("pack-%1.json").arg(verified.version);
    const int minimum = fields.value("minClientVersion").toInt();
    const int maximum = fields.value("maxClientVersion").toInt();

    if (fields.value("schemaVersion").toInt() != protocolVersion || minimum <= 0
        || clientVersion < minimum || clientVersion > maximum
        || verified.packId != config.packId || verified.version <= 0
        || (verified.kind != QStringLiteral("hotwords")
            && verified.kind != QStringLiteral("dictionary"))
        || (verified.locale != QStringLiteral("zh-CN")
            && verified.locale != QStringLiteral("en-US"))
        || verified.size <= 0 || verified.size > maxPackBytes || verified.fileName != expectedFile
        || !verified.publishedAt.isValid() || !verified.expiresAt.isValid()
        || verified.publishedAt >= verified.expiresAt || !digestPattern.match(digest).hasMatch()) {
        return false;
    }

    verified.digest = QByteArray::fromHex(digest.toLatin1());
    *manifest = verified;
    error->clear();
    return true;
}

Result load(const Config &config)
{
    if (!validPackId(config.packId) || config.root.isEmpty())
        return failure(QStringLiteral("Invalid pack storage configuration"));

    const auto dir = packDir(config);
    if (!dir.exists())
        return {};
    QLockFile lock(dir.filePath(QStringLiteral(".update-lock")));
    if (!lock.tryLock(1000))
        return failure(QStringLiteral("Pack storage is busy"));

    State state;
    if (!readState(dir, &state))
        return failure(QStringLiteral("Pack state is invalid"));
    if (state.current == 0)
        return {};
    auto result = loadSlot(config, dir, state.current);
    if (!result.error.isEmpty() && state.previous > 0) {
        result = loadSlot(config, dir, state.previous);
        if (result.error.isEmpty()) {
            state.current = state.previous;
            state.previous = 0;
            if (!writeState(dir, state))
                return failure(QStringLiteral("Could not recover pack state"));
            removeUnusedSlots(dir, state);
        }
    }
    result.highest = state.highest;
    result.previous = state.previous;
    return result;
}

Result install(const Config &config, const QByteArray &envelope, const QByteArray &payload)
{
    Result result;
    if (!readManifest(config, envelope, &result.active, &result.error))
        return result;
    if (!usableNow(result.active))
        return failure(QStringLiteral("Pack is expired or device time is incorrect"));
    if (!parseEntries(result.active, payload, &result.entries))
        return failure(QStringLiteral("Pack digest or entries are invalid"));

    const auto dir = packDir(config);
    if (config.root.isEmpty() || !QDir().mkpath(dir.absolutePath()))
        return failure(QStringLiteral("Cannot create pack storage"));
    QLockFile lock(dir.filePath(QStringLiteral(".update-lock")));
    if (!lock.tryLock(1000))
        return failure(QStringLiteral("Pack storage is busy"));

    State state;
    if (!readState(dir, &state))
        return failure(QStringLiteral("Pack state is invalid"));
    const int version = result.active.version;
    if (version <= state.highest)
        return failure(QStringLiteral("Pack version has already been seen"));

    if (!writeAtomic(dir.filePath(slotFileName(version, u".manifest")), envelope)
        || !writeAtomic(dir.filePath(slotFileName(version, u".pack")), payload)
        || !writeState(dir, {version, state.current, version})) {
        return failure(QStringLiteral("Could not save pack"));
    }

    result.highest = version;
    result.previous = state.current;
    removeUnusedSlots(dir, {version, state.current, version});
    return result;
}

Result importDirectory(const Config &config, const QString &directory)
{
    const QDir source(directory);
    const auto envelope = readBounded(source.filePath(QStringLiteral("manifest.json")),
                                     maxManifestBytes);
    Manifest manifest;
    QString error;
    if (!readManifest(config, envelope, &manifest, &error))
        return failure(error);

    const auto payload = readBounded(source.filePath(manifest.fileName), manifest.size);
    return install(config, envelope, payload);
}

Result rollback(const Config &config)
{
    if (!validPackId(config.packId) || config.root.isEmpty())
        return failure(QStringLiteral("Invalid pack storage configuration"));

    const auto dir = packDir(config);
    if (!dir.exists())
        return failure(QStringLiteral("No previous pack version"));
    QLockFile lock(dir.filePath(QStringLiteral(".update-lock")));
    if (!lock.tryLock(1000))
        return failure(QStringLiteral("Pack storage is busy"));

    State state;
    if (!readState(dir, &state) || state.previous == 0)
        return failure(QStringLiteral("No previous pack version"));
    auto result = loadSlot(config, dir, state.previous);
    if (!result.error.isEmpty())
        return result;
    if (!usableNow(result.active))
        return failure(QStringLiteral("Previous pack is expired or device time is incorrect"));

    const State restored{state.previous, 0, state.highest};
    if (!writeState(dir, restored))
        return failure(QStringLiteral("Could not save previous pack state"));
    removeUnusedSlots(dir, restored);
    result.highest = state.highest;
    return result;
}

} // namespace Velora::Packs
