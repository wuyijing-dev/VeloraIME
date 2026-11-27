#include "keyboardcontroller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QTextBoundaryFinder>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

bool asciiLetter(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

bool asciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool isPinyinInput(QStringView text, bool nineKey, bool composing)
{
    if (text.isEmpty() || (!composing && text.front() == u'\''))
        return false;
    return std::all_of(text.begin(), text.end(), [nineKey](QChar c) {
        return nineKey ? (c >= u'2' && c <= u'9') : asciiLetter(c) || c == u'\'' || c == u'ü';
    });
}

}

KeyboardController::KeyboardController(QObject *parent)
    : QObject(parent), m_lexicon(new KeyboardLexicon)
{
    m_workerThread.setObjectName(u"VeloraIME lexicon"_s);
    m_lexicon->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_lexicon, &QObject::deleteLater);
    m_workerThread.start();
    connect(qGuiApp, &QGuiApplication::focusObjectChanged, this, &KeyboardController::trackFocus);
    m_clipboardTimer.setInterval(60 * 1000);
    connect(&m_clipboardTimer, &QTimer::timeout, this, &KeyboardController::pruneClipboard);
    trackFocus();
}

KeyboardController::~KeyboardController()
{
    clearComposition(true);
    QMetaObject::invokeMethod(m_lexicon, [] {}, Qt::BlockingQueuedConnection);
    m_workerThread.quit();
    m_workerThread.wait();
}

QString KeyboardController::enterLabel() const
{
    const bool chinese = m_language.startsWith(u"zh");
    switch (m_enter) {
    case Qt::EnterKeyDone: return chinese ? u"完成"_s : u"Done"_s;
    case Qt::EnterKeyGo: return chinese ? u"前往"_s : u"Go"_s;
    case Qt::EnterKeySend: return chinese ? u"发送"_s : u"Send"_s;
    case Qt::EnterKeySearch: return chinese ? u"搜索"_s : u"Search"_s;
    case Qt::EnterKeyNext: return chinese ? u"下一项"_s : u"Next"_s;
    case Qt::EnterKeyPrevious: return chinese ? u"上一项"_s : u"Previous"_s;
    case Qt::EnterKeyDefault:
    case Qt::EnterKeyReturn:
        return m_hints.testFlag(Qt::ImhMultiLine)
            ? (chinese ? u"换行"_s : u"Return"_s)
            : (chinese ? u"回车"_s : u"Enter"_s);
    }
    return u"Enter"_s;
}

QString KeyboardController::preedit() const
{
    if (!m_swipePath.isEmpty())
        return m_swipeDisplay.isEmpty() ? m_swipePath : m_swipeDisplay;
    QString value;
    for (const auto &segment : m_segments)
        value += segment.text;
    return value + m_raw;
}

QQuickWindow *KeyboardController::window() const
{
    return m_window;
}

QStringList KeyboardController::clipboardHistory() const
{
    QStringList values;
    if (m_sensitive || !m_clipboardHistoryEnabled)
        return values;
    for (const auto &item : m_clipboard)
        values.append(item.first);
    return values;
}

QStringList KeyboardController::recentEmoji() const
{
    return m_sensitive ? QStringList() : m_recentEmoji;
}

QVariantList KeyboardController::userPhrases() const
{
    return m_sensitive ? QVariantList() : m_userPhrases;
}

void KeyboardController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    advanceSession();
    m_enabled = enabled;
    if (!enabled) {
        clearComposition(true);
        clearUndo();
    }
    refreshField();
    emit enabledChanged();
}

void KeyboardController::setPrivateMode(bool enabled)
{
    if (m_privateMode == enabled)
        return;
    advanceSession();
    m_privateMode = enabled;
    clearComposition(true);
    clearUndo();
    refreshField();
    emit privateModeChanged();
}

void KeyboardController::setLearningEnabled(bool enabled)
{
    if (m_learningEnabled == enabled)
        return;
    m_learningEnabled = enabled;
    if (!m_raw.isEmpty())
        requestCandidates(m_raw);
    else if (!m_englishWord.isEmpty())
        requestCandidates(m_englishWord);
    emit learningEnabledChanged();
}

void KeyboardController::setFuzzyPinyin(bool enabled)
{
    if (m_fuzzyPinyin == enabled)
        return;
    m_fuzzyPinyin = enabled;
    if (!m_raw.isEmpty())
        requestCandidates(m_raw);
    emit fuzzyPinyinChanged();
}

void KeyboardController::setLanguage(const QString &language)
{
    if ((language != u"en-US" && language != u"en-GB" && language != u"zh-CN" && language != u"zh-TW")
        || m_language == language)
        return;
    advanceSession();
    clearComposition(true);
    clearUndo();
    m_language = language;
    clearUserPhraseView();
    warmLexicon();
    emit languageChanged();
    emit fieldChanged();
}

void KeyboardController::setPinyinMode(const QString &mode)
{
    if ((mode != u"full" && mode != u"double" && mode != u"t9") || m_pinyinMode == mode)
        return;
    advanceSession();
    clearComposition(true);
    clearUndo();
    m_pinyinMode = mode;
    warmLexicon();
    emit pinyinModeChanged();
}

