#include "keyboardlexicon.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {
constexpr qsizetype maxCodeLength = 48;
constexpr qsizetype maxResults = 60;
constexpr qsizetype beamWidth = 5;

QString lexiconPath(const QString &name)
{
    const QString external = qEnvironmentVariable("VELORA_LEXICON_DIR");
    if (!external.isEmpty()) {
        const auto path = QDir(external).filePath(name);
        if (QFile::exists(path))
            return path;
    }
    return u":/velora/"_s + name;
}

struct Spelling {
    QString code;
    quint64 boundaries = 0;
};

uint singleton(QStringView text)
{
    if (text.size() == 1)
        return text.front().unicode();
    if (text.size() == 2 && text.front().isHighSurrogate() && text.back().isLowSurrogate())
        return QChar::surrogateToUcs4(text.front(), text.back());
    return 0;
}

QString keypadCode(QString value)
{
    static constexpr QLatin1StringView keys("22233344455566677778889999");
    for (QChar &c : value) {
        const int offset = c.unicode() - 'a';
        if (offset >= 0 && offset < keys.size())
            c = keys[offset];
    }
    return value;
}

QString doublePinyin(const QString &syllable)
{
    if (QStringView(u"aeo").contains(syllable.front())) {
        if (syllable.size() == 1)
            return syllable + syllable;
        if (syllable.size() == 2)
            return syllable;
    }

    static const QHash<QString, QChar> finals = {
        {u"a"_s, 'a'}, {u"o"_s, 'o'}, {u"e"_s, 'e'}, {u"i"_s, 'i'}, {u"u"_s, 'u'},
        {u"v"_s, 'v'}, {u"ai"_s, 'd'}, {u"ei"_s, 'w'}, {u"ao"_s, 'c'}, {u"ou"_s, 'z'},
        {u"an"_s, 'j'}, {u"en"_s, 'f'}, {u"ang"_s, 'h'}, {u"eng"_s, 'g'}, {u"ong"_s, 's'},
        {u"ia"_s, 'x'}, {u"ie"_s, 'p'}, {u"iao"_s, 'n'}, {u"iu"_s, 'q'}, {u"ian"_s, 'm'},
        {u"in"_s, 'b'}, {u"iang"_s, 'l'}, {u"ing"_s, 'k'}, {u"iong"_s, 's'},
        {u"ua"_s, 'x'}, {u"uo"_s, 'o'}, {u"uai"_s, 'k'}, {u"ui"_s, 'v'}, {u"uan"_s, 'r'},
        {u"un"_s, 'y'}, {u"uang"_s, 'l'}, {u"ue"_s, 't'}, {u"ve"_s, 't'},
        {u"van"_s, 'r'}, {u"vn"_s, 'y'}
    };
    const QStringView view(syllable);
    QChar first = syllable.front();
    qsizetype prefix = 1;
    if (view.startsWith(u"zh")) {
        first = u'v';
        prefix = 2;
    } else if (view.startsWith(u"ch")) {
        first = u'i';
        prefix = 2;
    } else if (view.startsWith(u"sh")) {
        first = u'u';
        prefix = 2;
    } else if (QStringView(u"aeo").contains(first)) {
        prefix = 0;
    }
    const QChar last = finals.value(syllable.sliced(prefix));
    return last.isNull() ? QString() : QString(first) + last;
}

QString fuzzySyllable(QString syllable)
{
    if (syllable.startsWith(u"zh") || syllable.startsWith(u"ch") || syllable.startsWith(u"sh"))
        syllable.remove(1, 1);
    if (syllable.front() == u'l')
        syllable[0] = u'n';
    if (syllable.endsWith(u"ang") || syllable.endsWith(u"eng") || syllable.endsWith(u"ing"))
        syllable.chop(1);
    return syllable;
}

Spelling encode(const QString &syllables, const QString &mode)
{
    Spelling result;
    for (const QString &syllable : syllables.split(u' ', Qt::SkipEmptyParts)) {
        if (result.code.size() >= maxCodeLength)
            return {};
        if (!result.code.isEmpty())
            result.boundaries |= quint64(1) << result.code.size();
        if (mode == u"double") {
            const QString pair = doublePinyin(syllable);
            if (pair.isEmpty())
                return {};
            result.code += pair;
        } else {
            result.code += syllable;
        }
        if (result.code.size() > maxCodeLength)
            return {};
    }
    if (mode == u"t9")
        result.code = keypadCode(result.code);
    return result;
}

QString normalizeSpelling(QString code)
{
    code = code.normalized(QString::NormalizationForm_C).toLower();
    code.replace(u'ü', u'v');
    code.replace(u'\'', u' ');
    return code.simplified();
}

bool validSpelling(QStringView code)
{
    return !code.isEmpty() && code.size() <= maxCodeLength
        && std::all_of(code.begin(), code.end(), [](QChar c) {
            return (c >= u'a' && c <= u'z') || c == u' ';
        });
}

