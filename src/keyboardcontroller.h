#pragma once

#include "keyboardlexicon.h"

#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>

class KeyboardInputTarget : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The host application supplies an input target")
public:
    using QObject::QObject;

    virtual bool active() const = 0;
    virtual QVariant query(Qt::InputMethodQuery property) const = 0;
    virtual bool commit(const QString &text, int replacementStart, int replacementLength) = 0;
    virtual void preedit(const QString &text) = 0;
    virtual void key(int key, Qt::KeyboardModifiers modifiers, const QString &text) = 0;

signals:
    // sessionChanged indicates the old editor no longer accepts preedit or commit.
    void sessionChanged();
    // stateChanged can update hints or availability within the current session.
    void stateChanged();
};

class KeyboardController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY fieldChanged FINAL)
    Q_PROPERTY(QString sessionToken READ sessionToken NOTIFY sessionTokenChanged FINAL)
    Q_PROPERTY(bool sensitive READ sensitive NOTIFY fieldChanged FINAL)
    Q_PROPERTY(bool privateMode READ privateMode WRITE setPrivateMode NOTIFY privateModeChanged FINAL)
    Q_PROPERTY(bool learningEnabled READ learningEnabled WRITE setLearningEnabled
               NOTIFY learningEnabledChanged FINAL)
    Q_PROPERTY(bool fuzzyPinyin READ fuzzyPinyin WRITE setFuzzyPinyin NOTIFY fuzzyPinyinChanged FINAL)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(QString pinyinMode READ pinyinMode WRITE setPinyinMode NOTIFY pinyinModeChanged FINAL)
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged FINAL)
    Q_PROPERTY(QString inputMode READ inputMode NOTIFY fieldChanged FINAL)
    Q_PROPERTY(QString enterLabel READ enterLabel NOTIFY fieldChanged FINAL)
    Q_PROPERTY(QString preedit READ preedit NOTIFY preeditChanged FINAL)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged FINAL)
    Q_PROPERTY(QVariantList supplementalEntries READ supplementalEntries WRITE setSupplementalEntries
               NOTIFY supplementalEntriesChanged FINAL)
    Q_PROPERTY(bool canUndoCandidate READ canUndoCandidate NOTIFY undoCandidateChanged FINAL)
    Q_PROPERTY(bool clipboardHistoryEnabled READ clipboardHistoryEnabled WRITE setClipboardHistoryEnabled
               NOTIFY clipboardHistoryEnabledChanged FINAL)
    Q_PROPERTY(QStringList clipboardHistory READ clipboardHistory NOTIFY clipboardHistoryChanged FINAL)
    Q_PROPERTY(QStringList recentEmoji READ recentEmoji NOTIFY recentEmojiChanged FINAL)
    Q_PROPERTY(QVariantList userPhrases READ userPhrases NOTIFY userPhrasesChanged FINAL)
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(KeyboardInputTarget *inputTarget READ inputTarget WRITE setInputTarget
               NOTIFY inputTargetChanged FINAL)

public:
    explicit KeyboardController(QObject *parent = nullptr);
    ~KeyboardController() override;

    bool enabled() const { return m_enabled; }
    bool active() const { return m_active; }
    QString sessionToken() const { return QString::number(m_session); }
    bool sensitive() const { return m_sensitive; }
    bool privateMode() const { return m_privateMode; }
    bool learningEnabled() const { return m_learningEnabled; }
    bool fuzzyPinyin() const { return m_fuzzyPinyin; }
    QString language() const { return m_language; }
    QString pinyinMode() const { return m_pinyinMode; }
    QString profileId() const { return m_profile; }
    QString inputMode() const { return m_mode; }
    QString enterLabel() const;
    QString preedit() const;
    QVariantList candidates() const { return m_candidates; }
    QVariantList supplementalEntries() const { return m_supplementalEntries; }
    bool canUndoCandidate() const { return !m_undo.text.isEmpty(); }
    bool clipboardHistoryEnabled() const { return m_clipboardHistoryEnabled; }
    QStringList clipboardHistory() const;
    QStringList recentEmoji() const;
    QVariantList userPhrases() const;
    QQuickWindow *window() const;
    KeyboardInputTarget *inputTarget() const { return m_inputTarget; }

    void setEnabled(bool enabled);
    void setPrivateMode(bool enabled);
    void setLearningEnabled(bool enabled);
    void setFuzzyPinyin(bool enabled);
    void setLanguage(const QString &language);
    void setPinyinMode(const QString &mode);
    void setProfileId(const QString &profile);
    void setSupplementalEntries(const QVariantList &entries);
    void setClipboardHistoryEnabled(bool enabled);
    void setWindow(QQuickWindow *window);
    void setInputTarget(KeyboardInputTarget *inputTarget);

    Q_INVOKABLE void input(const QString &text);
    Q_INVOKABLE void inputDirect(const QString &text);
    Q_INVOKABLE void inputSwipe(const QString &path);
    Q_INVOKABLE bool inputLiteral(const QString &text, const QString &sessionToken);
    Q_INVOKABLE void backspace();
    Q_INVOKABLE void space();
    Q_INVOKABLE void enter();
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void selectCandidate(const QString &id);
    Q_INVOKABLE void cancelComposition();
    Q_INVOKABLE void undoCandidate();
    Q_INVOKABLE void clearLearning();
    Q_INVOKABLE void addUserPhrase(const QString &code, const QString &text);
    Q_INVOKABLE void refreshUserPhrases();
    Q_INVOKABLE void removeUserPhrase(const QString &code, const QString &text);
    Q_INVOKABLE void exportUserData(const QUrl &file);
    Q_INVOKABLE void importUserData(const QUrl &file);
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void pasteHistory(int index);
    Q_INVOKABLE void clearClipboardHistory();
    Q_INVOKABLE void recordEmoji(const QString &emoji);
    Q_INVOKABLE void selectAll();