void KeyboardController::setProfileId(const QString &profile)
{
    const QString value = profile.isEmpty() ? u"default"_s : profile.left(200);
    if (m_profile == value)
        return;
    advanceSession();
    clearComposition(true);
    clearUndo();
    clearClipboardHistory();
    m_profile = value;
    clearUserPhraseView();
    m_recentEmoji.clear();
    emit recentEmojiChanged();
    if (!m_sensitive)
        requestRecentEmoji();
    emit profileIdChanged();
}

void KeyboardController::setSupplementalEntries(const QVariantList &entries)
{
    if (m_supplementalEntries == entries)
        return;
    m_supplementalEntries = entries;
    m_words.clear();
    m_words.reserve(entries.size());
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        QString code = entry.value(u"code"_s, entry.value(u"pinyin"_s)).toString().toLower();
        code.replace(u'\'', u' ');
        code.replace(u'ü', u'v');
        code = code.simplified();
        const QString text = entry.value(u"text"_s).toString().normalized(QString::NormalizationForm_C);
        if (text.isEmpty() || text.size() > 100 || code.size() > 48
            || !isPinyinInput(QString(code).remove(u' '), false, false))
            continue;
        m_words.append({text, code, entry.value(u"weight"_s).toInt(),
                        entry.value(u"expiresAt"_s).toDateTime(),
                        entry.value(u"locale"_s).toString()});
    }
    emit supplementalEntriesChanged();
}

void KeyboardController::setClipboardHistoryEnabled(bool enabled)
{
    if (m_clipboardHistoryEnabled == enabled)
        return;
    m_clipboardHistoryEnabled = enabled;
    if (enabled)
        m_clipboardTimer.start();
    else {
        m_clipboardTimer.stop();
        clearClipboardHistory();
    }
    emit clipboardHistoryEnabledChanged();
}

void KeyboardController::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    advanceSession();
    clearComposition(true);
    clearUndo();
    if (m_window)
        disconnect(m_window, &QObject::destroyed, this, nullptr);
    m_window = window;
    if (m_window) {
        connect(m_window, &QObject::destroyed, this, [this] {
            setFocusObject(nullptr);
            emit windowChanged();
        });
    }
    trackFocus();
    emit windowChanged();
}

void KeyboardController::setInputTarget(KeyboardInputTarget *target)
{
    if (m_inputTarget == target)
        return;
    advanceSession();
    clearComposition(true);
    clearUndo();
    if (m_inputTarget)
        disconnect(m_inputTarget, nullptr, this, nullptr);
    setFocusObject(nullptr);
    m_inputTarget = target;
    if (target) {
        connect(target, &KeyboardInputTarget::stateChanged, this, &KeyboardController::refreshField);
        connect(target, &KeyboardInputTarget::sessionChanged, this, [this] {
            advanceSession();
            clearComposition(false);
            clearUndo();
            refreshField();
        });
        connect(target, &QObject::destroyed, this, [this] {
            advanceSession();
            clearComposition(false);
            clearUndo();
            trackFocus();
            emit inputTargetChanged();
        });
    }
    trackFocus();
    emit inputTargetChanged();
}

void KeyboardController::trackFocus()
{
    QObject *focus = m_inputTarget ? nullptr : QGuiApplication::focusObject();
    if (m_window && focus) {
        const QQuickItem *item = qobject_cast<QQuickItem *>(focus);
        if (!item || item->window() != m_window)
            focus = nullptr;
    }
    setFocusObject(focus);
}

void KeyboardController::advanceSession()
{
    ++m_session;
    const quint64 value = m_session;
    QMetaObject::invokeMethod(this, [this, value] {
        if (m_session == value)
            emit sessionTokenChanged();
    }, Qt::QueuedConnection);
}

void KeyboardController::warmLexicon()
{
    QMetaObject::invokeMethod(m_lexicon, [engine = m_lexicon, locale = m_language, mode = m_pinyinMode] {
        engine->warm(locale, mode);
    }, Qt::QueuedConnection);
}

void KeyboardController::setFocusObject(QObject *focus)
{
    if (m_focus == focus) {
        refreshField();
        return;
    }
    advanceSession();
    clearComposition(true);
    clearUndo();
    if (m_focus) {
        m_focus->removeEventFilter(this);
        disconnect(m_focus, nullptr, this, nullptr);
    }
    m_focus = focus;
    if (focus) {
        focus->installEventFilter(this);
        connect(focus, &QObject::destroyed, this, [this] {
            advanceSession();
            clearComposition(false);
            clearUndo();
            refreshField();
        });
        const int refreshSlot = metaObject()->indexOfSlot("refreshField()");
        for (const char *signature : {"inputMethodHintsChanged()", "readOnlyChanged(bool)",
                                      "readOnlyChanged()", "echoModeChanged(int)",
                                      "echoModeChanged()", "enabledChanged()"}) {
            const int signal = focus->metaObject()->indexOfSignal(signature);
            if (signal >= 0)
                QObject::connect(focus, focus->metaObject()->method(signal),
                                 this, metaObject()->method(refreshSlot));
        }
        const int editingSlot = metaObject()->indexOfSlot("cancelAfterExternalEdit()");
        for (const char *signature : {"textChanged()", "cursorPositionChanged()"}) {
            const int signal = focus->metaObject()->indexOfSignal(signature);
            if (signal >= 0)
                QObject::connect(focus, focus->metaObject()->method(signal),
                                 this, metaObject()->method(editingSlot));
        }
    }
    refreshField();
}

