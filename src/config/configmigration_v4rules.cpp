// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"
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

namespace {

// ─── Animation App Rule → Rule conversion ─────────────────────────────
// Ports the (now-deleted) PhosphorRules::AnimationAppRuleBridge logic
// against the raw stash JSON. The legacy AnimationAppRule type is gone in v4+,
// so the conversion lives here — the migration is its sole remaining caller.

/// Fixed v5-UUID namespace for animation App-Rule identities. Inherited
/// verbatim from the legacy bridge so the migration's output is byte-stable
/// across re-runs (idempotent rule-id derivation).
const QUuid& animationAppRuleNamespaceUuid()
{
    static const QUuid ns(QStringLiteral("{b3f2c1a0-7d4e-5f6a-8b9c-0d1e2f3a4b5c}"));
    return ns;
}

// Legacy AnimationAppRule wire keys. The v3 on-disk format is frozen — the
// runtime accessors are deleted, so these literals are the migration's last
// reader of the shape. File-scope so the validate/build split below can share
// them without re-declaring.
constexpr QLatin1StringView kKeyClassPattern{"classPattern"};
constexpr QLatin1StringView kKeyEventPath{"eventPath"};
constexpr QLatin1StringView kKeyKind{"kind"};
constexpr QLatin1StringView kKeyEffectId{"effectId"};
constexpr QLatin1StringView kKeyShaderParams{"shaderParams"};
constexpr QLatin1StringView kKeyCurve{"curve"};
constexpr QLatin1StringView kKeyDurationMs{"durationMs"};
constexpr QLatin1StringView kKindShader{"shader"};
constexpr QLatin1StringView kKindTiming{"timing"};

/// True if @p source carries the minimum fields a non-discarded legacy
/// AnimationAppRule must have (non-empty classPattern + eventPath, and a
/// recognised `kind`). Mirrors the legacy rule-level loader's
/// drop-on-malformed contract: this is the predicate the caller uses to
/// filter the stash BEFORE assigning priorities, so dropped entries don't
/// leave gaps in the priority sequence the way they would if we filtered
/// in-loop with a pre-computed count.
bool isValidAnimationAppRuleSource(const QJsonObject& source)
{
    const QString classPattern = source.value(kKeyClassPattern).toString();
    const QString eventPath = source.value(kKeyEventPath).toString();
    if (classPattern.isEmpty() || eventPath.isEmpty()) {
        return false;
    }
    const QString kindStr = source.value(kKeyKind).toString();
    return kindStr.compare(kKindShader, Qt::CaseInsensitive) == 0
        || kindStr.compare(kKindTiming, Qt::CaseInsensitive) == 0;
}

/// Build a single Rule from a legacy AnimationAppRule JSON object
/// already known to pass @ref isValidAnimationAppRuleSource. The caller is
/// responsible for validating before calling; this function is total on
/// valid input.
///
/// @param i      zero-based index into the FILTERED (valid-only) source
///               list — used to derive `priority = count - i`.
/// @param count  total VALID source entries (priority floors at 1, reserving
///               0 for the unassigned-marker band that assignBandPrioritiesToZeroRules stamps).
PhosphorRules::Rule buildAnimationAppRule(const QJsonObject& source, int i, int count)
{
    namespace ActionParam = PhosphorRules::ActionParam;

    const QString classPattern = source.value(kKeyClassPattern).toString();
    const QString eventPath = source.value(kKeyEventPath).toString();
    const QString kindStr = source.value(kKeyKind).toString();
    const bool isShader = kindStr.compare(kKindShader, Qt::CaseInsensitive) == 0;

    PhosphorRules::RuleAction action;
    QJsonObject params;
    params.insert(ActionParam::Event, eventPath);
    if (isShader) {
        action.type = QString(PhosphorRules::ActionType::OverrideAnimationShader);
        // effectId is always written — the empty string is the engaged-blocking
        // sentinel ("disable shader for matching windows"), distinct from an
        // unfilled slot ("no rule matched").
        params.insert(ActionParam::EffectId, source.value(kKeyEffectId).toString());
        // shaderParams round-trip note: the legacy bridge funneled the inner
        // params through QVariantMap (JSON → AnimationAppRule → QVariantMap →
        // QJsonObject), which silently lossy-coerces edge-case numeric types.
        // The migration ports the object verbatim instead — strictly more
        // type-faithful, and the only observable difference is for inputs the
        // QVariantMap round-trip would have corrupted anyway. A non-object
        // shaderParams payload (stray array / scalar) drops the inner block
        // and logs a warning, matching the legacy loader's diagnostic.
        const QJsonValue rawParams = source.value(kKeyShaderParams);
        if (rawParams.isObject()) {
            const QJsonObject paramsObj = rawParams.toObject();
            if (!paramsObj.isEmpty()) {
                params.insert(ActionParam::Params, paramsObj);
            }
        } else if (!rawParams.isUndefined() && !rawParams.isNull()) {
            qWarning(
                "ConfigMigration: shaderParams for AnimationAppRule[%d] (classPattern=\"%s\") is not a JSON "
                "object — payload dropped",
                i, qPrintable(classPattern));
        }
    } else {
        action.type = QString(PhosphorRules::ActionType::OverrideAnimationTiming);
        const QString curve = source.value(kKeyCurve).toString();
        if (!curve.isEmpty()) {
            params.insert(ActionParam::Curve, curve);
        }
        // `durationMs <= 0` is the "inherit per-event default" sentinel —
        // omit the key entirely, mirroring AnimationAppRule::toJson. An
        // explicit `0` or negative value on disk falls into the same bucket
        // as an absent key.
        const int durationMs = source.value(kKeyDurationMs).toInt(0);
        if (durationMs > 0) {
            params.insert(ActionParam::DurationMs, durationMs);
        }
    }
    action.params = params;

    PhosphorRules::Rule rule;
    // Deterministic id from the source identity tuple so repeated migrations
    // yield byte-identical rules — keeps the conversion idempotent under
    // crash-and-retry. The third segment uses the canonical lowercase kind
    // ("shader" / "timing"); hand-edited uppercase input on disk still
    // produces the same id since the kind-string compare above is
    // case-insensitive.
    rule.id = QUuid::createUuidV5(animationAppRuleNamespaceUuid(),
                                  PhosphorRules::Detail::encodeSegment(classPattern)
                                      + PhosphorRules::Detail::encodeSegment(eventPath)
                                      + PhosphorRules::Detail::encodeSegment(isShader ? kKindShader : kKindTiming));
    rule.enabled = true;
    rule.priority = count - i;
    rule.match = PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::WindowClass,
                                                          PhosphorRules::Operator::Contains, classPattern);
    rule.actions.append(action);
    return rule;
}

