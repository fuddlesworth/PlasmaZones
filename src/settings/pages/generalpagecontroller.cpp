// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "generalpagecontroller.h"

#include "core/interfaces/isettings.h"
#include "phosphor_i18n.h"
#include "utils/gpudevicelist.h"

#include <QVariantMap>

#include <algorithm>

namespace PlasmaZones {

GeneralPageController::GeneralPageController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("general"), parent)
{
    // Snapshot current backend + GPU so the QML "restart required" message
    // survives page recreation.
    m_startupRenderingBackend = settings.renderingBackend();
    m_startupGpuDevice = settings.gpuDevice();

    QVariantMap autoEntry;
    autoEntry.insert(QStringLiteral("text"), PhosphorI18n::tr("Automatic"));
    autoEntry.insert(QStringLiteral("value"), QStringLiteral("auto"));
    m_availableGpus.append(autoEntry);
    m_availableGpus.append(GpuDeviceList::enumerate());

    // Keep a persisted selection displayable even when the device is gone.
    const QString stored = settings.gpuDevice();
    const bool storedPresent = std::any_of(m_availableGpus.cbegin(), m_availableGpus.cend(), [&](const QVariant& v) {
        return v.toMap().value(QStringLiteral("value")).toString() == stored;
    });
    if (!storedPresent) {
        QVariantMap missing;
        missing.insert(QStringLiteral("text"), PhosphorI18n::tr("Unavailable device (%1)").arg(stored));
        missing.insert(QStringLiteral("value"), stored);
        m_availableGpus.append(missing);
    }
}

} // namespace PlasmaZones