void KeyboardController::cancelAfterExternalEdit()
{
    if (!m_sending) {
        clearComposition(true);
        clearUndo();
    }
}

QVariant KeyboardController::query(Qt::InputMethodQuery property) const
{
    if (m_inputTarget)
        return m_inputTarget->query(property);
    if (!m_focus)
        return {};
    QInputMethodQueryEvent event(property);
    QCoreApplication::sendEvent(m_focus, &event);
    return event.value(property);
}

void KeyboardController::refreshField()
{
    const bool editAvailable = m_inputTarget ? m_inputTarget->active()
        : m_focus && query(Qt::ImEnabled).toBool() && !query(Qt::ImReadOnly).toBool();
    const bool active = m_enabled && editAvailable;
    const auto hints = Qt::InputMethodHints::fromInt(query(Qt::ImHints).toInt());
    const bool privacy = m_privateMode || hints.testAnyFlags(Qt::ImhHiddenText | Qt::ImhSensitiveData);
    QString mode = u"text"_s;
    if (hints.testFlag(Qt::ImhDigitsOnly))
        mode = u"number"_s;
    else if (hints.testAnyFlags(Qt::ImhFormattedNumbersOnly | Qt::ImhDecimalNumbersOnly))
        mode = u"decimal"_s;
    else if (hints.testFlag(Qt::ImhDialableCharactersOnly))
        mode = u"phone"_s;
    else if (hints.testFlag(Qt::ImhEmailCharactersOnly))
        mode = u"email"_s;
    else if (hints.testFlag(Qt::ImhUrlCharactersOnly))
        mode = u"url"_s;
    else if (hints.testAnyFlags(Qt::ImhPreferNumbers | Qt::ImhDate | Qt::ImhTime))
        mode = u"number"_s;
    const auto enter = active ? Qt::EnterKeyType(query(Qt::ImEnterKeyType).toInt())
                              : Qt::EnterKeyDefault;
    if (m_active == active && m_sensitive == privacy && m_mode == mode
        && m_enter == enter && m_hints == hints)
        return;

    if (m_active != active || m_sensitive != privacy || m_mode != mode || m_hints != hints)
        advanceSession();
    const bool privacyChanged = m_sensitive != privacy;
    m_active = active;
    m_sensitive = privacy;
    m_mode = mode;
    m_hints = hints;
    m_enter = enter;
    if (!canCompose()) {
        clearComposition(true);
        clearUndo();
    }
    if (privacyChanged) {
        emit clipboardHistoryChanged();
        emit recentEmojiChanged();
        emit userPhrasesChanged();
        if (!privacy && m_userPhrasesRequested)
            refreshUserPhrases();
    }
    if (active && !privacy && m_recentEmoji.isEmpty())
        requestRecentEmoji();
    if (active && !privacy && m_mode == u"text")
        warmLexicon();
    emit fieldChanged();
}

bool KeyboardController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_sending || watched != m_focus)
        return false;
    switch (event->type()) {
    case QEvent::FocusOut:
    case QEvent::InputMethod:
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
        clearComposition(true);
        clearUndo();
        break;
    case QEvent::KeyPress: {
        auto key = static_cast<QKeyEvent *>(event);
        if (!m_raw.isEmpty() || !m_segments.isEmpty() || !m_swipePath.isEmpty()) {
            if (key->key() == Qt::Key_Escape) {
                cancelComposition();
                return true;
            }
            if (!key->modifiers().testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
                if (key->key() == Qt::Key_Backspace) {
                    backspace();
                    return true;
                }
                if (key->key() == Qt::Key_Space) {
                    space();
                    return true;
                }
                if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                    enter();
                    return true;
                }
                if (isPinyinInput(key->text(), m_pinyinMode == u"t9", true)) {
                    input(key->text());
                    return true;
                }
            }
        }
        clearComposition(true);
        clearUndo();
        break;
    }
    default:
        break;
    }
    return false;
}

bool KeyboardController::canCompose() const
{
    return m_active && !m_sensitive && !m_hints.testFlag(Qt::ImhNoPredictiveText)
        && m_mode == u"text" && !m_hints.testFlag(Qt::ImhLatinOnly);
}

QString KeyboardController::restrictText(const QString &text) const
{
    QString value = text;
    if (m_hints.testFlag(Qt::ImhUppercaseOnly))
        value = value.toUpper();
    if (m_hints.testFlag(Qt::ImhLowercaseOnly))
        value = value.toLower();
    auto allowed = [this](QChar c) {
        if (m_hints.testFlag(Qt::ImhDigitsOnly))
            return asciiDigit(c);
        if (m_hints.testFlag(Qt::ImhDecimalNumbersOnly))
            return asciiDigit(c) || QStringView(u".,-+").contains(c);
        if (m_hints.testFlag(Qt::ImhFormattedNumbersOnly))
            return asciiDigit(c) || QStringView(u".,-+eE ").contains(c);
        if (m_hints.testFlag(Qt::ImhDialableCharactersOnly))
            return asciiDigit(c) || QStringView(u"+#*()- ").contains(c);
        if (m_hints.testFlag(Qt::ImhEmailCharactersOnly))
            return asciiLetter(c) || asciiDigit(c) || QStringView(u"@._+-").contains(c);
        if (m_hints.testFlag(Qt::ImhUrlCharactersOnly))
            return c >= u'!' && c <= u'~';
        if (m_hints.testFlag(Qt::ImhLatinOnly))
            return c.unicode() < 0x80 || c.script() == QChar::Script_Latin;
        return true;
    };
    value.removeIf([&](QChar c) { return !allowed(c); });
    return value;
}

