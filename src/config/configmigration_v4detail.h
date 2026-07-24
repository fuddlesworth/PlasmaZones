// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared internals of the v4 conversion, split across three TUs:
//   - migrateV3ToV4 (configmigration_v4.cpp) WRITES the four `_v4*Stash` root
//     keys + the disable-list stash fields;
//   - finalizeV4Conversion (configmigration_v4finalize.cpp) READS them;
//   - the rule-builder helpers (configmigration_v4rules.cpp) are called by the
//     finalizer.
// The stash-key aliases and stash field-name constants live here so a rename
// can't desync the writer and reader. The rule-builder + assignments-file
// helper declarations expose the cross-TU symbols; their definitions live in
// configmigration_v4rules.cpp (builders) and configmigration.cpp (the
// assignments.json lifecycle helpers).

#pragma once

#include "configkeys.h"

#include <PhosphorRules/Rule.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QList>
#include <QString>

namespace PlasmaZones {

// ── Temporary `_v4*Stash` root keys ────────────────────────────────────────
// The temporary root keys migrateV3ToV4 writes and finalizeV4Conversion
// consumes + strips. Not real schema keys — they never survive a completed
// conversion. Aliased to the frozen accessors in `ConfigKeys::Legacy` so
// settings.cpp::purgeStaleKeys (which preserves them across save() cycles
// when the chain stalls) reads from the same SSoT.
inline QString kV4DisableStashKey()
{
    return ConfigKeys::Legacy::v4DisableStashKey();
}
// Carries the v4 `Animations.AnimationAppRules` array forward to
// finalizeV4Conversion, which converts each legacy entry into a Rule and
// appends it to rules.json. The source key is removed from the
// Animations group in migrateV3ToV4 so the unified rule store becomes the sole
// home for per-window animation overrides.
inline QString kV4AnimationRulesStashKey()
{
    return ConfigKeys::Legacy::v4AnimationRulesStashKey();
}
// Sibling aliases for the v4 exclusion stashes added when the v3 Exclusions /
// Animations.WindowFiltering lists were folded into Exclude / ExcludeAnimations
// Rules. Same alias-policy as the two above: every stash key reads
// through a short shim in this TU so the call sites stay symmetric and the
// purgeStaleKeys preservation list still resolves through ConfigKeys::Legacy.
inline QString kV4ExclusionStashKey()
{
    return ConfigKeys::Legacy::v4ExclusionStashKey();
}
inline QString kV4AnimationExclusionStashKey()
{
    return ConfigKeys::Legacy::v4AnimationExclusionStashKey();
}

// Inner field names inside the `_v4DisableStash` object. Shared between the
// writer (migrateV3ToV4) and the reader (finalizeV4Conversion) so a typo or
// rename can't silently drop a disable list on conversion.
//
// Naming note: these are field names inside the v4 stash object, not v4 wire
// keys themselves — the "v3"-shaped data they describe (snapping vs.
// autotile, monitors vs. desktops vs. activities) is what gives the
// constants their names. The stash itself is a v4 artifact that bridges the
// chain step to the finalizer.
constexpr QLatin1StringView kStashSnappingMonitorsField{"snappingMonitors"};
constexpr QLatin1StringView kStashAutotileMonitorsField{"autotileMonitors"};
constexpr QLatin1StringView kStashSnappingDesktopsField{"snappingDesktops"};
constexpr QLatin1StringView kStashAutotileDesktopsField{"autotileDesktops"};
constexpr QLatin1StringView kStashSnappingActivitiesField{"snappingActivities"};
constexpr QLatin1StringView kStashAutotileActivitiesField{"autotileActivities"};

// ── assignments.json / rules.json lifecycle helpers ────────────────────────
// Defined in configmigration.cpp (they sit on the ensureJsonConfig hot path)
// but also called by migrateV1ToV2 (writes the v2 assignments.json artifact)
// and finalizeV4Conversion (reads + retires it).
QString legacyAssignmentsFilePath();
bool prevalidateLegacyAssignmentsFile(const QString& assignmentsPath);
bool prevalidateRulesFile(const QString& rulesPath);
void retireLegacyAssignmentsFile(const QString& assignmentsPath);

// ── Rule-builder helpers ───────────────────────────────────────────────────
// Defined in configmigration_v4rules.cpp; called by finalizeV4Conversion to
// assemble the migrated RuleSet.
void assignBandPrioritiesToZeroRules(QList<PhosphorRules::Rule>& rules);
void appendAnimationRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonArray& stash);
void appendExclusionRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonObject& stash);
void appendSteamDefaultRule(QList<PhosphorRules::Rule>& rules);
void appendAnimationExclusionRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonObject& stash);
void appendLayoutAppRulesAsSnapToZone(QList<PhosphorRules::Rule>& rules, const QString& layoutsDir);

} // namespace PlasmaZones
