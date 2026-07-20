// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "generalpagecontroller.h"

#include "../core/isettings.h"

namespace PlasmaZones {

GeneralPageController::GeneralPageController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("general"), parent)
{
    // Snapshot current backend so the QML "restart required" message
    // survives page recreation.
    m_startupRenderingBackend = settings.renderingBackend();
}

} // namespace PlasmaZones