bool validLocale(QStringView locale)
{
    return locale == u"zh-CN" || locale == u"zh-TW" || locale == u"en-US" || locale == u"en-GB";
}

bool validPersonal(const QString &locale, const QString &code, const QString &text)
{
    return validLocale(locale) && validSpelling(code) && !text.isEmpty() && text.size() <= 100
        && std::none_of(text.begin(), text.end(), [](QChar c) {
            return c.category() == QChar::Other_Control || c == u'\u2029' || c == u'\u2028';
        });
}

QString shapedEnglish(QString word, const QString &prefix)
{
    if (prefix.size() >= 2 && prefix == prefix.toUpper())
        return word.toUpper();
    if (!prefix.isEmpty() && prefix.front().isUpper())
        word[0] = word[0].toUpper();
    return word;
}

bool betterCandidate(const KeyboardCandidate &left, const KeyboardCandidate &right)
{
    if (left.score != right.score)
        return left.score > right.score;
    if (left.consumed != right.consumed)
        return left.consumed > right.consumed;
    return left.text < right.text;
}

void rank(QList<KeyboardCandidate> &words, qsizetype limit)
{
    std::sort(words.begin(), words.end(), betterCandidate);
    QSet<QPair<QString, qsizetype>> seen;
    qsizetype written = 0;
    for (qsizetype i = 0; i < words.size() && written < limit; ++i) {
        const QPair<QString, qsizetype> key(words[i].text, words[i].consumed);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        if (written != i)
            words[written] = std::move(words[i]);
        ++written;
    }
    words.resize(written);
}

const QHash<QString, int> &englishPrior()
{
    static const auto values = [] {
        const auto words = u"the and have that for you with this from they would there their "
                           "what will about which when make can like time just know take people "
                           "into year your good some could them see other than then now look only "
                           "come its think also back after use how our work first well want way "
                           "new because these give day most hello world please thanks thank "
                           "keyboard testing text input example beautiful quick brown language "
                           "suggestion correction tomorrow today yesterday message information "
                           "write read really welcome everyone morning night lovely"_s.split(u' ');
        QHash<QString, int> scores;
        for (qsizetype i = 0; i < words.size(); ++i)
            scores.insert(words[i], 160 - int(i));
        return scores;
    }();
    return values;
}

const QHash<QString, QString> &britishSpellings()
{
    static const QHash<QString, QString> forms {
        {u"analyze"_s, u"analyse"_s}, {u"analog"_s, u"analogue"_s},
        {u"behavior"_s, u"behaviour"_s}, {u"behaviors"_s, u"behaviours"_s},
        {u"canceled"_s, u"cancelled"_s}, {u"canceling"_s, u"cancelling"_s},
        {u"catalog"_s, u"catalogue"_s}, {u"catalogs"_s, u"catalogues"_s},
        {u"center"_s, u"centre"_s}, {u"centers"_s, u"centres"_s},
        {u"color"_s, u"colour"_s}, {u"colored"_s, u"coloured"_s},
        {u"colorful"_s, u"colourful"_s}, {u"colors"_s, u"colours"_s},
        {u"defense"_s, u"defence"_s}, {u"dialog"_s, u"dialogue"_s},
        {u"favor"_s, u"favour"_s}, {u"favorite"_s, u"favourite"_s},
        {u"favorites"_s, u"favourites"_s}, {u"flavor"_s, u"flavour"_s},
        {u"flavors"_s, u"flavours"_s}, {u"gray"_s, u"grey"_s},
        {u"honor"_s, u"honour"_s}, {u"humor"_s, u"humour"_s},
        {u"labor"_s, u"labour"_s}, {u"labeled"_s, u"labelled"_s},
        {u"labeling"_s, u"labelling"_s}, {u"neighbor"_s, u"neighbour"_s},
        {u"neighbors"_s, u"neighbours"_s}, {u"offense"_s, u"offence"_s},
        {u"theater"_s, u"theatre"_s}, {u"theaters"_s, u"theatres"_s},
        {u"traveled"_s, u"travelled"_s}, {u"traveling"_s, u"travelling"_s},
        {u"traveler"_s, u"traveller"_s}
    };
    return forms;
}

int englishScore(const QString &word, const QString &language)
{
    const auto &prior = englishPrior();
    if (language == u"en-GB") {
        static const auto preferred = [] {
            QHash<QString, int> values;
            const auto &forms = britishSpellings();
            for (auto i = forms.cbegin(); i != forms.cend(); ++i)
                values.insert(i.value(), 80 + englishPrior().value(i.key()));
            return values;
        }();
        return prior.value(word) + preferred.value(word);
    }
    return prior.value(word) + (britishSpellings().contains(word) ? 55 : 0);
}

int scoreFrequency(int frequency)
{
    return int(45 * std::log1p(std::max(0, frequency)));
}

