// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"
#include "configmigration_util.h"
#include "configmigration_v4detail.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/QSettingsBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/IdentityKey.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <array>
#include <atomic>
#include <optional>
#include <string_view>

namespace PlasmaZones {

// ── Schema migration: v3 → v4 ───────────────────────────────────────────────
// Window-rule consolidation — Phase 3.
//
// The v4 conversion produces the new rules.json store: every zone
// Assignment, per-mode disable entry, animation app rule, exclusion list
// entry, AND animation exclusion list entry becomes a Rule.
// rules.json SUPERSEDES assignments.json and the config.json
// Display.*Disabled* / Exclusions.* / Animations.AnimationAppRules /
// Animations.WindowFiltering.{Applications,WindowClasses} keys — the
// runtime LayoutRegistry, Settings, SnapEngine and effect now read the
// rule store exclusively, so the v3 inputs are removed once converted.
//
// Each migration step has signature `void(QJsonObject&)` — it can only touch
// config.json. This step REMOVES the six Display.*Disabled* keys, the
// Animations.AnimationAppRules array, both Exclusions.{Applications,
// WindowClasses} leaf keys, and both Animations.WindowFiltering.{Applications,
// WindowClasses} leaf keys. The values are stashed into four temporary
// root keys (`_v4DisableStash`, `_v4AnimationRulesStash`, `_v4ExclusionStash`,
// `_v4AnimationExclusionStash`) for finalizeV4Conversion to consume,
// and stamps `_version = 4`. finalizeV4Conversion (a post-chain step)
// reads that stash + assignments.json, writes rules.json, then deletes
// assignments.json as the irreversible commit.

void ConfigMigration::migrateV3ToV4(QJsonObject& root)
{
    // Schema-version-migration freeze policy: this function reads the v3
    // on-disk shape. All group/key accessors used here MUST be the frozen
    // `ConfigKeys::Legacy::v3*` accessors, NEVER the live ConfigDefaults
    // accessors. A future runtime rename of the live accessor would silently
    // retarget this migration to a path no v3 config ever had on disk; the
    // freeze decouples the migration's stable wire-format contract from the
    // live schema. See the comment block on `v4AnimationsGroup` in
    // configkeys.h for the same rationale applied to v4.
    //
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 4) {
        return;
    }

    QJsonObject display = root.value(ConfigKeys::Legacy::v3DisplayGroup()).toObject();

