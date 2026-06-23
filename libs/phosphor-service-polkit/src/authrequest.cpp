// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/AuthRequest.h>

namespace PhosphorServicePolkit {

AuthRequest::AuthRequest(QString actionId, QString message, QString iconName, QVariantMap details, QString cookie,
                         QStringList identities, QObject* parent)
    : QObject(parent)
    , m_actionId(std::move(actionId))
    , m_message(std::move(message))
    , m_iconName(std::move(iconName))
    , m_details(std::move(details))
    , m_cookie(std::move(cookie))
    , m_identities(std::move(identities))
{
}

void AuthRequest::setSelectedIdentity(int index)
{
    if (index < 0 || index >= m_identities.size() || index == m_selectedIdentity)
        return;
    m_selectedIdentity = index;
    Q_EMIT selectedIdentityChanged();
}

} // namespace PhosphorServicePolkit