int scorePersonal(int hits, qint64 usedAt, bool custom)
{
    const qint64 age = std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - usedAt);
    return (custom ? 1400 : 0) + 22 * std::min(hits, 12)
        + (hits > 0 ? std::max(0, 95 - int(std::min<qint64>(95, age / 86400 * 3))) : 0);
}

QString compactTrace(QStringView word)
{
    QString trace;
    for (QChar letter : word) {
        if (trace.isEmpty() || trace.back() != letter)
            trace += letter;
    }
    return trace;
}

QPointF keyPosition(QChar letter)
{
    static constexpr QStringView rows[] = {u"qwertyuiop", u"asdfghjkl", u"zxcvbnm"};
    static constexpr qreal offsets[] = {0.0, 0.25, 0.75};
    for (int row = 0; row < 3; ++row) {
        const auto column = rows[row].indexOf(letter);
        if (column >= 0)
            return {column + offsets[row], qreal(row)};
    }
    return {};
}

int traceDistance(QStringView path, QStringView word)
{
    // Extra crossed keys cost less than an omitted deliberate turn.
    std::array<int, maxCodeLength + 1> previous, current;
    for (qsizetype j = 0; j <= word.size(); ++j)
        previous[j] = 85 * j;
    for (qsizetype i = 1; i <= path.size(); ++i) {
        current[0] = 32 * i;
        for (qsizetype j = 1; j <= word.size(); ++j) {
            const QPointF delta = keyPosition(path[i - 1]) - keyPosition(word[j - 1]);
            const int substitution = path[i - 1] == word[j - 1] ? 0
                : (std::hypot(delta.x(), delta.y()) < 1.6 ? 62 : 115);
            current[j] = std::min({previous[j] + 32, current[j - 1] + 85,
                                   previous[j - 1] + substitution});
        }
        previous.swap(current);
    }
    return previous[word.size()];
}
}

KeyboardLexicon::~KeyboardLexicon()
{
    closeProfile();
}

void KeyboardLexicon::warm(const QString &locale, const QString &pinyinMode)
{
    if (locale.startsWith(u"zh")) {
        loadChinese();
        index(pinyinMode);
    } else {
        loadEnglish();
    }
}

void KeyboardLexicon::loadChinese()
{
    if (m_chineseLoaded)
        return;
    m_chineseLoaded = true;

    QHash<QString, int> frequencies;
    QHash<uint, QString> unambiguous;
    QSet<uint> polyphonic;
    QFile dictionary(lexiconPath(u"luna.dict.yaml"_s));
    QList<ChineseWord> words;
    if (dictionary.open(QIODevice::ReadOnly)) {
        bool entries = false;
        while (!dictionary.atEnd()) {
            const QByteArray row = dictionary.readLine().trimmed();
            if (!entries) {
                entries = row == "...";
                continue;
            }
            const QList<QByteArray> fields = row.split('\t');
            if (fields.size() < 2)
                continue;
            const QString spelling = normalizeSpelling(QString::fromUtf8(fields[1]));
            if (!validSpelling(spelling))
                continue;
            const QString text = QString::fromUtf8(fields[0]);
            const uint scalar = singleton(text);
            if (scalar && !polyphonic.contains(scalar)) {
                const auto prior = unambiguous.constFind(scalar);
                if (prior == unambiguous.cend()) {
                    unambiguous.insert(scalar, spelling);
                } else if (prior.value() != spelling) {
                    unambiguous.remove(scalar);
                    polyphonic.insert(scalar);
                }
            }
            words.append({text, spelling, fields.size() > 2 ? fields[2].toInt() : 0});
            frequencies.insert(text, 0);
        }
    }

    QFile priors(lexiconPath(u"essay.txt"_s));
    if (priors.open(QIODevice::ReadOnly)) {
        while (!priors.atEnd()) {
            const QByteArray row = priors.readLine();
            const qsizetype separator = row.indexOf('\t');
            if (separator < 0)
                continue;
            const QString text = QString::fromUtf8(row.constData(), separator);
            const int frequency = row.mid(separator + 1).trimmed().toInt();
            auto entry = frequencies.find(text);
            if (entry != frequencies.end()) {
                entry.value() = frequency;
                continue;
            }
            if (frequency < 1500)
                continue;
            const QList<uint> scalars = text.toUcs4();
            if (scalars.size() < 2 || scalars.size() > 8)
                continue;
            // Essay contributes high-frequency phrases whose readings can be recovered
            // unambiguously from the shipped character dictionary.
            QStringList spelling;
            for (uint scalar : scalars) {
                const auto reading = unambiguous.constFind(scalar);
                if (reading == unambiguous.cend())
                    break;
                spelling.append(reading.value());
            }
            if (spelling.size() == scalars.size())
                words.append({text, spelling.join(u' '), frequency});
        }
    }
    for (ChineseWord &word : words)
        word.score = scoreFrequency(std::max(word.score, frequencies.value(word.text)));

    auto loadConversions = [this](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        while (!file.atEnd()) {
            const QList<QByteArray> fields = file.readLine().trimmed().split('\t');
            if (fields.size() != 2 || fields[0].startsWith('#'))
                continue;
            const QString source = QString::fromUtf8(fields[0]);
            const QString target = QString::fromUtf8(fields[1].split(' ').value(0));
            if (target.isEmpty())
                continue;
            m_simplified.insert(source, target);
            m_maxPhraseLength = std::max(m_maxPhraseLength, source.size());
        }
    };
    loadConversions(lexiconPath(u"TSCharacters.txt"_s));
    loadConversions(lexiconPath(u"TSPhrases.txt"_s));
    m_chinese = std::move(words);
}