    // Move the disable-list values out of config.json: stash the value, then
    // REMOVE the key. rules.json supersedes them — the runtime Settings
    // layer reads DisableEngine rules from the store now, never these keys.
    // finalizeV4Conversion consumes the stash and writes the rules.
    QJsonObject stash;
    const auto moveDisableKey = [&display, &stash](const QString& configKey, QLatin1StringView stashKey) {
        // Only stash a value when the v3 key actually carried one — an absent
        // or empty disable list contributes no rules, so a stash entry for it
        // would just be inert noise finalizeV4Conversion has to skip.
        //
        // Surface non-string disk values (hand-edit / disk-corruption /
        // doubly-migrated remnants) before we discard them. `toString()`
        // silently returns "" for numbers / nulls / arrays / bools so the
        // stash-then-remove path would otherwise drop the data without a
        // trace. Mirrors the qWarning in migrateV2ToV3::takeKey.
        const QJsonValue raw = display.value(configKey);
        if (display.contains(configKey) && !raw.isString() && !raw.isNull() && !raw.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV3ToV4: discarding non-string value at Display.%s "
                "(type=%d) — only string disable-lists are migrated.",
                qPrintable(configKey), static_cast<int>(raw.type()));
        }
        const QString value = raw.toString();
        if (!value.isEmpty()) {
            stash.insert(stashKey, value);
        }
        display.remove(configKey);
    };
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledMonitorsKey(), kStashSnappingMonitorsField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledMonitorsKey(), kStashAutotileMonitorsField);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledDesktopsKey(), kStashSnappingDesktopsField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledDesktopsKey(), kStashAutotileDesktopsField);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledActivitiesKey(), kStashSnappingActivitiesField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledActivitiesKey(), kStashAutotileActivitiesField);

    // Write the stripped Display group back; drop it entirely if now empty so
    // no husk object lingers.
    if (display.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v3DisplayGroup());
    } else {
        root[ConfigKeys::Legacy::v3DisplayGroup()] = display;
    }

    // ── Stash hand-off to finalizeV4Conversion ─────────────────────────────
    // Both v4 stashes follow the same shape: persist the key only when
    // there's at least one entry to carry forward, so a clean v3 config
    // doesn't leave inert empty stashes for the finalizer to strip. The
    // finalizer reads a missing key as an empty input and no-ops on that
    // axis. The Animations group itself stays on disk when non-empty
    // (ShaderProfileTree still lives under it).
    //
    // Disable-list stash: each `moveDisableKey` above already skipped empty
    // values, so an empty `stash` here means the v3 config had no disable
    // lists at all.
    //
    // Re-entry note: the v3-version-gate at the top of this function
    // (`if (root[versionKey].toInt(0) >= 4) return;`) means a fully-stamped
    // v4 config never re-enters here, so the `stash` we build is for THIS
    // run only. A user who hand-edits their v3 config to re-add a
    // Display.*Disabled* key after a partial earlier run would land back
    // here, and the `root[kV4DisableStashKey()] = stash;` overwrite is
    // intentional: the prior stash represented the prior on-disk state,
    // and the user's hand-edit is the new authoritative input. Merging
    // would carry forward values the user just removed from disk. Spelt
    // out so a future maintainer doesn't "fix" this by accident.
    if (!stash.isEmpty()) {
        root[kV4DisableStashKey()] = stash;
    }

    // Animation App Rule stash: v4 folds per-window animation overrides into
    // the unified rules.json store as `OverrideAnimationShader` /
    // `OverrideAnimationTiming` actions on a `WindowClass Contains <pattern>`
    // matcher. finalizeV4Conversion ports the bridge logic against the
    // stashed JSON and appends the resulting Rules to the same rule
    // set assignments/disable lists feed.
    QJsonObject animations = root.value(ConfigKeys::Legacy::v4AnimationsGroup()).toObject();
    const QJsonValue rawAnimationRules = animations.value(ConfigKeys::Legacy::v4AnimationAppRulesKey());
    // Surface a non-array AnimationAppRules value before discarding it,
    // matching the moveDisableKey diagnostic above. Without this log, a
    // hand-edited or disk-corrupted value silently vanishes during
    // migration (toArray() returns empty for non-array QJsonValues).
    if (animations.contains(ConfigKeys::Legacy::v4AnimationAppRulesKey()) && !rawAnimationRules.isArray()
        && !rawAnimationRules.isNull() && !rawAnimationRules.isUndefined()) {
        qWarning(
            "ConfigMigration::migrateV3ToV4: discarding non-array value at Animations.%s "
            "(type=%d) — only arrays of animation app rules are migrated.",
            qPrintable(ConfigKeys::Legacy::v4AnimationAppRulesKey()), static_cast<int>(rawAnimationRules.type()));
    }
    const QJsonArray animationRules = rawAnimationRules.toArray();
    if (!animationRules.isEmpty()) {
        root[kV4AnimationRulesStashKey()] = animationRules;
    }
    animations.remove(ConfigKeys::Legacy::v4AnimationAppRulesKey());
    if (animations.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v4AnimationsGroup());
    } else {
        root[ConfigKeys::Legacy::v4AnimationsGroup()] = animations;
    }

    // Exclusions stash: the legacy `Exclusions.Applications` and
    // `Exclusions.WindowClasses` keys hold comma-joined pattern lists that the
    // runtime previously folded into terminal `Exclude` rules at evaluation
    // time via the (now-deleted) legacy bridge — see git history for
    // `ExclusionListBridge` if forensics on the pre-v4 builder are needed.
    // v4 promotes those into
    // first-class Rules: finalizeV4Conversion appends one
    // `AppId AppIdMatches <pattern> → Exclude` rule per surviving pattern to
    // rules.json, so the daemon's runtime exclusion behaviour for an
    // upgrading user does not change. Read both raw values, surface non-string
    // disk values (same diagnostic shape as the moveDisableKey logger above),
    // strip the keys, and drop the group entirely if it's now empty.
    // Single helper for both the snapping-side and animation-side exclusion
    // list stash drains — same shape, same diagnostic on non-string disk
    // values, same skip-on-empty. The `sourceLabel` argument is the dotted
    // group path the warning text uses for operator forensics; the rest is
    // mechanical. Takes `groupSrc`/`stashDst` by reference so the helper
    // works as either a free read-modify-write on the source group OR (in
    // the dot-path animation case) on the nested object the caller already
    // extracted.
    const auto stashListEntry = [](QJsonObject& groupSrc, QJsonObject& stashDst, const QString& configKey,
                                   const char* sourceLabel) {
        const QJsonValue raw = groupSrc.value(configKey);
        if (groupSrc.contains(configKey) && !raw.isString() && !raw.isNull() && !raw.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV3ToV4: discarding non-string value at %s.%s "
                "(type=%d) — only comma-joined string pattern lists are migrated.",
                sourceLabel, qPrintable(configKey), static_cast<int>(raw.type()));
        }
        const QString value = raw.toString();
        if (!value.isEmpty()) {
            // Field name = source-key name (Applications / WindowClasses).
            // finalizeV4Conversion uses the field name to choose the rule's
            // match-leaf field (DesktopFile / WindowClass for the animation
            // fold; both feed AppId for the snapping fold).
            stashDst.insert(configKey, value);
        }
        groupSrc.remove(configKey);
    };

    QJsonObject exclusions = root.value(ConfigKeys::Legacy::v3ExclusionsGroup()).toObject();
    QJsonObject exclusionStash;
    stashListEntry(exclusions, exclusionStash, ConfigKeys::Legacy::v3ExcludedApplicationsKey(), "Exclusions");
    stashListEntry(exclusions, exclusionStash, ConfigKeys::Legacy::v3ExcludedWindowClassesKey(), "Exclusions");
    if (exclusions.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v3ExclusionsGroup());
    } else {
        root[ConfigKeys::Legacy::v3ExclusionsGroup()] = exclusions;
    }
    if (!exclusionStash.isEmpty()) {
        root[kV4ExclusionStashKey()] = exclusionStash;
    }

    // Animation exclusion stash: the legacy
    // `Animations.WindowFiltering.{Applications,WindowClasses}` lists
    // historically fed the effect's `m_animationExclusionRuleSet` via the
    // bridge's `Contains`-leaf builder. v4 promotes those into first-class
    // `ExcludeAnimations` Rules so the effect can drop both the
    // QStringList settings and the per-effect rebuild. Same shape as the
    // snapping-side stash above — read raw, surface non-string disk
    // values, strip the keys, and drop the (dot-path) group if it's now
    // empty. The "Animations" parent segment routes through
    // `Legacy::v4AnimationsGroup()` and the "WindowFiltering" leaf segment
    // routes through `Legacy::v4WindowFilteringSegment()` so this block
    // stays in lockstep with the AnimationAppRules block above; a future
    // rename of either frozen accessor flows through every site.
    QJsonObject animationsForFiltering = root.value(ConfigKeys::Legacy::v4AnimationsGroup()).toObject();
    QJsonObject animationFiltering =
        animationsForFiltering.value(ConfigKeys::Legacy::v4WindowFilteringSegment()).toObject();
    QJsonObject animationExclusionStash;
    stashListEntry(animationFiltering, animationExclusionStash, ConfigKeys::Legacy::v3ExcludedApplicationsKey(),
                   "Animations.WindowFiltering");
    stashListEntry(animationFiltering, animationExclusionStash, ConfigKeys::Legacy::v3ExcludedWindowClassesKey(),
                   "Animations.WindowFiltering");
    if (animationFiltering.isEmpty()) {
        animationsForFiltering.remove(ConfigKeys::Legacy::v4WindowFilteringSegment());
    } else {
        animationsForFiltering[ConfigKeys::Legacy::v4WindowFilteringSegment()] = animationFiltering;
    }
    if (animationsForFiltering.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v4AnimationsGroup());
    } else {
        root[ConfigKeys::Legacy::v4AnimationsGroup()] = animationsForFiltering;
    }
    if (!animationExclusionStash.isEmpty()) {
        root[kV4AnimationExclusionStashKey()] = animationExclusionStash;
    }

    // v3.1 renamed the "Snapping › Appearance" page to "Zones" (it configures the
    // drag-time zone overlay). Move its config groups Snapping.Appearance.* ->
    // Snapping.Zones.* so the freed Snapping.Appearance.* namespace can hold the
    // new snapped-window appearance settings (mirroring Tiling.Appearance.*).
    // Both source and destination use FROZEN Legacy accessors — never the live
    // ConfigDefaults::snappingZones*Group() accessors — so a future rename of
    // those live group names can't silently retarget this historical step to a
    // path no migrated config ever produced (same freeze policy as v2→v3).
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceColorsGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesColorsGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceOpacityGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesOpacityGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceBorderGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesBorderGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceLabelsGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesLabelsGroup());

    // Stamp literal 4 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 4;
}

} // namespace PlasmaZones
