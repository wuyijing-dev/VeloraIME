#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace Velora::Packs {

inline constexpr qint64 maxManifestBytes = 16 * 1024;
inline constexpr qint64 maxPackBytes = 2 * 1024 * 1024;

struct Config {
    QString root;
    QString packId;
    QVariantMap trustedKeys;
};

struct Manifest {
    QString packId;
    QString kind;
    QString locale;
    QString fileName;
    int version = 0;
    int size = 0;
    QDateTime publishedAt;
    QDateTime expiresAt;
    QByteArray digest;
};

struct Result {
    QString error;
    Manifest active;
    QVariantList entries;
    int previous = 0;
    int highest = 0;
};

bool validPackId(const QString &value);
bool readManifest(const Config &config, const QByteArray &envelope, Manifest *manifest, QString *error);
Result load(const Config &config);
Result importDirectory(const Config &config, const QString &directory);
Result install(const Config &config, const QByteArray &envelope, const QByteArray &payload);
Result rollback(const Config &config);

} // namespace Velora::Packs