void KeyboardController::input(const QString &text)
{
    refreshField();
    if (!m_active || text.isEmpty())
        return;
    const auto session = m_session;
    clearUndo();
    if (session != m_session)
        return;

    const bool composing = !m_raw.isEmpty() || !m_segments.isEmpty();
    if (canCompose() && m_language.startsWith(u"zh")
        && isPinyinInput(text, m_pinyinMode == u"t9", composing)) {
        if (!composing) {
            m_compositionWords = m_words;
            // Some Qt editors remove the selection as soon as preedit starts.
            // Keep preedit in the panel until the selected text is replaced.
            m_inlinePreedit = query(Qt::ImCurrentSelection).toString().isEmpty();
        }
        QString spelling = text.toLower();
        spelling.replace(u'ü', u'v');
        if (m_raw.endsWith(u'\'') && spelling == u"'")
            return;
        if (m_raw.size() + spelling.size() > 48)
            return;
        m_raw += spelling;
        refreshComposition();
        return;
    }

    inputDirect(text);
}

void KeyboardController::inputDirect(const QString &text)
{
    refreshField();
    if (!m_active || text.isEmpty())
        return;
    const quint64 session = m_session;
    clearUndo();
    if (session != m_session)
        return;
    if (!m_swipePath.isEmpty()) {
        const auto *top = m_results.isEmpty() ? nullptr : &m_results.first();
        commitSwipe(top, asciiLetter(text.front()), false);
    } else if (!m_raw.isEmpty() || !m_segments.isEmpty()) {
        if (const auto *top = bestCompleteChinese())
            commitChinese(top->text, top->code, false);
        else
            commitChinese(m_raw, {}, false);
    }
    if (session == m_session && sendCommit(restrictText(text)) && session == m_session)
        refreshEnglish();
}

void KeyboardController::inputSwipe(const QString &path)
{
    refreshField();
    if (!canCompose() || !m_language.startsWith(u"en-") || path.size() > 48
        || !std::all_of(path.begin(), path.end(), asciiLetter))
        return;
    QString compact;
    for (QChar letter : path) {
        if (compact.isEmpty() || compact.back().toLower() != letter.toLower())
            compact += letter;
    }
    if (compact.size() < 3)
        return;

    const auto session = m_session;
    clearUndo();
    if (session != m_session)
        return;
    if (!m_swipePath.isEmpty()) {
        const auto *top = m_results.isEmpty() ? nullptr : &m_results.first();
        commitSwipe(top, true, false);
    }
    if (session != m_session)
        return;

    m_swipePath = compact;
    m_swipeDisplay.clear();
    m_compositionWords = m_words;
    m_inlinePreedit = query(Qt::ImCurrentSelection).toString().isEmpty();
    refreshComposition();
}

bool KeyboardController::inputLiteral(const QString &text, const QString &token)
{
    refreshField();
    if (!m_active || m_sensitive || token != sessionToken() || text.isEmpty())
        return false;
    const auto session = m_session;
    clearComposition(true);
    clearUndo();
    return session == m_session && sendCommit(restrictText(text));
}

void KeyboardController::backspace()
{
    refreshField();
    if (!m_active)
        return;
    const auto session = m_session;
    clearUndo();
    if (session != m_session)
        return;
    if (!m_swipePath.isEmpty()) {
        cancelComposition();
        return;
    }
    if (!m_raw.isEmpty()) {
        m_raw.chop(1);
        refreshComposition();
        return;
    }
    if (!m_segments.isEmpty()) {
        m_raw = m_segments.takeLast().raw;
        refreshComposition();
        return;
    }

    // QTextEdit, TextInput and host adapters retain their native selection,
    // validation and undo behavior via key delivery. For complex graphemes,
    // replace the full previous boundary in one edit when context is safe.
    if (!m_sensitive && query(Qt::ImCurrentSelection).toString().isEmpty()) {
        const QString previous = precedingText();
        QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, previous);
        boundary.toEnd();
        const qsizetype start = boundary.toPreviousBoundary();
        if (start >= 0 && previous.size() - start > 1) {
            const int count = int(previous.size() - start);
            sendCommit({}, -count, count);
            refreshEnglish();
            return;
        }
    }
    sendKey(Qt::Key_Backspace);
    refreshEnglish();
}

