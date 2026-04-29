// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTileEngine/AutotileConfig.h>

namespace PlasmaZones {

// Every concrete setter implementation now lives in settings.cpp and routes
// through PhosphorConfig::Store. The macro-generated boilerplate this file
// used to carry is gone; only the Virtual screen config setters below remain
// because the QHash<QString, Phosphor::Screens::VirtualScreenConfig> shape doesn't fit the
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

QHash<QString, Phosphor::Screens::VirtualScreenConfig> Settings::virtualScreenConfigs() const
{
    return m_virtualScreenConfigs;
}

void Settings::setVirtualScreenConfigs(const QHash<QString, Phosphor::Screens::VirtualScreenConfig>& configs)
{
    // Filter out 1-screen configs: hasSubdivisions() returns false for size==1,
    // so effectiveScreenIds() would not emit virtual IDs for them, but storing them
    // causes inconsistency (settings says VS exists, Phosphor::Screens::ScreenManager disagrees).
    // Also reject individually-invalid entries via Phosphor::Screens::VirtualScreenConfig::isValid
    // — Settings is the source of truth, so it must apply the same admission
    // rules as the singular setVirtualScreenConfig path.
    QHash<QString, Phosphor::Screens::VirtualScreenConfig> filtered;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        if (!it.value().hasSubdivisions()) {
            continue;
        }
        QString error;
        if (!Phosphor::Screens::VirtualScreenConfig::isValid(it.value(), it.key(),
                                                             ConfigDefaults::maxVirtualScreensPerPhysical(), &error)) {
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
        // approxEqual tolerates JSON round-trip float drift so a config
        // loaded from disk compares equal to the freshly-saved in-memory
        // version. Exact operator== would re-emit virtualScreenConfigsChanged
        // on every boot for every screen.
        if (existing == m_virtualScreenConfigs.constEnd() || !existing.value().approxEqual(it.value())) {
            m_virtualScreenConfigs = filtered;
            Q_EMIT virtualScreenConfigsChanged();
            Q_EMIT settingsChanged();
            return;
        }
    }
}

bool Settings::setVirtualScreenConfig(const QString& physicalScreenId,
                                      const Phosphor::Screens::VirtualScreenConfig& config)
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
        // Phosphor::Screens::ScreenManager. Otherwise Settings and Phosphor::Screens::ScreenManager diverge in
        // memory and the disk save persists garbage that next-load drops.
        QString error;
        if (!Phosphor::Screens::VirtualScreenConfig::isValid(config, physicalScreenId,
                                                             ConfigDefaults::maxVirtualScreensPerPhysical(), &error)) {
            qCWarning(lcConfig) << "setVirtualScreenConfig: rejected invalid config for" << physicalScreenId << "—"
                                << error;
            return false;
        }
        // approxEqual: tolerance-aware skip-gate for JSON-roundtripped
        // configs. Same reasoning as the setVirtualScreenConfigs bulk path.
        if (m_virtualScreenConfigs.value(physicalScreenId).approxEqual(config))
            return true; // unchanged is a successful no-op
        m_virtualScreenConfigs.insert(physicalScreenId, config);
    }
    Q_EMIT virtualScreenConfigsChanged();
    Q_EMIT settingsChanged();
    return true;
}

Phosphor::Screens::VirtualScreenConfig Settings::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.value(physicalScreenId);
}

bool Settings::renameVirtualScreenConfig(const QString& oldPhysicalScreenId, const QString& newPhysicalScreenId)
{
    if (oldPhysicalScreenId.isEmpty() || newPhysicalScreenId.isEmpty() || oldPhysicalScreenId == newPhysicalScreenId) {
        return true;
    }
    auto it = m_virtualScreenConfigs.constFind(oldPhysicalScreenId);
    if (it == m_virtualScreenConfigs.constEnd()) {
        return true; // nothing to migrate
    }
    // Rewrite the config so every def's physicalScreenId + id reflects the
    // new key. VirtualScreenId::make derives ids from the physical id, so
    // a bare move under the new key without this rewrite would leave stale
    // "oldId/vs:N" ids inside the persisted record.
    Phosphor::Screens::VirtualScreenConfig migrated = it.value();
    migrated.physicalScreenId = newPhysicalScreenId;
    for (auto& def : migrated.screens) {
        def.physicalScreenId = newPhysicalScreenId;
        def.id = PhosphorIdentity::VirtualScreenId::make(newPhysicalScreenId, def.index);
    }
    m_virtualScreenConfigs.remove(oldPhysicalScreenId);
    m_virtualScreenConfigs.insert(newPhysicalScreenId, migrated);
    qCInfo(lcConfig) << "VirtualScreen config migrated:" << oldPhysicalScreenId << "→" << newPhysicalScreenId;
    Q_EMIT virtualScreenConfigsChanged();
    Q_EMIT settingsChanged();
    return true;
}

} // namespace PlasmaZones
