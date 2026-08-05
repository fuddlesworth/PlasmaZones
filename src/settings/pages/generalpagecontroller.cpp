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
    , m_settings(settings)
{
    // Snapshot current backend + GPU so the QML "restart required" message
    // survives page recreation. Re-baselined on daemon restart via
    // rebaselineStartupSnapshots().
    m_startupRenderingBackend = settings.renderingBackend();
    m_startupGpuDevice = settings.gpuDevice();

    // One sysfs + pci.ids enumeration per process: the device set only
    // changes on hotplug, which this page deliberately does not track (see
    // the availableGpus property doc in the header).
    m_enumeratedGpus = GpuDeviceList::enumerate();
    rebuildAvailableGpus();

    // An external write (profile switch, D-Bus SetSetting, config import) can
    // point the stored value at a device absent from the enumerated list.
    // Rebuild so the synthetic "Unavailable device" row tracks the CURRENT
    // value rather than the construction-time one — and disappears again once
    // the stored value names a present device.
    connect(&settings, &ISettings::gpuDeviceChanged, this, &GeneralPageController::rebuildAvailableGpus);
}

void GeneralPageController::rebuildAvailableGpus()
{
    QVariantList gpus;
    QVariantMap autoEntry;
    autoEntry.insert(QStringLiteral("text"), PhosphorI18n::tr("Automatic"));
    autoEntry.insert(QStringLiteral("value"), ConfigDefaults::gpuDevice());
    gpus.append(autoEntry);
    gpus.append(m_enumeratedGpus);

    // Keep a persisted selection displayable even when the device is gone.
    // The QML combo binds storedValue to appSettings.gpuDevice verbatim, so
    // the synthetic row deliberately re-reads the same getter rather than
    // deriving (or normalizing) a value: the row can only be selected if it
    // byte-matches whatever the getter returns. Normalization is the schema
    // validator's job on the write path.
    const QString stored = m_settings.gpuDevice();
    const bool storedPresent = std::any_of(gpus.cbegin(), gpus.cend(), [&](const QVariant& v) {
        return v.toMap().value(QStringLiteral("value")).toString() == stored;
    });
    if (!storedPresent) {
        QVariantMap missing;
        missing.insert(QStringLiteral("text"), PhosphorI18n::tr("Unavailable device (%1)").arg(stored));
        missing.insert(QStringLiteral("value"), stored);
        gpus.append(missing);
    }

    if (gpus != m_availableGpus) {
        m_availableGpus = gpus;
        Q_EMIT availableGpusChanged();
    }
}

void GeneralPageController::rebaselineStartupSnapshots()
{
    const QString backend = m_settings.renderingBackend();
    const QString gpu = m_settings.gpuDevice();
    if (backend == m_startupRenderingBackend && gpu == m_startupGpuDevice) {
        return;
    }
    m_startupRenderingBackend = backend;
    m_startupGpuDevice = gpu;
    Q_EMIT startupSnapshotsChanged();
}

} // namespace PlasmaZones