void KeyboardController::space()
{
    refreshField();
    if (!m_active)
        return;
    if (!m_swipePath.isEmpty()) {
        const auto *top = m_results.isEmpty() ? nullptr : &m_results.first();
        commitSwipe(top, true, true);
        return;
    }
    if (!m_raw.isEmpty() || !m_segments.isEmpty()) {
        if (const auto *top = bestCompleteChinese())
            commitChinese(top->text, top->code, true);
        else
            commitChinese(m_raw, {}, false);
        return;
    }
    const auto session = m_session;
    const bool english = m_language.startsWith(u"en-");
    const QString accepted = english && canCompose() && m_learningEnabled
        ? currentEnglishWord() : QString();
    const bool alreadyLearned = m_undo.learned && m_undo.text == accepted;
    input(u" "_s);
    if (session == m_session && !alreadyLearned && accepted.size() >= 2 && accepted.size() <= 48
        && precedingText().endsWith(accepted + u' ')) {
        const auto profile = m_profile;
        const auto locale = m_language;
        QMetaObject::invokeMethod(m_lexicon, [lexicon = m_lexicon, profile, locale, accepted] {
            lexicon->remember(profile, locale, accepted.toLower(), accepted);
        }, Qt::QueuedConnection);
    }
}

void KeyboardController::enter()
{
    refreshField();
    if (!m_active)
        return;
    if (!m_swipePath.isEmpty()) {
        const auto *top = m_results.isEmpty() ? nullptr : &m_results.first();
        commitSwipe(top, false, true);
        return;
    }
    if (!m_raw.isEmpty() || !m_segments.isEmpty()) {
        commitChinese(m_raw, {}, false);
        return;
    }
    clearUndo();
    clearCandidates();
    const auto action = m_enter;
    const auto focus = m_focus;
    const auto target = m_inputTarget;
    if (action != Qt::EnterKeyDefault && action != Qt::EnterKeyReturn)
        emit enterActionRequested(action);
    if (focus != m_focus || target != m_inputTarget || !m_active)
        return;
    if (action == Qt::EnterKeyNext || action == Qt::EnterKeyPrevious) {
        if (auto item = qobject_cast<QQuickItem *>(focus.data())) {
            const bool forward = action == Qt::EnterKeyNext;
            if (QQuickItem *next = item->nextItemInFocusChain(forward); next && next != item) {
                next->forceActiveFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
                return;
            }
        }
        sendKey(action == Qt::EnterKeyNext ? Qt::Key_Tab : Qt::Key_Backtab);
        return;
    }
    sendKey(Qt::Key_Return, {}, u"\r"_s);
}

void KeyboardController::moveCursor(int delta)
{
    refreshField();
    if (!m_active || delta == 0)
        return;
    const auto session = m_session;
    clearComposition(true);
    clearUndo();
    const int steps = std::clamp(delta, -64, 64);
    const int key = steps < 0 ? Qt::Key_Left : Qt::Key_Right;
    for (int i = 0; i < std::abs(steps) && session == m_session; ++i)
        sendKey(key);
}

void KeyboardController::selectCandidate(const QString &id)
{
    refreshField();
    if (!canCompose())
        return;
    const auto candidate = std::find_if(m_candidates.cbegin(), m_candidates.cend(), [&id](const QVariant &value) {
        return value.toMap().value(u"id"_s).toString() == id;
    });
    if (candidate == m_candidates.cend())
        return;
    const KeyboardCandidate choice = m_results.at(candidate - m_candidates.cbegin());

    if (!m_swipePath.isEmpty()) {
        commitSwipe(&choice, false, true);
        return;
    }
    if (m_language.startsWith(u"zh")) {
        if (choice.consumed <= 0 || choice.consumed > m_raw.size())
            return;
        if (choice.consumed == m_raw.size()) {
            commitChinese(choice.text, choice.code, true);
        } else {
            m_segments.append({choice.text, m_raw.left(choice.consumed), choice.code});
            m_raw.remove(0, choice.consumed);
            refreshComposition();
        }
        return;
    }

    const QString original = currentEnglishWord();
    if (original.isEmpty() || original != m_englishWord)
        return;
    const auto session = m_session;
    if (sendCommit(choice.text, -int(original.size()), int(original.size()))
        && session == m_session
        && canCompose() && precedingText().endsWith(choice.text)) {
        m_englishWord.clear();
        clearCandidates();
        if (session != m_session)
            return;
        const bool learn = m_learningEnabled;
        m_undo = {choice.text, choice.code, original, query(Qt::ImCursorPosition).toInt(), learn};
        emit undoCandidateChanged();
        if (learn && session == m_session) {
            const auto profile = m_profile;
            const auto locale = m_language;
            QMetaObject::invokeMethod(m_lexicon, [lexicon = m_lexicon, profile, locale, choice] {
                lexicon->remember(profile, locale, choice.code, choice.text);
            }, Qt::QueuedConnection);
        }
    }
}

