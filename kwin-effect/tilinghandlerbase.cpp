// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilinghandlerbase.h"

namespace PlasmaZones {

TilingHandlerBase::TilingHandlerBase(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void TilingHandlerBase::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
    if (!enabled) {
        // Re-enabling later must re-evaluate the cursor's window from scratch
        // — a stale key here would suppress the first re-focus event.
        m_lastFocusFollowsMouseWindowId.clear();
    }
}

void TilingHandlerBase::clearLastFocusFollowsMouseWindow(const QString& windowId)
{
    if (m_lastFocusFollowsMouseWindowId == windowId) {
        m_lastFocusFollowsMouseWindowId.clear();
    }
}

} // namespace PlasmaZones