/// Fixed v5-UUID namespace for migrated exclusion-rule identities. Pinned
/// here as the canonical SSoT since the helper layer's
/// `ExclusionRules::detail::namespaceUuid` retired alongside the legacy
/// list-builder. The constant is byte-identical to the one the legacy
/// runtime bridge used (deleted alongside the v4 fold — see git history
/// for `ExclusionListBridge`) so a daemon that bridge-built a rule from
/// the same `(field, op, pattern)` tuple at runtime (pre-v4) produces
/// the same id post-migration. Two consequences of the deterministic
/// derivation:
///   - within a single rebuild, two source patterns that derive the same
///     id collapse via `RuleSet::setRules`' id-dedup (so the snapping
///     fold of identical patterns across both v3 lists yields one rule);
///     cross-RUN idempotency is provided separately by the
///     `rulesAlreadyConverted` existence probe in the finalizer, which
///     refuses to rebuild once rules.json exists, so the rebuild path
///     never runs twice, and
///   - the LEGACY runtime bridge's id (pre-v4 daemons that built the
///     same rule from the same lists at runtime) matches the migration's
///     id, so a v4 store that somehow saw both producers stays consistent.
/// This is NOT a dedup against hand-authored rules: a user authoring an
/// `AppId AppIdMatches firefox → Exclude` rule through the Rules
/// page receives a fresh `QUuid::createUuid()` random id at allocation
/// time (rulecontroller.cpp / Rule default-constructed `id`),
/// not the deterministic UUIDv5 the migration derives. The migration's
/// rule and the user's rule will coexist as two distinct entries with
/// semantically-equivalent matches — the store dedups on id, not on
/// match-leaf identity. Acceptable: both rules resolve to Excluded for
/// the same window, so the user-visible behaviour is correct, just
/// slightly redundant; the picker shows two entries the user can
/// reconcile manually.
inline const QUuid& exclusionMigrationNamespace()
{
    static const QUuid ns(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
    return ns;
}

// The migrated exclusion-rule ids below are UUIDv5-derived from the integer
// values of the Field/Operator enumerators (encoded as decimal strings). Those
// integers are therefore a wire format: renumbering any of them silently
// changes every previously-migrated exclusion-rule id and breaks the
// collision-with-self idempotency guarantee — the same failure mode the
// ExcludeAnimations wire-string static_assert below guards against. MatchTypes.h
// already documents "keeping enum values stable across versions"; pin the exact
// values the derivation depends on so a renumber breaks the build instead.
static_assert(static_cast<int>(PhosphorRules::Field::AppId) == 0
                  && static_cast<int>(PhosphorRules::Field::WindowClass) == 1
                  && static_cast<int>(PhosphorRules::Field::DesktopFile) == 2,
              "Field enum values feed migrated exclusion-rule UUIDs — renumbering them silently "
              "changes every migrated rule id. Bump the schema version and write a v4→v5 migration "
              "if a renumber is truly needed.");
static_assert(static_cast<int>(PhosphorRules::Operator::Contains) == 1
                  && static_cast<int>(PhosphorRules::Operator::AppIdMatches) == 5,
              "Operator enum values feed migrated exclusion-rule UUIDs — renumbering them silently "
              "changes every migrated rule id. Bump the schema version and write a v4→v5 migration "
              "if a renumber is truly needed.");

/// Fixed v5-UUID namespace for migrated SnapToZone-rule identities — distinct
/// from the exclusion namespace so the two folds can never collide on id.
inline const QUuid& snapToZoneMigrationNamespace()
{
    static const QUuid ns(QStringLiteral("{6f1c8e44-2a7b-5d93-8e10-4b2c9a7f1d35}"));
    return ns;
}

// Frozen on-disk keys for the legacy per-layout `appRules` array — the dead v3
// zone app-assignment format this fold is the last reader of. Local literals
// (NOT the live `ZoneJsonKeys::` accessors) so a future rename of those live
// keys can never retarget this migration away from what v3 wrote to disk.
constexpr QLatin1String kLayoutAppRulesKey{"appRules"};
constexpr QLatin1String kLayoutAppRulePattern{"pattern"};
constexpr QLatin1String kLayoutAppRuleZoneNumber{"zoneNumber"};
constexpr QLatin1String kLayoutAppRuleTargetScreen{"targetScreen"};

} // namespace