void KeyboardLexicon::loadEnglish()
{
    if (m_englishLoaded)
        return;
    m_englishLoaded = true;
    QFile file(lexiconPath(u"english.txt"_s));
    if (!file.open(QIODevice::ReadOnly))
        return;
    while (!file.atEnd()) {
        const QByteArray word = file.readLine().trimmed();
        if (!word.isEmpty())
            m_english.append(QString::fromLatin1(word));
    }
    for (const QString &word : britishSpellings())
        m_english.append(word);
    std::sort(m_english.begin(), m_english.end());
    m_english.erase(std::unique(m_english.begin(), m_english.end()), m_english.end());
}

const QList<KeyboardLexicon::IndexEntry> &KeyboardLexicon::index(const QString &mode)
{
    auto found = m_indices.constFind(mode);
    if (found != m_indices.cend())
        return found.value();

    QList<IndexEntry> entries;
    entries.reserve(m_chinese.size());
    for (qsizetype i = 0; i < m_chinese.size(); ++i) {
        const QString &spelling = m_chinese.at(i).spelling;
        auto encoded = encode(spelling, mode);
        if (!encoded.code.isEmpty())
            entries.append({std::move(encoded.code), encoded.boundaries, i});
    }
    std::sort(entries.begin(), entries.end(), [](const IndexEntry &a, const IndexEntry &b) {
        return a.code < b.code;
    });
    return m_indices.insert(mode, std::move(entries)).value();
}

QString KeyboardLexicon::simplify(const QString &text) const
{
    QString result;
    for (qsizetype i = 0; i < text.size();) {
        const qsizetype fallback = text[i].isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
        bool converted = false;
        for (qsizetype length = std::min(m_maxPhraseLength, text.size() - i);
             length >= fallback; --length) {
            const auto found = m_simplified.constFind(text.mid(i, length));
            if (found != m_simplified.cend()) {
                result += found.value();
                i += length;
                converted = true;
                break;
            }
        }
        if (!converted) {
            result += QStringView(text).sliced(i, fallback);
            i += fallback;
        }
    }
    return result;
}

QList<KeyboardCandidate> KeyboardLexicon::candidates(const KeyboardQuery &query)
{
    if (query.input.isEmpty() || query.input.size() > maxCodeLength)
        return {};
    if (query.swipe)
        return swipeCandidates(query);
    return query.language.startsWith(u"zh") ? chineseCandidates(query) : englishCandidates(query);
}

