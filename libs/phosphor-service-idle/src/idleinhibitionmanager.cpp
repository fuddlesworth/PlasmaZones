// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "idleinhibitionmanager.h"

namespace PhosphorServiceIdle {

IdleInhibitionManager::IdleInhibitionManager(QObject* parent)
    : QObject(parent)
{
}

IdleInhibitionManager::~IdleInhibitionManager() = default;

int IdleInhibitionManager::inhibit()
{
    const int cookie = m_nextCookie++;
    const bool wasInhibited = !m_cookies.isEmpty();
    m_cookies.insert(cookie);
    if (!wasInhibited)
        Q_EMIT inhibitedChanged(true);
    return cookie;
}

bool IdleInhibitionManager::release(int cookie)
{
    if (!m_cookies.remove(cookie))
        return false; // unknown or already-released cookie: a safe no-op.
    if (m_cookies.isEmpty())
        Q_EMIT inhibitedChanged(false);
    return true;
}

bool IdleInhibitionManager::isInhibited() const
{
    return !m_cookies.isEmpty();
}

int IdleInhibitionManager::activeCount() const
{
    return m_cookies.size();
}

} // namespace PhosphorServiceIdle
