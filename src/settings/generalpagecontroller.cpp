// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "generalpagecontroller.h"

#include "../core/isettings.h"
#include "../phosphor_i18n.h"

namespace PlasmaZones {

GeneralPageController::GeneralPageController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("general"), parent)
{
    // Translate rendering backend display names once at construction.
    for (const auto& name : ConfigDefaults::renderingBackendDisplayNames()) {
        m_renderingBackendDisplayNames.append(PhosphorI18n::tr(name.toUtf8().constData()));
    }

    // Snapshot current backend so the QML "restart required" message
    // survives page recreation.
    m_startupRenderingBackend = settings.renderingBackend();
}

} // namespace PlasmaZones