QList<KeyboardCandidate> KeyboardLexicon::chineseCandidates(const KeyboardQuery &query)
{
    loadChinese();
    QString input;
    quint64 boundaries = 0;
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < query.input.size(); ++i) {
        const QChar c = query.input.at(i).toLower();
        if (c == u'\'') {
            boundaries |= quint64(1) << input.size();
            if (!offsets.isEmpty())
                offsets.last() = i + 1;
            continue;
        }
        input += c;
        offsets.append(i + 1);
    }
    if (input.isEmpty())
        return {};

    const auto &entries = index(query.pinyinMode);
    const bool traditional = query.language == u"zh-TW";
    auto matches = [&](qsizetype start, qsizetype end, bool prefix = false) {
        QList<KeyboardCandidate> words;
        const QString key = input.mid(start, end - start);
        const quint64 endMask = (quint64(1) << (end - start)) - 1;
        const quint64 explicitBoundaries = (boundaries >> start) & endMask & ~quint64(1);
        const QString fuzzyKey = query.fuzzy && query.pinyinMode == u"full" && key.size() <= 7
            ? fuzzySyllable(key) : key;

        auto append = [&](const IndexEntry &entry, int deduction) {
            if ((entry.boundaries & explicitBoundaries) != explicitBoundaries)
                return;
            const ChineseWord &word = m_chinese.at(entry.word);
            words.append({word.text, word.spelling, offsets.at(end - 1),
                          500 + word.score - deduction});
        };
        auto seek = [&](const QString &code, bool allowPrefix, int deduction) {
            auto found = std::lower_bound(entries.cbegin(), entries.cend(), code,
                                          [](const IndexEntry &entry, const QString &value) {
                return entry.code < value;
            });
            for (qsizetype count = 0; found != entries.cend() && count < 1200; ++found, ++count) {
                if (allowPrefix ? !found->code.startsWith(code) : found->code != code)
                    break;
                append(*found, deduction + (allowPrefix ? 9 * int(found->code.size() - code.size()) : 0));
            }
        };
        seek(key, prefix, prefix ? 360 : 0);
        if (!prefix && query.fuzzy && query.pinyinMode == u"full" && key.size() <= 7) {
            const QList<QPair<QString, QString>> initials = {{u"n"_s, u"l"_s}, {u"z"_s, u"zh"_s},
                                                           {u"c"_s, u"ch"_s}, {u"s"_s, u"sh"_s}};
            QStringList options{fuzzyKey};
            for (const auto &[initial, alternate] : initials) {
                if (fuzzyKey.startsWith(initial))
                    options.append(alternate + fuzzyKey.sliced(initial.size()));
            }
            const QStringList expanded = options;
            for (const QString &option : expanded) {
                if (option.endsWith(u"an") || option.endsWith(u"en") || option.endsWith(u"in"))
                    options.append(option + u'g');
            }
            options.removeDuplicates();
            for (const QString &option : options) {
                if (option != key)
                    seek(option, false, 270);
            }
        }
        rank(words, start == 0 && end == input.size() ? maxResults : 12);
        return words;
    };

    const qsizetype size = input.size();
    QList<QList<KeyboardCandidate>> paths(size + 1);
    paths[size].append({{}, {}, query.input.size(), 0});
    QList<KeyboardCandidate> result;
    for (qsizetype start = size - 1; start >= 0; --start) {
        for (qsizetype end = start + 1; end <= std::min(size, start + 24); ++end) {
            QList<KeyboardCandidate> words = matches(start, end);
            if (start == 0) {
                for (auto one : words) {
                    if (end < size)
                        one.score -= 420 + int(size - end) * 8;
                    result.append(std::move(one));
                }
            }
            for (qsizetype n = 0; n < std::min<qsizetype>(words.size(), beamWidth); ++n) {
                const auto &head = words.at(n);
                for (const auto &tail : paths.at(end)) {
                    const int score = tail.text.isEmpty() ? head.score
                        : head.score / 2 + tail.score / 2 - 48;
                    paths[start].append({head.text + tail.text,
                                         tail.code.isEmpty() ? head.code : head.code + u' ' + tail.code,
                                         tail.consumed, score});
                }
            }
        }
        rank(paths[start], beamWidth);
    }
    for (const auto &candidate : paths.first())
        result.append(candidate);

    if (size <= 4 && !query.input.endsWith(u'\''))
        result.append(matches(0, size, true));

    auto matchesSupplement = [&](const QString &code) {
        const Spelling value = encode(code, query.pinyinMode);
        if (value.code == input)
            return true;
        if (query.pinyinMode != u"double")
            return false;
        // Publishers may supply compact pinyin instead of a syllable-separated code.
        const auto known = std::lower_bound(entries.cbegin(), entries.cend(), input,
                                            [](const IndexEntry &entry, const QString &value) {
            return entry.code < value;
        });
        const QString compact = QString(code).remove(u' ');
        for (auto i = known; i != entries.cend() && i->code == input; ++i) {
            if (QString(m_chinese.at(i->word).spelling).remove(u' ') == compact)
                return true;
        }
        return false;
    };

    for (auto &candidate : result) {
        if (!traditional)
            candidate.text = simplify(candidate.text);
    }
    for (const auto &entry : query.supplemental) {
        if ((!entry.locale.isEmpty() && entry.locale != query.language)
            || (entry.expiresAt.isValid() && entry.expiresAt <= QDateTime::currentDateTimeUtc())
            || !matchesSupplement(entry.code))
            continue;
        const int boost = std::clamp(entry.weight, 1, 100);
        result.append({entry.text, entry.code, query.input.size(), 780 + boost});
    }
    for (const auto &entry : personalWords(query.profile, query.language)) {
        if ((!query.useLearning && !entry.custom) || !matchesSupplement(entry.code))
            continue;
        const int boost = scorePersonal(query.useLearning ? entry.hits : 0, entry.usedAt, entry.custom);
        int base = 720;
        for (const auto &candidate : result) {
            if (candidate.text == entry.text && candidate.consumed == query.input.size())
                base = std::max(base, candidate.score);
        }
        result.append({entry.text, entry.code, query.input.size(), base + boost});
    }
    rank(result, maxResults);
    return result;
}

