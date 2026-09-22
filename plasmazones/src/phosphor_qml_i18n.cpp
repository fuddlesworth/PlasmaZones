// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phosphor_qml_i18n.h"
#include <QCoreApplication>

PhosphorLocalizedContext::PhosphorLocalizedContext(QObject* parent)
    : QObject(parent)
{
}

QString PhosphorLocalizedContext::substituteArgs(QString text, const QVariant& a1, const QVariant& a2,
                                                 const QVariant& a3, const QVariant& a4, const QVariant& a5)
{
    // Single left-to-right scan so %N markers introduced by an argument's
    // own value are never re-substituted (a layout named "%2" must come
    // out verbatim). Sequential QString::replace passes would substitute
    // markers injected by earlier substitutions. %N with no matching
    // argument stays literal, matching the previous behaviour.
    QStringList args;
    if (a1.isValid())
        args << a1.toString();
    if (a2.isValid())
        args << a2.toString();
    if (a3.isValid())
        args << a3.toString();
    if (a4.isValid())
        args << a4.toString();
    if (a5.isValid())
        args << a5.toString();
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('%') && i + 1 < text.size()) {
            // Compare the ASCII digit directly rather than via QChar::digitValue(),
            // which returns a value for ANY Unicode decimal digit (Arabic-Indic,
            // Devanagari, …): a translated string with a literal `%` before a
            // localized digit must not have an argument spliced into it.
            const QChar next = text.at(i + 1);
            if (next >= u'1' && next <= u'5') {
                const int digit = next.unicode() - u'0';
                if (digit <= args.size()) {
                    out += args.at(digit - 1);
                    ++i;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

QString PhosphorLocalizedContext::i18n(const QString& text, const QVariant& a1, const QVariant& a2, const QVariant& a3,
                                       const QVariant& a4, const QVariant& a5) const
{
    QString result = QCoreApplication::translate("plasmazones", text.toUtf8().constData());
    return substituteArgs(result, a1, a2, a3, a4, a5);
}

QString PhosphorLocalizedContext::i18nc(const QString& context, const QString& text, const QVariant& a1,
                                        const QVariant& a2, const QVariant& a3, const QVariant& a4,
                                        const QVariant& a5) const
{
    QString result =
        QCoreApplication::translate("plasmazones", text.toUtf8().constData(), context.toUtf8().constData());
    return substituteArgs(result, a1, a2, a3, a4, a5);
}

QString PhosphorLocalizedContext::i18np(const QString& singular, const QString& plural, int nRaw) const
{
    // Qt's plural-form selection is undefined for a negative numerus, and the
    // English-fallback branch below would pick "plural" for it too; clamp so
    // both paths treat a stray negative as zero rather than mis-rendering.
    const int n = qMax(0, nRaw);
    // The SINGULAR is always the catalog key: lupdate indexes the numerus
    // message under it, and translate() with a numerus arg picks the right
    // numerusform via the target language's plural rules. Pre-selecting the
    // English form here made every n != 1 lookup miss the catalog entirely
    // (the plural text is not a key), so the second numerusform was
    // unreachable in every language.
    // Probe WITHOUT the numerus arg first: translate() with a numerus arg
    // substitutes %n into the source text even when no translator is
    // installed, so the numerus result can never be compared against the
    // source to detect a catalog miss.
    //
    // KNOWN LIMITATION: a translation whose text is byte-identical to the
    // English singular is indistinguishable from a catalog miss here, so for
    // such an entry n != 1 falls back to the English plural rather than the
    // catalog's second numerusform. Harmless for the current catalogs; a
    // future language that legitimately keeps the singular identical to English
    // would need the numerus form to differ, or a translator-installed probe.
    const QByteArray key = singular.toUtf8();
    const QString probe = QCoreApplication::translate("plasmazones", key.constData());
    if (probe == singular) {
        // Catalog miss (or untranslated entry): English form selection.
        QString result = (n == 1) ? singular : plural;
        result.replace(QLatin1String("%n"), QString::number(n));
        return result;
    }
    QString result = QCoreApplication::translate("plasmazones", key.constData(), nullptr, n);
    // translate() replaces %n when a numerus arg is provided, but guard
    // against a .ts entry without numerusform.
    if (result.contains(QLatin1String("%n"))) {
        result.replace(QLatin1String("%n"), QString::number(n));
    }
    return result;
}

QString PhosphorLocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural,
                                         int nRaw) const
{
    // Same key + probe contract as i18np above, including the negative-n clamp
    // and the identical-to-English limitation.
    const int n = qMax(0, nRaw);
    const QByteArray key = singular.toUtf8();
    const QByteArray ctx = context.toUtf8();
    const QString probe = QCoreApplication::translate("plasmazones", key.constData(), ctx.constData());
    if (probe == singular) {
        QString result = (n == 1) ? singular : plural;
        result.replace(QLatin1String("%n"), QString::number(n));
        return result;
    }
    QString result = QCoreApplication::translate("plasmazones", key.constData(), ctx.constData(), n);
    if (result.contains(QLatin1String("%n"))) {
        result.replace(QLatin1String("%n"), QString::number(n));
    }
    return result;
}
