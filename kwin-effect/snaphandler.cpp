// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snaphandler.h"

#include "plasmazoneseffect.h"

namespace PlasmaZones {

SnapHandler::SnapHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

} // namespace PlasmaZones