QList<KeyboardCandidate> KeyboardLexicon::englishCandidates(const KeyboardQuery &query)
{
    loadEnglish();
    const QString prefix = query.input.toLower();
    if (prefix.size() < 2 || !validSpelling(prefix))
        return {};

    QList<KeyboardCandidate> result;
    auto add = [&result, &query](const QString &word, int score, bool correction = false) {
        const QString display = shapedEnglish(word, query.input);
        if (display != query.input)
            result.append({display, query.input.toLower(), query.input.size(), score, correction});
    };
    const auto first = std::lower_bound(m_english.cbegin(), m_english.cend(), prefix);
    for (auto i = first; i != m_english.cend() && i->startsWith(prefix); ++i)
        add(*i, 570 + englishScore(*i, query.language) - int(i->size() - prefix.size()) * 13);

    if (query.language == u"en-GB") {
        const QString preferred = britishSpellings().value(prefix);
        if (!preferred.isEmpty())
            add(preferred, 750, true);
    }

    if (prefix.size() >= 3 && prefix.size() <= 24 && !std::binary_search(m_english.cbegin(),
                                                                       m_english.cend(), prefix)) {
        QSet<QString> alterations;
        for (qsizetype i = 0; i <= prefix.size(); ++i) {
            if (i < prefix.size())
                alterations.insert(QString(prefix).remove(i, 1));
            if (i + 1 < prefix.size()) {
                QString swapped = prefix;
                const QChar first = swapped.at(i);
                swapped[i] = swapped.at(i + 1);
                swapped[i + 1] = first;
                alterations.insert(swapped);
            }
            for (QChar letter = u'a'; letter <= u'z'; letter = QChar(letter.unicode() + 1)) {
                alterations.insert(QString(prefix).insert(i, letter));
                if (i < prefix.size())
                    alterations.insert(QString(prefix).replace(i, 1, letter));
            }
        }
        for (const QString &word : alterations) {
            if (std::binary_search(m_english.cbegin(), m_english.cend(), word))
                add(word, 500 + englishScore(word, query.language), true);
        }
    }

    for (const auto &entry : query.supplemental) {
        if (entry.locale == query.language && entry.code.startsWith(prefix)
            && (!entry.expiresAt.isValid() || entry.expiresAt > QDateTime::currentDateTimeUtc()))
            add(entry.text, 570 + std::clamp(entry.weight, 1, 100));
    }
    for (const auto &entry : personalWords(query.profile, query.language)) {
        if ((query.useLearning || entry.custom) && entry.code.startsWith(prefix))
            add(entry.text, 650 + scorePersonal(query.useLearning ? entry.hits : 0,
                                               entry.usedAt, entry.custom));
    }
    rank(result, maxResults);
    return result;
}

QList<KeyboardCandidate> KeyboardLexicon::swipeCandidates(const KeyboardQuery &query)
{
    loadEnglish();
    const QString path = compactTrace(query.input.toLower());
    if (path.size() < 3 || !validSpelling(path))
        return {};
    QList<KeyboardCandidate> result;
    auto add = [&](const QString &word, int boost) {
        if (word.size() < 3 || word.size() > maxCodeLength
            || word.front() != path.front() || word.back() != path.back())
            return;
        const QString trace = compactTrace(word);
        if (trace.size() > path.size() + 3 || trace.size() * 2 + 2 < path.size())
            return;
        const int distance = traceDistance(path, trace);
        if (distance <= 85 + 15 * path.size())
            result.append({shapedEnglish(word, query.input), word, query.input.size(),
                           1200 + boost + englishScore(word, query.language) - 4 * distance});
    };
    const QString start(path.front());
    const auto first = std::lower_bound(m_english.cbegin(), m_english.cend(), start);
    for (auto i = first; i != m_english.cend() && i->startsWith(start); ++i)
        add(*i, 0);
    for (const auto &entry : query.supplemental) {
        if (entry.locale == query.language && validSpelling(entry.text)
            && (!entry.expiresAt.isValid() || entry.expiresAt > QDateTime::currentDateTimeUtc()))
            add(entry.text.toLower(), std::clamp(entry.weight, 1, 100));
    }
    for (const auto &entry : personalWords(query.profile, query.language)) {
        if ((query.useLearning || entry.custom) && validSpelling(entry.text))
            add(entry.text.toLower(), std::min(180, scorePersonal(query.useLearning ? entry.hits : 0,
                                                                entry.usedAt, entry.custom)));
    }
    rank(result, 12);
    return result;
}

void KeyboardLexicon::closeProfile()
{
    m_personalCache.clear();
    m_dataVersion = 0;
    m_profile.clear();
    if (m_database.isValid()) {
        const QString connection = m_database.connectionName();
        m_database.close();
        m_database = {};
        QSqlDatabase::removeDatabase(connection);
    }
}