signals:
    void enabledChanged();
    void fieldChanged();
    void sessionTokenChanged();
    void privateModeChanged();
    void learningEnabledChanged();
    void fuzzyPinyinChanged();
    void languageChanged();
    void pinyinModeChanged();
    void profileIdChanged();
    void preeditChanged();
    void candidatesChanged();
    void supplementalEntriesChanged();
    void undoCandidateChanged();
    void clipboardHistoryEnabledChanged();
    void clipboardHistoryChanged();
    void recentEmojiChanged();
    void userPhrasesChanged();
    void windowChanged();
    void inputTargetChanged();
    void userDataOperationFinished(const QString &operation, bool success);
    void enterActionRequested(Qt::EnterKeyType action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshField();
    void cancelAfterExternalEdit();

private:
    struct Undo {
        QString text;
        QString code;
        QString restore;
        int cursor = -1;
        bool learned = false;
        QString learnedWord;
    };
    struct CompositionPart {
        QString text;
        QString raw;
        QString code;
    };

    void trackFocus();
    void warmLexicon();
    void advanceSession();
    void setFocusObject(QObject *focus);
    QVariant query(Qt::InputMethodQuery property) const;
    void setPreedit(QObject *focus, const QString &text);
    void clearComposition(bool notifyEditor);
    void refreshComposition();
    void clearCandidates();
    void refreshEnglish();
    QString currentEnglishWord() const;
    QString precedingText() const;
    QString restrictText(const QString &text) const;
    bool canCompose() const;
    bool sendCommit(const QString &text, int replaceFrom = 0, int replaceLength = 0);
    void sendKey(int key, Qt::KeyboardModifiers modifiers = {}, const QString &text = {});
    bool commitChinese(const QString &text, const QString &code, bool learn);
    const KeyboardCandidate *bestCompleteChinese() const;
    bool commitSwipe(const KeyboardCandidate *candidate, bool addSpace, bool learn);
    void requestCandidates(const QString &text);
    void clearUndo();
    void runUserOperation(const QString &name, std::function<bool(KeyboardLexicon &)> operation);
    void sendClipboardKey(int key);
    void recordClipboard(const QString &text);
    void pruneClipboard();
    void requestRecentEmoji();
    void clearUserPhraseView();

    QPointer<QQuickWindow> m_window;
    QPointer<KeyboardInputTarget> m_inputTarget;
    QPointer<QObject> m_focus;
    QThread m_workerThread;
    KeyboardLexicon *m_lexicon;
    std::atomic<quint64> m_generation{0};
    quint64 m_session = 0;
    QString m_language = QStringLiteral("en-US");
    QString m_pinyinMode = QStringLiteral("full");
    QString m_profile = QStringLiteral("default");
    QString m_mode = QStringLiteral("text");
    QString m_raw;
    QString m_englishWord;
    QString m_swipePath;
    QString m_swipeDisplay;
    QList<CompositionPart> m_segments;
    QVariantList m_supplementalEntries;
    QList<KeyboardWord> m_words;
    QList<KeyboardWord> m_compositionWords;
    QList<KeyboardCandidate> m_results;
    QVariantList m_candidates;
    QList<QPair<QString, qint64>> m_clipboard;
    QStringList m_recentEmoji;
    QVariantList m_userPhrases;
    QTimer m_clipboardTimer;
    Undo m_undo;
    Qt::InputMethodHints m_hints;
    Qt::EnterKeyType m_enter = Qt::EnterKeyDefault;
    bool m_enabled = true;
    bool m_active = false;
    bool m_sensitive = false;
    bool m_privateMode = false;
    bool m_learningEnabled = true;
    bool m_fuzzyPinyin = false;
    bool m_inlinePreedit = false;
    bool m_sending = false;
    bool m_clipboardHistoryEnabled = false;
    bool m_userPhrasesRequested = false;
};
