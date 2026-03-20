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
    if (a1.isValid())
        text = text.arg(a1.toString());
    if (a2.isValid())
        text = text.arg(a2.toString());
    if (a3.isValid())
        text = text.arg(a3.toString());
    if (a4.isValid())
        text = text.arg(a4.toString());
    if (a5.isValid())
        text = text.arg(a5.toString());
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
    QString result = QCoreApplication::translate(
        "plasmazones", (n == 1) ? singular.toUtf8().constData() : plural.toUtf8().constData());
    return result.arg(n);
}

QString PzLocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const
{
    QString result = QCoreApplication::translate("plasmazones",
                                                 (n == 1) ? singular.toUtf8().constData() : plural.toUtf8().constData(),
                                                 context.toUtf8().constData());
    return result.arg(n);
}
