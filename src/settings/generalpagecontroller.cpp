// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "generalpagecontroller.h"

#include "../config/settings.h"
#include "../pz_i18n.h"

namespace PlasmaZones {

GeneralPageController::GeneralPageController(Settings* settings, QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(settings);

    // Translate rendering backend display names once at construction.
    for (const auto& name : ConfigDefaults::renderingBackendDisplayNames()) {
        m_renderingBackendDisplayNames.append(PzI18n::tr(name.toUtf8().constData()));
    }

    // Snapshot current backend so the QML "restart required" message
    // survives page recreation.
    m_startupRenderingBackend = settings->renderingBackend();
}

} // namespace PlasmaZones