bool KeyboardController::commitChinese(const QString &text, const QString &code, bool learn)
{
    QString converted;
    QString restore;
    QStringList acceptedSpelling;
    for (const auto &part : m_segments) {
        converted += part.text;
        restore += part.raw;
        acceptedSpelling.append(part.code);
    }
    converted += text;
    restore += m_raw;
    if (!code.isEmpty())
        acceptedSpelling.append(code);
    const QString learningCode = !code.isEmpty() ? acceptedSpelling.join(u' ') : QString();
    const auto session = m_session;
    const auto extra = m_compositionWords;
    const bool committed = sendCommit(converted);
    if (session != m_session)
        return committed;
    clearComposition(!committed);
    if (!committed || session != m_session
        || !canCompose() || !learn || !precedingText().endsWith(converted))
        return committed;

    m_undo = {converted, learningCode, restore, query(Qt::ImCursorPosition).toInt(),
              m_learningEnabled && !learningCode.isEmpty()};
    // Restore the lexicon snapshot if the user undoes this selection.
    m_compositionWords = extra;
    emit undoCandidateChanged();
    if (m_undo.learned) {
        const auto profile = m_profile;
        const auto locale = m_language;
        QMetaObject::invokeMethod(m_lexicon, [lexicon = m_lexicon, profile, locale, learningCode, converted] {
            lexicon->remember(profile, locale, learningCode, converted);
        }, Qt::QueuedConnection);
    }
    return true;
}

const KeyboardCandidate *KeyboardController::bestCompleteChinese() const
{
    const auto whole = std::find_if(m_results.cbegin(), m_results.cend(), [this](const auto &word) {
        return word.consumed == m_raw.size();
    });
    return whole == m_results.cend() ? nullptr : &*whole;
}

bool KeyboardController::commitSwipe(const KeyboardCandidate *candidate, bool addSpace, bool learn)
{
    const QString word = candidate ? candidate->text : m_swipePath;
    const QString code = candidate ? candidate->code : QString();
    const QString commit = addSpace ? word + u' ' : word;
    const auto session = m_session;
    const bool sent = sendCommit(commit);
    if (session != m_session)
        return sent;
    clearComposition(!sent);
    if (!sent || session != m_session || !canCompose() || !learn
        || code.isEmpty() || !precedingText().endsWith(commit))
        return sent;

    m_undo = {commit, code, {}, query(Qt::ImCursorPosition).toInt(), m_learningEnabled, word};
    emit undoCandidateChanged();
    if (m_learningEnabled && session == m_session) {
        const auto profile = m_profile;
        const auto locale = m_language;
        QMetaObject::invokeMethod(m_lexicon, [lexicon = m_lexicon, profile, locale, code, word] {
            lexicon->remember(profile, locale, code, word);
        }, Qt::QueuedConnection);
    }
    return true;
}

void KeyboardController::undoCandidate()
{
    refreshField();
    if (!canCompose() || !canUndoCandidate() || !query(Qt::ImCurrentSelection).toString().isEmpty()
        || query(Qt::ImCursorPosition).toInt() != m_undo.cursor
        || !precedingText().endsWith(m_undo.text)) {
        clearUndo();
        return;
    }
    const Undo undo = m_undo;
    const auto session = m_session;
    clearUndo();
    if (session != m_session)
        return;
    const bool chinese = m_language.startsWith(u"zh");
    if (!sendCommit(chinese ? QString() : undo.restore, -int(undo.text.size()), int(undo.text.size()))
        || session != m_session)
        return;
    if (undo.learned) {
        const auto profile = m_profile;
        const auto locale = m_language;
        QMetaObject::invokeMethod(m_lexicon, [lexicon = m_lexicon, profile, locale, undo] {
            lexicon->forget(profile, locale, undo.code,
                           undo.learnedWord.isEmpty() ? undo.text : undo.learnedWord);
        }, Qt::QueuedConnection);
    }
    if (chinese) {
        m_raw = undo.restore;
        m_inlinePreedit = true;
        refreshComposition();
    } else {
        refreshEnglish();
    }
}

void KeyboardController::cancelComposition()
{
    clearComposition(true);
    clearUndo();
}

void KeyboardController::clearComposition(bool notifyEditor)
{
    const bool show = m_inlinePreedit;
    const bool composed = !m_raw.isEmpty() || !m_segments.isEmpty() || !m_swipePath.isEmpty();
    m_inlinePreedit = false;
    m_raw.clear();
    m_englishWord.clear();
    m_swipePath.clear();
    m_swipeDisplay.clear();
    m_segments.clear();
    m_compositionWords.clear();
    if (show && notifyEditor)
        setPreedit(m_focus, {});
    clearCandidates();
    if (composed)
        emit preeditChanged();
}

void KeyboardController::refreshComposition()
{
    const QString text = preedit();
    if (m_inlinePreedit)
        setPreedit(m_focus, text);
    emit preeditChanged();
    if (text.isEmpty())
        m_inlinePreedit = false;
    requestCandidates(m_swipePath.isEmpty() ? m_raw : m_swipePath);
}

void KeyboardController::clearCandidates()
{
    ++m_generation;
    m_results.clear();
    if (m_candidates.isEmpty())
        return;
    m_candidates.clear();
    emit candidatesChanged();
}

void KeyboardController::refreshEnglish()
{
    if (!canCompose() || !m_language.startsWith(u"en-")) {
        m_englishWord.clear();
        clearCandidates();
        return;
    }
    m_englishWord = currentEnglishWord();
    requestCandidates(m_englishWord);
}

QString KeyboardController::currentEnglishWord() const
{
    if (!query(Qt::ImCurrentSelection).toString().isEmpty())
        return {};
    const QString preceding = precedingText();
    qsizetype start = preceding.size();
    while (start > 0 && asciiLetter(preceding[start - 1]) && preceding.size() - start <= 48)
        --start;
    return preceding.mid(start);
}