bool KeyboardLexicon::openProfile(const QString &profile)
{
    if (m_profile == profile && m_database.isOpen())
        return true;
    closeProfile();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + u"/VeloraIME"_s;
    const QString name = QString::fromLatin1(QCryptographicHash::hash(profile.toUtf8(),
                                                          QCryptographicHash::Sha256).toHex());
    if (!QDir().mkpath(dir))
        return false;
    const QString path = QDir(dir).filePath(name + u".sqlite3"_s);
    QFile file(path);
    if (!file.exists()) {
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && !file.exists())
            return false;
        file.close();
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    m_database = QSqlDatabase::addDatabase(u"QSQLITE"_s, QUuid::createUuid().toString());
    m_database.setDatabaseName(path);
    m_database.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=2500"_s);
    if (!m_database.open()) {
        closeProfile();
        return false;
    }
    bool initialized = false;
    {
        QSqlQuery query(m_database);
        initialized = query.exec(u"PRAGMA secure_delete = ON"_s)
            && query.exec(u"CREATE TABLE IF NOT EXISTS personal ("
                         "locale TEXT NOT NULL, code TEXT NOT NULL, text TEXT NOT NULL, "
                         "hits INTEGER NOT NULL DEFAULT 0, used_at INTEGER NOT NULL DEFAULT 0, "
                         "custom INTEGER NOT NULL DEFAULT 0, "
                         "PRIMARY KEY(locale, code, text))"_s)
            && query.exec(u"CREATE TABLE IF NOT EXISTS recent_emoji ("
                         "text TEXT PRIMARY KEY, used_at INTEGER NOT NULL)"_s);
    }
    if (!initialized) {
        closeProfile();
        return false;
    }
    m_profile = profile;
    return true;
}

QList<KeyboardLexicon::PersonalWord> KeyboardLexicon::personalWords(const QString &profile,
                                                                   const QString &locale)
{
    if (!openProfile(profile))
        return {};
    QSqlQuery version(m_database);
    if (version.exec(u"PRAGMA data_version"_s) && version.next()) {
        const auto current = version.value(0).toLongLong();
        if (m_dataVersion != current) {
            m_personalCache.clear();
            m_dataVersion = current;
        }
    }
    const auto cached = m_personalCache.constFind(locale);
    if (cached != m_personalCache.cend())
        return cached.value();

    QSqlQuery query(m_database);
    query.prepare(u"SELECT code, text, hits, used_at, custom FROM personal WHERE locale = ?"_s);
    query.addBindValue(locale);
    QList<PersonalWord> words;
    if (query.exec()) {
        while (query.next())
            words.append({locale, query.value(0).toString(), query.value(1).toString(),
                          query.value(2).toInt(), query.value(3).toLongLong(), query.value(4).toBool()});
        m_personalCache.insert(locale, words);
    }
    return words;
}

bool KeyboardLexicon::remember(const QString &profile, const QString &locale, const QString &code,
                               const QString &text)
{
    if (!validPersonal(locale, code, text) || !openProfile(profile))
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"INSERT INTO personal(locale, code, text, hits, used_at) VALUES(?,?,?,1,?) "
                  "ON CONFLICT(locale, code, text) DO UPDATE SET "
                  "hits = min(personal.hits + 1, 10000), used_at = excluded.used_at"_s);
    query.addBindValue(locale);
    query.addBindValue(code);
    query.addBindValue(text);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    const bool saved = query.exec();
    if (saved)
        m_personalCache.remove(locale);
    return saved;
}

bool KeyboardLexicon::forget(const QString &profile, const QString &locale, const QString &code,
                             const QString &text)
{
    if (!validPersonal(locale, code, text) || !openProfile(profile))
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"UPDATE personal SET hits = max(hits - 1, 0) "
                  "WHERE locale = ? AND code = ? AND text = ?"_s);
    query.addBindValue(locale);
    query.addBindValue(code);
    query.addBindValue(text);
    const bool saved = query.exec()
        && query.exec(u"DELETE FROM personal WHERE hits = 0 AND custom = 0"_s);
    m_personalCache.remove(locale);
    return saved;
}

bool KeyboardLexicon::setPhrase(const QString &profile, const QString &locale, const QString &code,
                                const QString &text)
{
    const QString normalized = normalizeSpelling(code);
    if (!validPersonal(locale, normalized, text) || !openProfile(profile))
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"INSERT INTO personal(locale, code, text, custom) VALUES(?,?,?,1) "
                  "ON CONFLICT(locale, code, text) DO UPDATE SET custom = 1"_s);
    query.addBindValue(locale);
    query.addBindValue(normalized);
    query.addBindValue(text);
    const bool saved = query.exec();
    if (saved)
        m_personalCache.remove(locale);
    return saved;
}

bool KeyboardLexicon::removePhrase(const QString &profile, const QString &locale, const QString &code,
                                   const QString &text)
{
    const QString normalized = normalizeSpelling(code);
    if (!validPersonal(locale, normalized, text) || !openProfile(profile))
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"DELETE FROM personal WHERE locale = ? AND code = ? AND text = ?"_s);
    query.addBindValue(locale);
    query.addBindValue(normalized);
    query.addBindValue(text);
    const bool saved = query.exec();
    if (saved)
        m_personalCache.remove(locale);
    return saved;
}

