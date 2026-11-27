#pragma once

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>

inline void configureVeloraFonts()
{
    const QDir fonts(qEnvironmentVariable("QT_QPA_FONTDIR", QStringLiteral("/usr/share/fonts")));
    const QString cjkFile = fonts.filePath(QStringLiteral("NotoSansCJK-Regular.ttc"));
    if (QFileInfo::exists(cjkFile))
        QFontDatabase::addApplicationFont(cjkFile);
    const QString cjkFamily = QStringLiteral("Noto Sans CJK SC");
    if (QFontDatabase::families().contains(cjkFamily))
        QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han, {cjkFamily});

    const QString emojiFile = fonts.filePath(QStringLiteral("NotoColorEmoji.ttf"));
    if (QFileInfo::exists(emojiFile))
        QFontDatabase::addApplicationFont(emojiFile);
    const QString emojiFamily = QStringLiteral("Noto Color Emoji");
    if (QFontDatabase::families().contains(emojiFamily))
        QFontDatabase::setApplicationEmojiFontFamilies({emojiFamily});

    QFont font = QGuiApplication::font();
    QStringList families = font.families();
    if (QFontDatabase::families().contains(cjkFamily) && !families.contains(cjkFamily))
        families.append(cjkFamily);
    font.setFamilies(families);
    QGuiApplication::setFont(font);
}
