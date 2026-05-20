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
}

} // namespace PlasmaZones
