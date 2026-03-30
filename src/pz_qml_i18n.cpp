// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pz_qml_i18n.h"
#include <QCoreApplication>

PzLocalizedContext::PzLocalizedContext(QObject* parent)
    : QObject(parent)
{
}

QString PzLocalizedContext::substituteArgs(QString text, const QVariant& a1, const QVariant& a2, const QVariant& a3,
                                           const QVariant& a4, const QVariant& a5)
{
    // Use multi-arg QString::arg() to prevent double-substitution when
    // argument values themselves contain %N markers. Sequential .arg()
    // calls would replace markers introduced by earlier substitutions.
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
    for (int i = 0; i < args.size(); ++i)
        text = text.replace(QStringLiteral("%") + QString::number(i + 1), args.at(i));
    return text;
}

QString PzLocalizedContext::i18n(const QString& text, const QVariant& a1, const QVariant& a2, const QVariant& a3,
                                 const QVariant& a4, const QVariant& a5) const
{
    QString result = QCoreApplication::translate("plasmazones", text.toUtf8().constData());
    return substituteArgs(result, a1, a2, a3, a4, a5);
}

QString PzLocalizedContext::i18nc(const QString& context, const QString& text, const QVariant& a1, const QVariant& a2,
                                  const QVariant& a3, const QVariant& a4, const QVariant& a5) const
{
    QString result =
        QCoreApplication::translate("plasmazones", text.toUtf8().constData(), context.toUtf8().constData());
    return substituteArgs(result, a1, a2, a3, a4, a5);
}

QString PzLocalizedContext::i18np(const QString& singular, const QString& plural, int n) const
{
    // Qt numerus: the singular form is the lookup key in .ts files.
    // translate() with the 4th arg (n) selects the correct numerus form
    // and auto-replaces %n with the count.
    Q_UNUSED(plural) // Plural form is embedded in the .ts numerusform entries
    return QCoreApplication::translate("plasmazones", singular.toUtf8().constData(), nullptr, n);
}

QString PzLocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const
{
    Q_UNUSED(plural)
    return QCoreApplication::translate("plasmazones", singular.toUtf8().constData(), context.toUtf8().constData(), n);
}