/// Drain the v4 animation-rule stash into @p rules. Malformed entries are
/// silently discarded — the legacy runtime loader did the same. The two-pass
/// shape (filter, then build) matches the legacy bridge byte-for-byte: the
/// priority `count - i` is computed against the POST-filter size, so dropped
/// entries don't leave gaps in the descending-by-list-order priority
/// sequence (`AnimationAppRuleList::fromJson` filtered first; `toRuleSet`
/// then used the filtered `entries.size()` as count).
/// Give every migrated rule the append helpers left at priority 0 (the Exclude,
/// animation-exclusion, and SnapToZone rules) a sensible band priority, matching what the Settings
/// renormalizer (RuleTemplates / sectionFor) would stamp. The Settings
/// controller only renormalizes on edit, not on load, so without this the
/// migrated rules would all read "Priority 0" and tie on a fresh load. A
/// composite match (e.g. the Steam exclude) lands in the Advanced band; the
/// simple window-property rules (AppId/WindowClass exclude, SnapToZone) land in
/// Application. One descending offset per band keeps them distinct, mirroring
/// renormalizePriorities. Managed and already-prioritized rules are untouched.
/// Past 100 zero-priority rules in one band the offset floors at the band base
/// (the `qMax(0, ...)` below), so the 100th-onward rules tie there — the same
/// saturation renormalizePriorities has, and harmless because priority does not
/// affect the boolean exclusion / snap slices these rules feed.
void assignBandPrioritiesToZeroRules(QList<PhosphorRules::Rule>& rules)
{
    using PhosphorRules::MatchExpression;
    using PhosphorRules::Rule;
    // Bands mirror RuleTemplates (src/settings/ruletemplates.h) — duplicated as
    // literals because that header lives in the settings tree and the core
    // library cannot link it.
    constexpr int kApplicationBandBase = 200;
    constexpr int kAdvancedBandBase = 500;
    constexpr int kBandWidth = 100;

    const auto isSimpleConjunction = [](const MatchExpression& m) {
        if (m.isLeaf()) {
            return true;
        }
        if (m.kind() != MatchExpression::Kind::All) {
            return false;
        }
        for (const MatchExpression& child : m.children()) {
            if (!child.isLeaf()) {
                return false;
            }
        }
        return true;
    };

    QHash<int, int> nextOffset; // band base → next available offset
    for (Rule& rule : rules) {
        if (rule.managed || rule.priority != 0) {
            continue;
        }
        const int base = isSimpleConjunction(rule.match) ? kApplicationBandBase : kAdvancedBandBase;
        auto it = nextOffset.find(base);
        if (it == nextOffset.end()) {
            it = nextOffset.insert(base, kBandWidth - 1);
        }
        const int offset = qMax(0, it.value());
        it.value() = offset - 1;
        rule.priority = base + offset;
    }
}

void appendAnimationRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonArray& stash)
{
    QList<QJsonObject> valid;
    valid.reserve(stash.size());
    for (const QJsonValue& entry : stash) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject obj = entry.toObject();
        if (isValidAnimationAppRuleSource(obj)) {
            valid.append(obj);
        }
    }
    const int count = valid.size();
    rules.reserve(rules.size() + count);
    for (int i = 0; i < count; ++i) {
        rules.append(buildAnimationAppRule(valid.at(i), i, count));
    }
}

/// Drain the v4 exclusion stash into @p rules. The stash carries two
/// comma-joined string fields (`Applications` and `WindowClasses`) — the v3
/// schema split them by intent (one matched against desktopFileName, one
/// against windowClass) but the daemon's runtime bridge always folded BOTH
/// against the resolved `appId` using the segment-aware `AppIdMatches`
/// operator. Preserve that bridge-flavoured semantics here so the migration
/// is behaviour-preserving: each surviving pattern, from either list, becomes
/// one `AppId AppIdMatches <pattern>` matcher with a terminal `Exclude`
/// action. Empty / whitespace-only patterns are dropped, mirroring the
/// runtime bridge's `pattern.trimmed().isEmpty()` skip.
void appendExclusionRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonObject& stash)
{
    using namespace PhosphorRules;
    const auto appendOne = [&rules](const QString& rawCsv) {
        for (const QString& part : rawCsv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString pattern = part.trimmed();
            if (pattern.isEmpty()) {
                continue;
            }
            Rule rule;
            // Deterministic id keyed off `(field, op, pattern)` — byte-
            // identical namespace + segment encoding to the (now-retired)
            // legacy runtime bridge so any pre-v4 daemon that bridge-built
            // a rule from the same `(field, op, pattern)` tuple produced
            // the same id, and an upgrading user carries the same UUIDs
            // across the migration.
            rule.id = QUuid::createUuidV5(
                exclusionMigrationNamespace(),
                Detail::encodeSegment(QString::number(static_cast<int>(Field::AppId)))
                    + Detail::encodeSegment(QString::number(static_cast<int>(Operator::AppIdMatches)))
                    + Detail::encodeSegment(pattern));
            rule.enabled = true;
            // Left at 0 here; assignBandPrioritiesToZeroRules stamps the real
            // band priority once the full list is assembled. The user can
            // drag-reorder it in the Rules page if precedence matters.
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern);
            RuleAction action;
            action.type = QString(ActionType::Exclude);
            rule.actions.append(action);
            rules.append(rule);
        }
    };
    // Both lists feed AppId rules — see the comment block above.
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedApplicationsKey()).toString());
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedWindowClassesKey()).toString());
}