bool KeyboardLexicon::clearLearning(const QString &profile)
{
    if (!openProfile(profile) || !m_database.transaction())
        return false;
    QSqlQuery query(m_database);
    if (query.exec(u"DELETE FROM personal WHERE custom = 0"_s)
        && query.exec(u"UPDATE personal SET hits = 0, used_at = 0 WHERE custom = 1"_s)
        && m_database.commit()) {
        m_personalCache.clear();
        return true;
    }
    m_database.rollback();
    return false;
}

bool KeyboardLexicon::exportProfile(const QString &profile, const QString &path)
{
    if (!openProfile(profile) || !m_database.transaction())
        return false;
    QJsonArray entries;
    for (const QString &locale : {u"zh-CN"_s, u"zh-TW"_s, u"en-US"_s, u"en-GB"_s}) {
        for (const auto &entry : personalWords(profile, locale))
            entries.append(QJsonObject{{u"locale"_s, locale}, {u"code"_s, entry.code},
                {u"text"_s, entry.text}, {u"hits"_s, entry.hits}, {u"usedAt"_s, entry.usedAt},
                {u"custom"_s, entry.custom}});
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    const QByteArray payload = QJsonDocument(QJsonObject{{u"version"_s, 1},
                                                         {u"entries"_s, entries}}).toJson();
    return file.write(payload) == payload.size() && file.commit();
}

bool KeyboardLexicon::importProfile(const QString &profile, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(u"version"_s).toInt() != 1 || !root.value(u"entries"_s).isArray()
        || !openProfile(profile) || !m_database.transaction())
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"INSERT INTO personal(locale, code, text, hits, used_at, custom) VALUES(?,?,?,?,?,?) "
                  "ON CONFLICT(locale, code, text) DO UPDATE SET "
                  "hits = max(personal.hits, excluded.hits), "
                  "used_at = max(personal.used_at, excluded.used_at), "
                  "custom = max(personal.custom, excluded.custom)"_s);
    const QJsonArray entries = root.value(u"entries"_s).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject item = value.toObject();
        const QString locale = item.value(u"locale"_s).toString();
        const QString code = item.value(u"code"_s).toString();
        const QString text = item.value(u"text"_s).toString();
        const qint64 usedAt = item.value(u"usedAt"_s).toInteger();
        const int hits = item.value(u"hits"_s).toInt();
        const bool custom = item.value(u"custom"_s).toBool();
        if (!validPersonal(locale, code, text) || usedAt < 0 || hits < 0 || hits > 10000
            || (!custom && hits == 0)) {
            m_database.rollback();
            return false;
        }
        query.bindValue(0, locale);
        query.bindValue(1, code);
        query.bindValue(2, text);
        query.bindValue(3, hits);
        query.bindValue(4, std::min(usedAt, QDateTime::currentSecsSinceEpoch()));
        query.bindValue(5, custom);
        if (!query.exec()) {
            m_database.rollback();
            return false;
        }
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return false;
    }
    m_personalCache.clear();
    return true;
}

QStringList KeyboardLexicon::recentEmoji(const QString &profile)
{
    QStringList values;
    if (!openProfile(profile))
        return values;
    QSqlQuery query(m_database);
    if (query.exec(u"SELECT text FROM recent_emoji ORDER BY used_at DESC LIMIT 20"_s)) {
        while (query.next())
            values.append(query.value(0).toString());
    }
    return values;
}

bool KeyboardLexicon::rememberEmoji(const QString &profile, const QString &emoji)
{
    if (!openProfile(profile) || !m_database.transaction())
        return false;
    QSqlQuery query(m_database);
    query.prepare(u"INSERT INTO recent_emoji(text, used_at) VALUES(?,?) "
                  "ON CONFLICT(text) DO UPDATE SET used_at = excluded.used_at"_s);
    query.addBindValue(emoji);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (query.exec()
        && query.exec(u"DELETE FROM recent_emoji WHERE text NOT IN ("
                      "SELECT text FROM recent_emoji ORDER BY used_at DESC LIMIT 20)"_s)
        && m_database.commit())
        return true;
    m_database.rollback();
    return false;
}

QVariantList KeyboardLexicon::userPhrases(const QString &profile, const QString &locale)
{
    auto words = personalWords(profile, locale);
    std::sort(words.begin(), words.end(), [](const PersonalWord &a, const PersonalWord &b) {
        return std::tie(a.code, a.text) < std::tie(b.code, b.text);
    });
    QVariantList values;
    for (const auto &word : words) {
        if (word.custom)
            values.append(QVariantMap{{u"locale"_s, word.locale}, {u"code"_s, word.code},
                                      {u"text"_s, word.text}, {u"hits"_s, word.hits}});
    }
    return values;
}
