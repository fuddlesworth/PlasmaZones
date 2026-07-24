// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"

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

// ── Schema migration: v2 → v3 ───────────────────────────────────────────────
// Splits the single "this monitor / desktop / activity is disabled in PlasmaZones"
// list into independent per-mode lists, and relocates them out of
// Snapping.Behavior.Display (which historically gated both modes despite the
// snapping-prefixed group name) into a mode-neutral Display group.
//
// Migration semantics: every entry in a v2 disabled list is copied into BOTH
// the snapping and autotile lists in v3. Rationale: v2 didn't distinguish
// modes, so the only safe interpretation of "the user disabled monitor X" is
// "the user wanted X off in PlasmaZones, period" — preserve that intent in
// both modes until the user explicitly re-enables one in the new UI.
//
// Side note: the v2 keys ShowOnAllMonitors and FilterByAspectRatio remain
// in Snapping.Behavior.Display untouched — only the three Disabled* keys move.

void ConfigMigration::migrateV2ToV3(QJsonObject& root)
{
    // Defense-in-depth idempotency guard, mirroring migrateV1ToV2. The
    // PhosphorConfig::MigrationRunner gates this on version == 2 and
    // `ensureJsonConfig` bails early when version >= ConfigSchemaVersion,
    // but a direct caller that hands us an already-v3 doc would otherwise
    // re-read v3-named groups as if they were v2 candidates. The v2→v3
    // step is largely empty-tolerant (each takeKey returns empty for
    // absent keys), but the asymmetry vs v1→v2's guard is a foot-gun.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 3) {
        return;
    }

    // Walk the canonical v2 dot-path Snapping.Behavior.Display by splitting
    // the FROZEN v2 accessor on '.' — this keeps the migration in lockstep
    // with the schema instead of duplicating segment names as bare literals.
    // Using the LIVE `snappingBehaviorDisplayGroup()` here would silently
    // retarget the v2→v3 step to whatever path a future rename of that
    // accessor points to, which by definition isn't where v2 configs ever
    // wrote. This mirrors the same freeze policy applied at the v3→v4 step
    // and at the v3-write site below.
    const QStringList v2GroupSegments =
        ConfigKeys::Legacy::v2SnappingBehaviorDisplayGroup().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    // Frozen-literal invariant: `"Snapping.Behavior.Display"` is exactly three
    // dot-segments. Q_ASSERT documents the contract at the test bench; the
    // release-build guard below prevents an out-of-bounds segment access if a
    // future freeze-policy violation ever lands the literal at a different
    // shape. Without the runtime guard, the `v2GroupSegments[0..2]` indexing
    // would be UB in release builds — the Q_ASSERT alone is asymmetric
    // coverage.
    Q_ASSERT(v2GroupSegments.size() == 3);
    if (v2GroupSegments.size() != 3) {
        qCritical("migrateV2ToV3: frozen v2 group accessor split into %lld segments (expected 3) — aborting step",
                  static_cast<long long>(v2GroupSegments.size()));
        return;
    }
    const QString& snappingSeg = v2GroupSegments[0];
    const QString& behaviorSeg = v2GroupSegments[1];
    const QString& displaySeg = v2GroupSegments[2];

    QJsonObject snapping = root.value(snappingSeg).toObject();
    QJsonObject behavior = snapping.value(behaviorSeg).toObject();
    QJsonObject v2Display = behavior.value(displaySeg).toObject();

    // takeKey: read the v2 string value at @p key, drop the key from @p obj
    // unconditionally if present (even when the value isn't a string — we
    // don't want a hand-edited array or null lingering past the migration
    // and looking like live v2 data on a v3-stamped config), and return
    // the string representation when one is available. Logs a warning
    // when a non-string value is erased so a user reviewing the log can
    // recover their hand-edit by hand.
    auto takeKey = [](QJsonObject& obj, const QString& key) -> QString {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            return {};
        }
        const QJsonValue v = it.value();
        QString result;
        if (v.isString()) {
            result = v.toString();
        } else if (!v.isNull() && !v.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV2ToV3: discarding non-string value at v2 key %s — hand-edited "
                "values that don't match the v2 wire format do not survive migration",
                qPrintable(key));
        }
        obj.erase(it);
        return result;
    };

    const QString v2Monitors = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledMonitorsKey());
    const QString v2Desktops = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledDesktopsKey());
    const QString v2Activities = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledActivitiesKey());

    // Write the duplicated lists into the new Display group. Skip empties so
    // a clean v2 config with no disabled entries doesn't grow noise keys.
    // Group name must come from the FROZEN v3 accessor — `displayGroup()`
    // is the LIVE accessor that follows future renames, so using it here
    // would silently retarget the v2→v3 step to a path no v3 config ever
    // had on disk after a future rename. The v3→v4 step downstream
    // (`migrateV3ToV4`) already uses `Legacy::v3DisplayGroup()` for the
    // same reason; this site was the outlier.
    QJsonObject v3Display = root.value(ConfigKeys::Legacy::v3DisplayGroup()).toObject();

    auto writeIfNonEmpty = [&v3Display](const QString& key, const QString& value) {
        if (!value.isEmpty()) {
            v3Display[key] = value;
        }
    };

    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledActivitiesKey(), v2Activities);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledActivitiesKey(), v2Activities);

    // Stitch the trimmed v2 Display object back into Snapping.Behavior, drop
    // the Display sub-object entirely if it became empty (no ShowOnAllMonitors
    // / FilterByAspectRatio either). Same for Snapping.Behavior itself.
    if (v2Display.isEmpty()) {
        behavior.remove(displaySeg);
    } else {
        behavior[displaySeg] = v2Display;
    }
    if (behavior.isEmpty()) {
        snapping.remove(behaviorSeg);
    } else {
        snapping[behaviorSeg] = behavior;
    }
    if (snapping.isEmpty()) {
        root.remove(snappingSeg);
    } else {
        root[snappingSeg] = snapping;
    }

    if (!v3Display.isEmpty()) {
        // Use the FROZEN v3 accessor — see comment above on the matching
        // `Legacy::v3DisplayGroup()` read for why the live accessor would
        // break the chain.
        root[ConfigKeys::Legacy::v3DisplayGroup()] = v3Display;
    }

    // Stamp literal 3 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 3;
}

} // namespace PlasmaZones