/// Seed the premade "Steam" Rule into a freshly-built v4 rule set.
///
/// Steam is a CEF/XWayland client that spawns most of its UI — the Friends
/// List, the self-drawn `notificationtoasts_<N>_desktop` popups, Settings, and
/// chat windows — as separate top-level windows. They all share the `steam`
/// window class but report a title other than the main library window's
/// `Steam`. The transient/popup/menu members are already filtered structurally
/// by the effect's `shouldHandleWindow()` (see the `transientFor()` /
/// `isStructurallyUnmanageableWindowType()` net referenced in discussion #461),
/// but the Normal-type top-levels (Friends List, the notification toasts) slip
/// that filter and get auto-tiled — the long-standing "Steam breaks tiling"
/// bug other compositors ship rules for.
///
/// The rule excludes every `steam`-class window whose title is NOT exactly
/// `Steam`, leaving the main library window tileable (the Hyprland
/// `title:^(?!Steam$).*` idiom). `Exclude` is enforced at the effect's
/// `shouldHandleWindow()` gate, which evaluates the FULL WindowQuery
/// (windowClass + title) — so the composite match resolves there even though
/// the daemon-side appId-only fast paths (`isAppIdExcluded`, pending-restore
/// prune) ignore non-AppId leaves; those gate keyboard navigation / state
/// cleanup, not whether the window is tiled.
///
/// `WindowClass Contains "steam"` matches KWin's raw `"resourceName
/// resourceClass"` string (e.g. `"steam Steam"`, `"steamwebhelper Steam"`)
/// case-insensitively; the `Title Equals "Steam"` guard is likewise
/// case-insensitive (see MatchTypes operator semantics). The id is a fixed
/// deterministic UUIDv5 so a re-run never produces a duplicate.
void appendSteamDefaultRule(QList<PhosphorRules::Rule>& rules)
{
    using namespace PhosphorRules;
    Rule rule;
    rule.id = QUuid::createUuidV5(exclusionMigrationNamespace(),
                                  Detail::encodeSegment(QStringLiteral("steam-default-exclude")));
    rule.name = QStringLiteral("Steam");
    rule.enabled = true;
    // Left at 0 here; assignBandPrioritiesToZeroRules stamps the real band
    // priority once the full list is assembled (composite match → Advanced
    // band). An Exclude rule's precedence is irrelevant to the boolean exclusion
    // slice the effect evaluates, but a band value displays better than 0.
    rule.priority = 0;
    rule.match = MatchExpression::makeAll(
        {MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("steam")),
         MatchExpression::makeNone(
             {MatchExpression::makeLeaf(Field::Title, Operator::Equals, QStringLiteral("Steam"))})});
    RuleAction action;
    action.type = QString(ActionType::Exclude);
    rule.actions.append(action);
    rules.append(rule);
}

/// Drain the v4 ANIMATION exclusion stash into @p rules. Mirrors
/// `appendExclusionRulesFromStash` but produces `ExcludeAnimations`-action
/// rules with `DesktopFile Contains <pattern>` / `WindowClass Contains
/// <pattern>` leaves — the same match semantics the legacy effect-side
/// helper produced for the animation pipeline, so the effect's
/// shouldAnimateWindow gate stays behaviour-preserving for an upgrading
/// user. The Application/WindowClass split mirrors the legacy lists'
/// match-field distinction (unlike the snapping-side migration above,
/// which folded both into AppId rules because the daemon's runtime
/// bridge already collapsed the distinction).
void appendAnimationExclusionRulesFromStash(QList<PhosphorRules::Rule>& rules, const QJsonObject& stash)
{
    using namespace PhosphorRules;
    // Pin the wire-string for ExcludeAnimations against a future rename.
    // The animation-exclusion rule id is derived as
    // `UUIDv5(namespace, "<field>" + "<op>" + "<pattern>" + "<actionType>")`
    // — so renaming the wire-string from "excludeAnimations" to anything
    // else would silently change every existing migrated rule's id and
    // break the migration's collision-with-self idempotency guarantee.
    // The same rule's `RuleAction::type` field below also stores the wire-
    // string verbatim, so any rename has to update both producers AND the
    // testAnimationExclusions_idempotentRuleIds golden hash in lockstep.
    // QLatin1StringView's operator== is not constexpr in this Qt minimum;
    // the std::string_view bridge IS constexpr-comparable and gives a
    // build-break at compile time on rename.
    static_assert(std::string_view(ActionType::ExcludeAnimations.data(),
                                   static_cast<std::size_t>(ActionType::ExcludeAnimations.size()))
                      == std::string_view("excludeAnimations"),
                  "Renaming the ExcludeAnimations wire-string is a migration-breaking "
                  "change — every previously-migrated animation-exclude rule id is derived "
                  "from this exact byte sequence. Bump the schema version and write a "
                  "v4→v5 migration if you really need to rename.");
    const auto appendOne = [&rules](const QString& rawCsv, Field field) {
        for (const QString& part : rawCsv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString pattern = part.trimmed();
            if (pattern.isEmpty()) {
                continue;
            }
            Rule rule;
            // Deterministic id keyed off `(field, op, pattern, action)` — same
            // namespace + segment encoding as the snapping-side exclusion
            // migration so identical inputs collapse to identical ids on
            // re-runs. The snapping side always uses AppId/AppIdMatches while
            // this side uses DesktopFile|WindowClass/Contains, so the two folds
            // can never collide on the field/op/pattern triple anyway; the
            // appended action segment is defensive and additionally keeps this
            // fold's own entries distinct should the encodings ever converge.
            rule.id = QUuid::createUuidV5(
                exclusionMigrationNamespace(),
                Detail::encodeSegment(QString::number(static_cast<int>(field)))
                    + Detail::encodeSegment(QString::number(static_cast<int>(Operator::Contains)))
                    + Detail::encodeSegment(pattern) + Detail::encodeSegment(QString(ActionType::ExcludeAnimations)));
            rule.enabled = true;
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(field, Operator::Contains, pattern);
            RuleAction action;
            action.type = QString(ActionType::ExcludeAnimations);
            rule.actions.append(action);
            rules.append(rule);
        }
    };
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedApplicationsKey()).toString(), Field::DesktopFile);
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedWindowClassesKey()).toString(), Field::WindowClass);
}