QString KeyboardController::precedingText() const
{
    if (m_sensitive || !m_active)
        return {};
    const QVariant previous = query(Qt::ImTextBeforeCursor);
    if (previous.isValid())
        return previous.toString().right(512);
    const int cursor = query(Qt::ImCursorPosition).toInt();
    return query(Qt::ImSurroundingText).toString().left(cursor).right(512);
}

void KeyboardController::setPreedit(QObject *focus, const QString &text)
{
    QScopedValueRollback guard(m_sending, true);
    if (m_inputTarget) {
        m_inputTarget->preedit(text);
    } else if (focus) {
        QInputMethodEvent event(text, {});
        QCoreApplication::sendEvent(focus, &event);
    }
}

bool KeyboardController::sendCommit(const QString &text, int replaceFrom, int replaceLength)
{
    if (!m_active || (text.isEmpty() && replaceLength == 0))
        return false;
    QScopedValueRollback guard(m_sending, true);
    if (m_inputTarget)
        return m_inputTarget->commit(text, replaceFrom, replaceLength);
    if (!m_focus)
        return false;
    QInputMethodEvent event;
    event.setCommitString(text, replaceFrom, replaceLength);
    return QCoreApplication::sendEvent(m_focus, &event) && event.isAccepted();
}

void KeyboardController::sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    if (!m_active)
        return;
    QScopedValueRollback guard(m_sending, true);
    if (m_inputTarget) {
        m_inputTarget->key(key, modifiers, text);
        return;
    }
    const auto focus = m_focus;
    if (!focus)
        return;
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(focus, &press);
    if (focus) {
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(focus, &release);
    }
}

void KeyboardController::clearUndo()
{
    if (m_undo.text.isEmpty())
        return;
    m_undo = {};
    emit undoCandidateChanged();
}

