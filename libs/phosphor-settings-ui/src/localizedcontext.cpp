// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/LocalizedContext.h"

#include <QCoreApplication>

namespace PhosphorSettingsUi {

LocalizedContext::LocalizedContext(QObject* parent)
    : QObject(parent)
    , m_context(QCoreApplication::applicationName())
{
}

LocalizedContext::~LocalizedContext() = default;

QString LocalizedContext::translationContext() const
{
    return m_context;
}

void LocalizedContext::setTranslationContext(const QString& ctx)
{
    if (m_context == ctx) {
        return;
    }
    m_context = ctx;
    Q_EMIT translationContextChanged();
}

QString LocalizedContext::i18n(const QString& text) const
{
    return QCoreApplication::translate(qPrintable(m_context), qPrintable(text));
}

QString LocalizedContext::i18nc(const QString& context, const QString& text) const
{
    return QCoreApplication::translate(qPrintable(m_context), qPrintable(text), qPrintable(context));
}

QString LocalizedContext::i18np(const QString& singular, const QString& plural, int n) const
{
    return QCoreApplication::translate(qPrintable(m_context), qPrintable(n == 1 ? singular : plural), nullptr, n);
}

QString LocalizedContext::i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const
{
    return QCoreApplication::translate(qPrintable(m_context), qPrintable(n == 1 ? singular : plural),
                                       qPrintable(context), n);
}

} // namespace PhosphorSettingsUi