/// Convert each layout file's legacy per-layout `appRules` into first-class
/// Rules. v3 stored app→zone assignments on the Layout (`Layout::appRules`:
/// a `{pattern, zoneNumber, targetScreen}` triple, single zone); v4 unifies them
/// into the window-rule store. Each becomes `AppId AppIdMatches <pattern> →
/// SnapToZone [zoneNumber]`, plus a `RouteToScreen <targetScreen>` action when the
/// legacy rule pinned a monitor. AppId / AppIdMatches mirrors the retired
/// `Layout::matchAppRule` (which matched the pattern against the window's appId via
/// segment-aware `appIdMatches`) and the daemon placement path, which resolves the
/// query on appId — WindowClass is not tracked daemon-side, so a WindowClass leaf
/// would never match.
///
/// The pattern is normalized through `WindowId::normalizeAppId` before becoming the
/// AppId leaf. v3 stored the raw matcher, which for X11 windows was often the
/// two-token "resourceName resourceClass" form ("chromium chromium"). The v4 daemon
/// keys rule queries on the SINGLE normalized appId ("chromium"), and
/// `appIdMatches` treats the embedded space as a literal — so a two-token pattern
/// matched nothing. Normalizing here (last whitespace token, lowercased — the exact
/// derivation the runtime applies) makes the migrated rule match the appId the
/// daemon actually reports.
///
/// The legacy `targetScreen` is carried over as a `RouteToScreen` action, which
/// pins the placement to that monitor on open — restoring the per-monitor
/// assignment v3 had. The value is copied verbatim: real v3 data already stores the
/// canonical EDID screen-id form ("Manuf:Model:Serial", what the runtime and the
/// settings screen-picker use), so this stays a pure JSON transform with no
/// coupling to live screen state. A legacy connector-name value ("DP-1") simply
/// won't resolve at runtime and the route is skipped (the snap falls back to the
/// opening screen) — no worse than the old behaviour, which dropped the pin
/// entirely. The deterministic rule id folds in the target screen alongside the
/// pattern and zone so a crash-and-retry conversion stays byte-identical.
///
/// Patterns are deduped across layouts — a SnapToZone ordinal rule fires
/// regardless of which layout is active, so one pattern can map to only one
/// placement; on a same-pattern / different-zone conflict the first wins and the
/// rest are logged. Layout files are visited in name order for deterministic
/// "first wins".
void appendLayoutAppRulesAsSnapToZone(QList<PhosphorRules::Rule>& rules, const QString& layoutsDir)
{
    using namespace PhosphorRules;
    QDir dir(layoutsDir);
    if (!dir.exists()) {
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QSet<QString> seenPatterns;
    for (const QString& fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const QJsonArray appRules = doc.object().value(kLayoutAppRulesKey).toArray();
        for (const QJsonValue& entry : appRules) {
            if (!entry.isObject()) {
                continue;
            }
            const QJsonObject ar = entry.toObject();
            const QString rawPattern = ar.value(kLayoutAppRulePattern).toString().trimmed();
            const int zoneNumber = ar.value(kLayoutAppRuleZoneNumber).toInt(0);
            if (rawPattern.isEmpty() || zoneNumber < 1) {
                continue;
            }
            // Normalize the legacy matcher to the single appId token the v4 daemon
            // keys rule queries on. v3 stored the raw class string, often the X11
            // "resourceName resourceClass" two-token form ("chromium chromium");
            // `appIdMatches` treats the embedded space literally, so that pattern
            // matched nothing once the daemon switched to the normalized appId. Use
            // the SAME derivation the runtime applies (last space-delimited token,
            // lowercased) so the migrated leaf matches.
            const QString pattern = PhosphorIdentity::WindowId::normalizeAppId(QString(), rawPattern);
            if (pattern.isEmpty()) {
                continue;
            }
            const QString targetScreen = ar.value(kLayoutAppRuleTargetScreen).toString().trimmed();
            // The SnapToZone action validator caps ordinals at MaxZoneOrdinal, so a
            // legacy zoneNumber beyond it would be silently dropped by the loader's
            // validator. Skip it here with a visible warning instead of emitting a
            // rule that vanishes on the next load.
            if (zoneNumber > MaxZoneOrdinal) {
                qWarning(
                    "ConfigMigration: app->zone pattern '%s' targets zone %d beyond the max ordinal (%d) — "
                    "dropping the assignment.",
                    qPrintable(rawPattern), zoneNumber, MaxZoneOrdinal);
                continue;
            }
            // Dedup on the NORMALIZED appId: two raw patterns that normalize to the
            // same app ("chromium chromium" and "chromium") are one placement. A
            // SnapToZone rule fires regardless of the active layout, so a normalized
            // pattern can map to only one placement — first wins.
            const QString patternKey = pattern;
            if (seenPatterns.contains(patternKey)) {
                qWarning(
                    "ConfigMigration: duplicate app->zone pattern '%s' (normalized '%s') across layouts — keeping "
                    "the first, dropping zone %d (a SnapToZone ordinal rule fires regardless of the active layout, "
                    "so a pattern can map to only one placement).",
                    qPrintable(rawPattern), qPrintable(pattern), zoneNumber);
                continue;
            }
            seenPatterns.insert(patternKey);

            Rule rule;
            // Deterministic id from (normalized pattern, zone, target screen) so a
            // crash-and-retry conversion yields byte-identical rules.
            rule.id =
                QUuid::createUuidV5(snapToZoneMigrationNamespace(),
                                    Detail::encodeSegment(pattern) + Detail::encodeSegment(QString::number(zoneNumber))
                                        + Detail::encodeSegment(targetScreen));
            rule.enabled = true;
            // Left at 0 here; assignBandPrioritiesToZeroRules stamps the real
            // band priority once the full list is assembled. The user can
            // drag-reorder it in the Rules page if precedence matters.
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern);
            RuleAction action;
            action.type = QString(ActionType::SnapToZone);
            QJsonObject params;
            params.insert(QString(ActionParam::Zones), QJsonArray{zoneNumber});
            action.params = params;
            rule.actions.append(action);
            // Carry the legacy per-monitor pin as a RouteToScreen action so the app
            // still opens into its zone on the target monitor. Verbatim copy — real
            // v3 data stores the canonical EDID screen id the runtime matches; a
            // legacy connector-name value just won't resolve and the route is
            // skipped (the snap falls back to the opening screen).
            if (!targetScreen.isEmpty()) {
                RuleAction route;
                route.type = QString(ActionType::RouteToScreen);
                QJsonObject routeParams;
                routeParams.insert(QString(ActionParam::TargetScreenId), targetScreen);
                route.params = routeParams;
                rule.actions.append(route);
            }
            rules.append(rule);
        }
    }
}

} // namespace PlasmaZones
