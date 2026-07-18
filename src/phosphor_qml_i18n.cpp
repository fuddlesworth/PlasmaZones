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
            const int digit = text.at(i + 1).digitValue();
            if (digit >= 1 && digit <= args.size()) {
                out += args.at(digit - 1);
                ++i;
                continue;
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

QString PhosphorLocalizedContext::i18np(const QString& singular, const QString& plural, int n) const
{
    // Qt numerus translation only works with loaded .ts files.
    // Without them, translate() always returns the singular with %n replaced.
    // We select the correct English form ourselves, then substitute %n.
    const QString& form = (n == 1) ? singular : plural;
    QString result = QCoreApplication::translate("plasmazones", form.toUtf8().constData(), nullptr, n);
    // translate() replaces %n when numerus arg is provided, but guard against
    // cases where it doesn't (e.g. if the string was found in a .ts file
    // without numerusform entries)
    if (result.contains(QLatin1String("%n"))) {
        result.replace(QLatin1String("%n"), QString::number(n));
    }
    return result;
}

QString PhosphorLocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural,
                                         int n) const
{
    const QString& form = (n == 1) ? singular : plural;
    QString result =
        QCoreApplication::translate("plasmazones", form.toUtf8().constData(), context.toUtf8().constData(), n);
    if (result.contains(QLatin1String("%n"))) {
        result.replace(QLatin1String("%n"), QString::number(n));
    }
    return result;
}
