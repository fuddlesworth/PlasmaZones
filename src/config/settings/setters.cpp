// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../../autotile/AutotileConfig.h"

namespace PlasmaZones {

// Every concrete setter implementation now lives in settings.cpp and routes
// through PhosphorConfig::Store. The macro-generated boilerplate this file
// used to carry is gone; only the Virtual screen config setters below remain
// because the QHash<QString, VirtualScreenConfig> shape doesn't fit the
// Store's scalar-key-per-setting model — they persist via a per-screen
// "VirtualScreen:<id>" group structure that saveVirtualScreenConfigs
// handles directly.

// ═══════════════════════════════════════════════════════════════════════════════
// Rendering — setter moved to settings.cpp (PhosphorConfig::Store-backed).
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Effects — setters live in settings.cpp and route through
// PhosphorConfig::Store (m_store). See settingsschema.cpp for the schema
// and settings.cpp for the setter implementations.

// ═══════════════════════════════════════════════════════════════════════════════
// Shortcut setters
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual screen config setters
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, VirtualScreenConfig> Settings::virtualScreenConfigs() const
{
    return m_virtualScreenConfigs;
}

void Settings::setVirtualScreenConfigs(const QHash<QString, VirtualScreenConfig>& configs)
{
    // Filter out 1-screen configs: hasSubdivisions() returns false for size==1,
    // so effectiveScreenIds() would not emit virtual IDs for them, but storing them
    // causes inconsistency (settings says VS exists, ScreenManager disagrees).
    // Also reject individually-invalid entries via VirtualScreenConfig::isValid
    // — Settings is the source of truth, so it must apply the same admission
    // rules as the singular setVirtualScreenConfig path.
    QHash<QString, VirtualScreenConfig> filtered;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        if (!it.value().hasSubdivisions()) {
            continue;
        }
        QString error;
        if (!VirtualScreenConfig::isValid(it.value(), it.key(), ConfigDefaults::maxVirtualScreensPerPhysical(),
                                          &error)) {
            qCWarning(lcConfig) << "setVirtualScreenConfigs: dropping invalid entry for" << it.key() << "—" << error;
            continue;
        }
        filtered.insert(it.key(), it.value());
    }

    // Check exact equality to avoid dropping tiny geometry adjustments
    if (m_virtualScreenConfigs.size() != filtered.size()) {
        m_virtualScreenConfigs = filtered;
        Q_EMIT virtualScreenConfigsChanged();
        Q_EMIT settingsChanged();
        return;
    }
    for (auto it = filtered.constBegin(); it != filtered.constEnd(); ++it) {
        auto existing = m_virtualScreenConfigs.constFind(it.key());
        if (existing == m_virtualScreenConfigs.constEnd() || !(existing.value() == it.value())) {
            m_virtualScreenConfigs = filtered;
            Q_EMIT virtualScreenConfigsChanged();
            Q_EMIT settingsChanged();
            return;
        }
    }
}

bool Settings::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcConfig) << "setVirtualScreenConfig: empty physicalScreenId";
        return false;
    }

    if (config.screens.isEmpty() || !config.hasSubdivisions()) {
        if (!m_virtualScreenConfigs.contains(physicalScreenId))
            return true; // already-empty removal is a successful no-op
        m_virtualScreenConfigs.remove(physicalScreenId);
    } else {
        // Validate before storing — Settings is the source of truth for VS
        // configs, so it must reject inputs that would later be refused by
        // ScreenManager. Otherwise Settings and ScreenManager diverge in
        // memory and the disk save persists garbage that next-load drops.
        QString error;
        if (!VirtualScreenConfig::isValid(config, physicalScreenId, ConfigDefaults::maxVirtualScreensPerPhysical(),
                                          &error)) {
            qCWarning(lcConfig) << "setVirtualScreenConfig: rejected invalid config for" << physicalScreenId << "—"
                                << error;
            return false;
        }
        if (m_virtualScreenConfigs.value(physicalScreenId) == config)
            return true; // unchanged is a successful no-op
        m_virtualScreenConfigs.insert(physicalScreenId, config);
    }
    Q_EMIT virtualScreenConfigsChanged();
    Q_EMIT settingsChanged();
    return true;
}

VirtualScreenConfig Settings::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.value(physicalScreenId);
}

} // namespace PlasmaZones
