// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include <QList>
#include <QSet>
#include <QUuid>

namespace PlasmaZones {

/// Strip any stale MANAGED appearance baseline rules from @p store. Window
/// appearance defaults live in the config store now, so a managed rule carrying
/// one of the three fixed baseline ids (ConfigDefaults::managedAppearanceBaselineIds)
/// is a leftover an older build wrote to rules.json; drop it, preserving every
/// user rule. Persists (setAllRules) only when something was removed. Returns
/// false ONLY when a removal happened AND the persist failed, so a caller can
/// `if (!stripStaleManagedAppearanceBaselines(store)) qCWarning(...)`. Shared by
/// the daemon's startup cleanup and the D-Bus Restore Defaults path so the two
/// can't drift.
inline bool stripStaleManagedAppearanceBaselines(PhosphorRules::RuleStore& store)
{
    const QSet<QUuid> staleBaselineIds = ConfigDefaults::managedAppearanceBaselineIds();
    const QList<PhosphorRules::Rule>& currentRules = store.ruleSet().rules();
    QList<PhosphorRules::Rule> keptRules;
    keptRules.reserve(currentRules.size());
    bool removedAny = false;
    for (const PhosphorRules::Rule& rule : currentRules) {
        if (rule.managed && staleBaselineIds.contains(rule.id)) {
            removedAny = true;
            continue;
        }
        keptRules.append(rule);
    }
    return !removedAny || store.setAllRules(keptRules);
}

} // namespace PlasmaZones