void KeyboardController::requestCandidates(const QString &text)
{
    const QString current = text;
    clearCandidates();
    const quint64 generation = m_generation;
    if (current.isEmpty() || !canCompose())
        return;
    const bool swipe = !m_swipePath.isEmpty();
    const auto &entries = m_language.startsWith(u"zh") || swipe ? m_compositionWords : m_words;
    const KeyboardQuery data{current, m_language, m_pinyinMode, m_profile,
                             entries, m_fuzzyPinyin, m_learningEnabled, swipe};
    QMetaObject::invokeMethod(m_lexicon, [this, generation, data] {
        if (m_generation != generation)
            return;
        auto candidates = m_lexicon->candidates(data);
        if (m_generation != generation)
            return;
        QMetaObject::invokeMethod(this, [this, generation, candidates = std::move(candidates)] {
            if (m_generation != generation || !canCompose())
                return;
            m_results = candidates;
            if (!m_swipePath.isEmpty() && !candidates.isEmpty()) {
                m_swipeDisplay = candidates.first().text;
                if (m_inlinePreedit)
                    setPreedit(m_focus, m_swipeDisplay);
                emit preeditChanged();
                if (m_generation != generation)
                    return;
            }
            for (qsizetype i = 0; i < m_results.size(); ++i) {
                const auto &item = m_results.at(i);
                m_candidates.append(QVariantMap{{u"id"_s, QString::number(generation) + u':' + QString::number(i)},
                                                {u"text"_s, item.text},
                                                {u"correction"_s, item.correction}});
            }
            emit candidatesChanged();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void KeyboardController::runUserOperation(const QString &name,
                                           std::function<bool(KeyboardLexicon &)> operation)
{
    refreshField();
    if (m_sensitive) {
        emit userDataOperationFinished(name, false);
        return;
    }
    QMetaObject::invokeMethod(m_lexicon, [this, name, operation = std::move(operation)] {
        const bool success = operation(*m_lexicon);
        QMetaObject::invokeMethod(this, [this, name, success] {
            if (success) {
                if (!m_raw.isEmpty())
                    requestCandidates(m_raw);
                else if (!m_englishWord.isEmpty())
                    requestCandidates(m_englishWord);
                if (m_userPhrasesRequested)
                    refreshUserPhrases();
            }
            emit userDataOperationFinished(name, success);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void KeyboardController::clearLearning()
{
    runUserOperation(u"clear"_s, [profile = m_profile](KeyboardLexicon &engine) {
        return engine.clearLearning(profile);
    });
}

void KeyboardController::addUserPhrase(const QString &code, const QString &text)
{
    runUserOperation(u"add"_s, [profile = m_profile, locale = m_language, code, text](KeyboardLexicon &engine) {
        return engine.setPhrase(profile, locale, code, text);
    });
}

void KeyboardController::refreshUserPhrases()
{
    m_userPhrasesRequested = true;
    if (m_sensitive)
        return;
    QMetaObject::invokeMethod(m_lexicon, [this, profile = m_profile, locale = m_language] {
        const auto values = m_lexicon->userPhrases(profile, locale);
        QMetaObject::invokeMethod(this, [this, profile, locale, values] {
            if (profile == m_profile && locale == m_language && m_userPhrases != values) {
                m_userPhrases = values;
                emit userPhrasesChanged();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void KeyboardController::clearUserPhraseView()
{
    if (!m_userPhrases.isEmpty()) {
        m_userPhrases.clear();
        emit userPhrasesChanged();
    }
    if (m_userPhrasesRequested)
        refreshUserPhrases();
}

void KeyboardController::removeUserPhrase(const QString &code, const QString &text)
{
    runUserOperation(u"remove"_s, [profile = m_profile, locale = m_language, code, text](KeyboardLexicon &engine) {
        return engine.removePhrase(profile, locale, code, text);
    });
}

void KeyboardController::exportUserData(const QUrl &file)
{
    runUserOperation(u"export"_s, [profile = m_profile, file](KeyboardLexicon &engine) {
        return file.isLocalFile() && engine.exportProfile(profile, file.toLocalFile());
    });
}

void KeyboardController::importUserData(const QUrl &file)
{
    runUserOperation(u"import"_s, [profile = m_profile, file](KeyboardLexicon &engine) {
        return file.isLocalFile() && engine.importProfile(profile, file.toLocalFile());
    });
}

void KeyboardController::sendClipboardKey(int key)
{
    refreshField();
    if (!m_active || m_sensitive || m_hints.testFlag(Qt::ImhNoEditMenu))
        return;
    const quint64 session = m_session;
    const QString copied = (key == Qt::Key_C || key == Qt::Key_X)
        && m_clipboardHistoryEnabled ? query(Qt::ImCurrentSelection).toString() : QString();
    QString pasted;
    if (key == Qt::Key_V && m_clipboardHistoryEnabled && !m_inputTarget)
        pasted = QGuiApplication::clipboard()->text();
    clearComposition(true);
    clearUndo();
    if (session != m_session)
        return;
    sendKey(key, Qt::ControlModifier);
    if (session == m_session)
        recordClipboard(pasted.isEmpty() ? copied : pasted);
}

void KeyboardController::copy() { sendClipboardKey(Qt::Key_C); }
void KeyboardController::cut() { sendClipboardKey(Qt::Key_X); }
void KeyboardController::paste() { sendClipboardKey(Qt::Key_V); }
void KeyboardController::selectAll() { sendClipboardKey(Qt::Key_A); }

void KeyboardController::recordClipboard(const QString &text)
{
    if (m_sensitive || !m_clipboardHistoryEnabled || text.isEmpty() || text.size() > 4096)
        return;
    m_clipboard.removeIf([&text](const auto &item) { return item.first == text; });
    m_clipboard.prepend({text, QDateTime::currentSecsSinceEpoch()});
    if (m_clipboard.size() > 10)
        m_clipboard.removeLast();
    emit clipboardHistoryChanged();
}

void KeyboardController::pruneClipboard()
{
    const qint64 earliest = QDateTime::currentSecsSinceEpoch() - 3600;
    if (m_clipboard.removeIf([earliest](const auto &item) { return item.second < earliest; }) > 0)
        emit clipboardHistoryChanged();
}

void KeyboardController::clearClipboardHistory()
{
    if (m_clipboard.isEmpty())
        return;
    m_clipboard.clear();
    emit clipboardHistoryChanged();
}

void KeyboardController::pasteHistory(int index)
{
    refreshField();
    if (!m_active || m_sensitive || !m_clipboardHistoryEnabled
        || m_hints.testFlag(Qt::ImhNoEditMenu) || index < 0 || index >= m_clipboard.size())
        return;
    pruneClipboard();
    if (index >= m_clipboard.size())
        return;
    const auto session = m_session;
    const QString text = m_clipboard.at(index).first;
    clearComposition(true);
    clearUndo();
    if (session == m_session && sendCommit(restrictText(text)) && session == m_session)
        recordClipboard(text);
}

void KeyboardController::requestRecentEmoji()
{
    QMetaObject::invokeMethod(m_lexicon, [this, profile = m_profile] {
        const auto values = m_lexicon->recentEmoji(profile);
        QMetaObject::invokeMethod(this, [this, profile, values] {
            if (profile == m_profile && values != m_recentEmoji) {
                m_recentEmoji = values;
                emit recentEmojiChanged();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void KeyboardController::recordEmoji(const QString &emoji)
{
    if (!canCompose() || emoji.isEmpty() || emoji.size() > 24 || !precedingText().endsWith(emoji))
        return;
    bool hasSymbol = false;
    for (uint character : emoji.toUcs4()) {
        const auto category = QChar::category(character);
        if (category == QChar::Symbol_Other || character == 0x20e3)
            hasSymbol = true;
        else if (character != 0x200d && character != 0xfe0f && character != 0xfe0e
                 && !(character >= 0xe0020 && character <= 0xe007f)
                 && !(character >= 0x1f3fb && character <= 0x1f3ff)
                 && !(character >= '0' && character <= '9') && character != '#' && character != '*')
            return;
    }
    if (!hasSymbol)
        return;
    QMetaObject::invokeMethod(m_lexicon, [this, profile = m_profile, emoji] {
        if (m_lexicon->rememberEmoji(profile, emoji)) {
            const auto values = m_lexicon->recentEmoji(profile);
            QMetaObject::invokeMethod(this, [this, profile, values] {
                if (profile == m_profile && values != m_recentEmoji) {
                    m_recentEmoji = values;
                    emit recentEmojiChanged();
                }
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}
