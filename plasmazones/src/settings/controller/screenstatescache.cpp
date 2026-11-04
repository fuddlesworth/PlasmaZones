// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenstatescache.h"

#include <utility>

namespace PlasmaZones {

ScreenStatesCache::ScreenStatesCache(int coalesceMs, QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(coalesceMs);
    connect(&m_timer, &QTimer::timeout, this, &ScreenStatesCache::refreshDue);
}

void ScreenStatesCache::seed(QVariantList states)
{
    m_states = std::move(states);
}

void ScreenStatesCache::schedule()
{
    m_timer.start();
}

quint64 ScreenStatesCache::beginRequest()
{
    return ++m_latestRequest;
}

bool ScreenStatesCache::finishRequest(quint64 request, bool ok, QVariantList states)
{
    if (request != m_latestRequest || !ok || states == m_states) {
        return false;
    }
    m_states = std::move(states);
    Q_EMIT statesChanged();
    return true;
}

} // namespace PlasmaZones
