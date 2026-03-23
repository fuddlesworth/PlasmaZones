// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QML i18n context.  Exposes i18n()/i18nc()/i18np()/i18ncp() to QML
// via setContextObject().  Backed by QCoreApplication::translate()
// with "plasmazones" as the translation context.
//
// Supports %1..%5 argument substitution (covers all current QML usage).

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include "plasmazones_export.h"

class PLASMAZONES_EXPORT PzLocalizedContext : public QObject
{
    Q_OBJECT
public:
    explicit PzLocalizedContext(QObject* parent = nullptr);

    // i18n("text") and i18n("text %1", arg1, ..., arg5)
    Q_INVOKABLE QString i18n(const QString& text, const QVariant& a1 = QVariant(), const QVariant& a2 = QVariant(),
                             const QVariant& a3 = QVariant(), const QVariant& a4 = QVariant(),
                             const QVariant& a5 = QVariant()) const;

    // i18nc("@ctx", "text") and i18nc("@ctx", "text %1", arg1, ..., arg5)
    Q_INVOKABLE QString i18nc(const QString& context, const QString& text, const QVariant& a1 = QVariant(),
                              const QVariant& a2 = QVariant(), const QVariant& a3 = QVariant(),
                              const QVariant& a4 = QVariant(), const QVariant& a5 = QVariant()) const;

    // i18np("1 item", "%1 items", count)
    Q_INVOKABLE QString i18np(const QString& singular, const QString& plural, int n) const;

    // i18ncp("@ctx", "1 item", "%1 items", count)
    Q_INVOKABLE QString i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const;

private:
    static QString substituteArgs(QString text, const QVariant& a1, const QVariant& a2, const QVariant& a3,
                                  const QVariant& a4, const QVariant& a5);
};
