#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>

struct KeyboardWord {
    QString text;
    QString code;
    int weight = 0;
    QDateTime expiresAt;
    QString locale;
};

struct KeyboardCandidate {
    QString text;
    QString code;
    qsizetype consumed = 0;
    int score = 0;
    bool correction = false;
};

struct KeyboardQuery {
    QString input;
    QString language;
    QString pinyinMode;
    QString profile;
    QList<KeyboardWord> supplemental;
    bool fuzzy = false;
    bool useLearning = true;
    bool swipe = false;
};

class KeyboardLexicon : public QObject
{
public:
    using QObject::QObject;
    ~KeyboardLexicon() override;

    void warm(const QString &locale, const QString &pinyinMode);
    QList<KeyboardCandidate> candidates(const KeyboardQuery &query);
    bool remember(const QString &profile, const QString &locale, const QString &code,
                  const QString &text);
    bool forget(const QString &profile, const QString &locale, const QString &code,
                const QString &text);
    bool setPhrase(const QString &profile, const QString &locale, const QString &code,
                   const QString &text);
    bool removePhrase(const QString &profile, const QString &locale, const QString &code,
                      const QString &text);
    bool clearLearning(const QString &profile);
    bool exportProfile(const QString &profile, const QString &path);
    bool importProfile(const QString &profile, const QString &path);
    QStringList recentEmoji(const QString &profile);
    bool rememberEmoji(const QString &profile, const QString &emoji);
    QVariantList userPhrases(const QString &profile, const QString &locale);

private:
    struct ChineseWord {
        QString text;
        QString spelling;
        int score;
    };
    struct IndexEntry {
        quint64 boundaries = 0;
        qsizetype word = 0;
        qsizetype offset = 0;
        qsizetype length = 0;
    };
    struct ChineseIndex {
        QByteArray codes;
        QList<IndexEntry> entries;

        QByteArrayView code(const IndexEntry &entry) const
        {
            return QByteArrayView(codes).sliced(entry.offset, entry.length);
        }
    };
    struct PersonalWord {
        QString locale;
        QString code;
        QString text;
        int hits = 0;
        qint64 usedAt = 0;
        bool custom = false;
    };

    void loadChinese();
    void loadEnglish();
    const ChineseIndex &index(const QString &mode);
    QString simplify(const QString &text) const;
    int englishScore(const QString &word, const QString &language) const;
    QList<KeyboardCandidate> chineseCandidates(const KeyboardQuery &query);
    QList<KeyboardCandidate> englishCandidates(const KeyboardQuery &query);
    QList<KeyboardCandidate> swipeCandidates(const KeyboardQuery &query);
    bool openProfile(const QString &profile);
    QList<PersonalWord> personalWords(const QString &profile, const QString &locale);
    void closeProfile();

    QList<ChineseWord> m_chinese;
    QHash<QString, ChineseIndex> m_indices;
    QHash<QString, QString> m_simplified;
    QHash<quint32, int> m_boundaryScores;
    qsizetype m_maxPhraseLength = 1;
    QStringList m_english;
    QHash<QString, int> m_englishFrequencies;
    QString m_profile;
    QSqlDatabase m_database;
    QHash<QString, QList<PersonalWord>> m_personalCache;
    qint64 m_dataVersion = 0;
    bool m_chineseLoaded = false;
    bool m_englishLoaded = false;
};
