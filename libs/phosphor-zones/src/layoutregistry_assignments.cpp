// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.
//
// Phase 3b: the per-context assignment cascade is reimplemented on the
// unified WindowRule engine. There is one resolution model: a windowless
// WindowQuery evaluated through RuleEvaluator against the WindowRuleStore's
// rule set. The deterministic Assignment cascade (exact → activity → desktop
// → screen → provider default) is reproduced by the ContextRuleBridge
// priority formula — higher priority pins more context dimensions, so the
// evaluator's descending-priority, first-action-per-slot walk returns the
// same result the old walkCascade() did. Connector-name / virtual-screen
// fallback is NOT a priority band — it is a query-side recursive key rewrite,
// kept here in layoutForScreen() / the shared resolve helper.

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRule.h>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace {

namespace PWR = PhosphorWindowRule;
namespace CRB = PhosphorWindowRule::ContextRuleBridge;

// Build the windowless context query for a (screen, desktop, activity) tuple.
// No window attributes are set — window-property predicates evaluate false,
// so only context-only rules contribute. This reproduces the old cascade.
PWR::WindowQuery makeContextQuery(const QString& screenId, int virtualDesktop, const QString& activity)
{
    PWR::WindowQuery query;
    query.screenId = screenId;
    query.virtualDesktop = virtualDesktop;
    query.activity = activity;
    return query;
}

// Human-readable label for a context assignment rule's (screen, desktop,
// activity) tuple — the single place the " · Desktop N" / " · Activity"
// suffix shape is constructed (used by upsert + every batch setter).
QString contextRuleName(const QString& screenId, int virtualDesktop, const QString& activity)
{
    return screenId + (virtualDesktop > 0 ? QStringLiteral(" · Desktop ") + QString::number(virtualDesktop) : QString())
        + (activity.isEmpty() ? QString() : QStringLiteral(" · Activity"));
}

// Decode a match expression's pinned (screenId, desktop, activity) context
// tuple via the shared ContextRuleBridge::contextDimsOf — the one classifier
// for context-rule shape. A non-context / nested-composite match leaves all
// three at their defaults (empty / 0).
struct ContextDims
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
};
ContextDims decodeDims(const PWR::MatchExpression& match)
{
    ContextDims dims;
    CRB::contextDimsOf(match, dims.screenId, dims.virtualDesktop, dims.activity);
    return dims;
}

// True if @p match is exactly the context shape for the pinned dimensions of
// (screenId, virtualDesktop, activity) — i.e. the match ContextRuleBridge
// would emit for that tuple. A match pinning more/fewer dimensions, pinning
// different values, or carrying ANY window-property leaf is NOT an exact
// match. This is what hasExplicitAssignment relies on to distinguish a stored
// entry from a wider cascade entry or the synthesized provider default.
//
// contextDimsOf ignores window-property leaves, so a flat rule mixing a
// window predicate with the context leaves (e.g. All{ ScreenId==DP-1,
// AppId==konsole }) would still decode to the same tuple — the
// isContextOnly() gate is the discriminator that rejects it, mirroring
// isContextAssignmentRule's "context-only" contract so upsert / clear never
// clobber a window-property rule.
bool matchIsExactContext(const PWR::MatchExpression& match, const QString& screenId, int virtualDesktop,
                         const QString& activity)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return dims.screenId == screenId && dims.virtualDesktop == virtualDesktop && dims.activity == activity;
}

// True if @p rule carries a SetEngineMode action. Both context assignment
// rules and the provider-default catch-all carry one; callers that must
// exclude the catch-all do so explicitly (see resolveAssignmentEntry), and
// the matchIsExactContext* shape filters reject a catch-all anyway.
bool hasEngineModeAction(const PWR::WindowRule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            return true;
        }
    }
    return false;
}

// Forward declarations: the shape predicates are defined just below but
// isContextAssignmentRule references all three.
bool matchIsExactContextBase(const PWR::MatchExpression& match);
bool matchIsExactContextDesktop(const PWR::MatchExpression& match);
bool matchIsExactContextActivity(const PWR::MatchExpression& match);

// True if @p rule is a pure context-assignment rule for one of the cascade
// families (per-screen-base / per-desktop / per-activity) — i.e. it carries a
// SetEngineMode action AND its match is exactly a pinned context shape (not
// the catch-all, not a window-property rule that happens to carry an
// engine-mode action). The batch purge / clear loops gate on this so a
// legitimate window-property rule carrying SetSnappingLayout / SetEngineMode
// actions is never rebuilt — rebuilding force-injects SetEngineMode and
// drops every other action, which would clobber a window-property rule.
bool isContextAssignmentRule(const PWR::WindowRule& rule)
{
    if (!hasEngineModeAction(rule) || rule.match.isCatchAll()) {
        return false;
    }
    return matchIsExactContextBase(rule.match) || matchIsExactContextDesktop(rule.match)
        || matchIsExactContextActivity(rule.match);
}

// Shape predicates for the per-screen-base / per-desktop / per-activity
// context rule families — used by the batch setters to drop one family
// before writing the new entries, and by the introspection helpers to keep
// their family filter identical to the batch setters'. Each gates on
// MatchExpression::isContextOnly() before decoding (see matchIsExactContext
// for why a window-property leaf must never classify as a context family
// member), then decomposes via the shared decodeDims (contextDimsOf).
bool matchIsExactContextBase(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && dims.virtualDesktop == 0 && dims.activity.isEmpty();
}
bool matchIsExactContextDesktop(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && dims.virtualDesktop > 0 && dims.activity.isEmpty();
}
bool matchIsExactContextActivity(const PWR::MatchExpression& match)
{
    if (!match.isContextOnly()) {
        return false;
    }
    const ContextDims dims = decodeDims(match);
    return !dims.screenId.isEmpty() && !dims.activity.isEmpty();
}

// Build the AssignmentEntry encoded directly by a rule's action list (no
// evaluation — used by introspection helpers like desktopAssignments()).
AssignmentEntry entryFromRuleMatchActions(const PWR::WindowRule& rule)
{
    AssignmentEntry entry;
    // A rule with no SetEngineMode action leaves the mode at the Snapping
    // default — make that explicit rather than relying on AssignmentEntry's
    // default member initializer.
    entry.mode = AssignmentEntry::Snapping;
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
            entry.mode = action.params.value(QLatin1String("mode")).toString() == QLatin1String("autotile")
                ? AssignmentEntry::Autotile
                : AssignmentEntry::Snapping;
        } else if (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)) {
            entry.snappingLayout = action.params.value(QLatin1String("layoutId")).toString();
        } else if (action.type == QLatin1String(PWR::ActionType::SetTilingAlgorithm)) {
            entry.tilingAlgorithm = action.params.value(QLatin1String("algorithm")).toString();
        }
    }
    return entry;
}

} // anonymous namespace

// ── Rule-backed cascade resolution ──────────────────────────────────────────

std::optional<AssignmentEntry> LayoutRegistry::resolveAssignmentEntry(const QString& screenId, int virtualDesktop,
                                                                      const QString& activity) const
{
    // The cascade is reproduced by the rule set's descending-priority order.
    // The legacy walkCascade treated the provider default as a MISS
    // (resolveDefaultAssignmentEntry handled it via injected lambdas), so the
    // catch-all rule is excluded here — only PINNED context rules count as a
    // cascade hit.
    //
    // The descending-priority, tie-break-by-list-order walk is the shared
    // RuleEvaluator::highestPriorityMatch — the one place that walk lives, so
    // this resolver can never drift from the evaluator's own ordering. The
    // candidate filter keeps only pinned context-assignment rules.
    // SetSnappingLayout and SetTilingAlgorithm share the `layout` action slot,
    // so the winning rule's full action list is read, preserving the
    // mode-toggle losslessness invariant (an entry keeps BOTH snappingLayout
    // and tilingAlgorithm regardless of active mode).
    const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
    const PWR::WindowRule* winner = m_evaluator->highestPriorityMatch(query, [](const PWR::WindowRule& rule) {
        return hasEngineModeAction(rule) && !rule.match.isCatchAll();
    });
    if (winner == nullptr) {
        return std::nullopt;
    }
    return entryFromRuleMatchActions(*winner);
}

bool LayoutRegistry::hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    return findExactContextRule(screenId, virtualDesktop, activity) != nullptr;
}
const PhosphorWindowRule::WindowRule* LayoutRegistry::findExactContextRule(const QString& screenId, int virtualDesktop,
                                                                           const QString& activity) const
{
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!hasEngineModeAction(rule)) {
            continue;
        }
        if (matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
            return &rule;
        }
    }
    return nullptr;
}

QUuid LayoutRegistry::exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    const PWR::WindowRule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    return rule ? rule->id : QUuid();
}

void LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    const bool autotile = (entry.mode == AssignmentEntry::Autotile);
    PWR::WindowRule rule = PWR::ContextRuleBridge::makeAssignmentRule(
        contextRuleName(screenId, virtualDesktop, activity), screenId, virtualDesktop, activity, autotile,
        entry.snappingLayout, entry.tilingAlgorithm);

    const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity);
    if (existing == nullptr) {
        m_ruleStore->addRule(rule);
    } else {
        rule.id = existing->id; // preserve the rule's identity across the update
        // makeAssignmentRule always stamps enabled = true; an upsert must not
        // silently re-enable a rule the user disabled. A disabled context
        // assignment is still an explicit assignment — preserve the flag.
        rule.enabled = existing->enabled;
        m_ruleStore->updateRule(rule);
    }
}

bool LayoutRegistry::removeAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity)
{
    const QUuid existing = exactContextRuleId(screenId, virtualDesktop, activity);
    if (existing.isNull()) {
        return false;
    }
    return m_ruleStore->removeRule(existing);
}

bool LayoutRegistry::purgeSnappingLayoutFromAssignments(const QString& layoutId)
{
    // A snap layout was deleted. Every rule whose SetSnappingLayout action
    // carries this id must lose that reference — but NOT the whole rule, and
    // NOT its other actions.
    //
    // Two rule shapes can carry a SetSnappingLayout action for the deleted id:
    //
    //  1. A pure context-assignment rule (per-screen / -desktop / -activity).
    //     An Autotile-mode context rule can still carry a stale
    //     SetSnappingLayout (mode-toggle losslessness), so blanket-deleting it
    //     would drop its SetEngineMode + SetTilingAlgorithm autotile intent.
    //     Rebuild via ContextRuleBridge::makeAssignmentActions with the
    //     snapping layout cleared but mode + tilingAlgorithm preserved; only
    //     drop the whole rule when, after the clear, nothing but a default
    //     (Snapping) engine-mode remains — a pure Snapping mode-only context
    //     rule encodes no intent beyond the default.
    //
    //  2. A window-property rule that legitimately carries a SetSnappingLayout
    //     action (the phased rollout introduces exactly such rules). Rebuilding
    //     it through makeAssignmentActions would force-inject a SetEngineMode
    //     action and drop every other action it carries. Instead, surgically
    //     remove ONLY the matching SetSnappingLayout action and leave all
    //     other actions intact; drop the rule only if nothing meaningful
    //     remains after the removal.
    QList<PWR::WindowRule> kept;
    bool changed = false;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        const bool referencesDeleted =
            std::any_of(rule.actions.cbegin(), rule.actions.cend(), [&layoutId](const PWR::RuleAction& action) {
                return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                    && action.params.value(QLatin1String("layoutId")).toString() == layoutId;
            });
        if (!referencesDeleted) {
            kept.append(rule);
            continue;
        }
        changed = true;

        if (isContextAssignmentRule(rule)) {
            // Shape 1: rebuild the lossless context-action set with the dead
            // snapping reference cleared; mode + tilingAlgorithm survive.
            const AssignmentEntry entry = entryFromRuleMatchActions(rule);
            const bool autotile = (entry.mode == AssignmentEntry::Autotile);
            if (!autotile && entry.tilingAlgorithm.isEmpty()) {
                // Nothing meaningful remains — a bare Snapping engine-mode is
                // the default. Drop the whole rule.
                qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                    << "— only a default Snapping mode remained after clearing the deleted layout";
                continue;
            }
            PWR::WindowRule rebuilt = rule;
            rebuilt.actions = PWR::ContextRuleBridge::makeAssignmentActions(autotile, QString(), entry.tilingAlgorithm);
            kept.append(rebuilt);
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: rebuilt context rule" << rule.id.toString()
                                << "— cleared deleted snapping layout, preserved mode/tilingAlgorithm";
            continue;
        }

        // Shape 2: a window-property (or otherwise non-context) rule. Remove
        // only the SetSnappingLayout actions referencing the deleted layout;
        // every other action is preserved verbatim.
        PWR::WindowRule trimmed = rule;
        trimmed.actions.erase(std::remove_if(trimmed.actions.begin(), trimmed.actions.end(),
                                             [&layoutId](const PWR::RuleAction& action) {
                                                 return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                                                     && action.params.value(QLatin1String("layoutId")).toString()
                                                     == layoutId;
                                             }),
                              trimmed.actions.end());
        if (trimmed.actions.isEmpty()) {
            // The rule's only action was the dead snapping reference — nothing
            // meaningful remains, so drop it.
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped rule" << rule.id.toString()
                                << "— its only action referenced the deleted snapping layout";
            continue;
        }
        kept.append(trimmed);
        qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: trimmed rule" << rule.id.toString()
                            << "— removed the SetSnappingLayout action for the deleted layout, kept all others";
    }
    if (changed) {
        m_ruleStore->setAllRules(kept);
    }
    return changed;
}

// ── Mutators ────────────────────────────────────────────────────────────────

void LayoutRegistry::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                  PhosphorZones::Layout* layout)
{
    if (layout) {
        // Preserve an existing tilingAlgorithm — only mode + snappingLayout
        // change (the mode-toggle losslessness invariant). One exact-shape
        // lookup reads the stored rule's actions directly: a wider cascade
        // entry must NOT bleed its tilingAlgorithm into this narrower rule,
        // so only the rule pinning exactly this tuple seeds the entry.
        AssignmentEntry entry;
        if (const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
            entry = entryFromRuleMatchActions(*existing);
        }
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layout->id().toString();
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        qCDebug(lcZonesLib) << "assignLayout: screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity)
                            << "layout=" << layout->name();
    } else {
        // Clearing: remove the exact-shape rule entirely. Skip the signal
        // when there was nothing to remove.
        if (!removeAssignmentRule(screenId, virtualDesktop, activity)) {
            return;
        }
        qCDebug(lcZonesLib) << "assignLayout: removed screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity);
    }

    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
}

void LayoutRegistry::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId)
{
    if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        // Store autotile assignment — set mode to Autotile, preserve the
        // existing snappingLayout. One exact-shape lookup (see assignLayout)
        // — only the rule pinning exactly this tuple seeds the entry.
        AssignmentEntry entry;
        if (const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
            entry = entryFromRuleMatchActions(*existing);
        }
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutRegistry::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                              const AssignmentEntry& entry)
{
    // Store the entry unconditionally — mode-only entries (empty snapping +
    // empty tiling) are valid when explicitly set by the KCM to preserve
    // mode at a context level.
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);

    qCDebug(lcZonesLib) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                        << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm;

    PhosphorZones::Layout* layout = nullptr;
    if (entry.mode == AssignmentEntry::Snapping && !entry.snappingLayout.isEmpty()) {
        layout = layoutById(QUuid::fromString(entry.snappingLayout));
    }
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
}

// ── Queries ─────────────────────────────────────────────────────────────────

PhosphorZones::Layout* LayoutRegistry::layoutForScreen(const QString& screenId, int virtualDesktop,
                                                       const QString& activity) const
{
    // Connector-name / virtual-screen fallback: a query-side recursive key
    // rewrite, not a priority band. The retry is gated on "no cascade entry
    // at all", NOT "no snap Layout*" — the legacy walkCascade terminated at
    // the first matching entry (including a narrower Autotile entry) and only
    // then fell through to the global default. Treating an Autotile entry as
    // a miss would let a connector/VS retry surface a different (snapping)
    // assignment the legacy walk never reached. So tryResolve returns:
    //   - nullopt   -> no cascade entry; the caller may retry.
    //   - {nullptr} -> an entry exists but yields no snap Layout* (Autotile,
    //                  or a snap entry with empty/unknown layout id) — the
    //                  cascade is settled, fall through to defaultLayout().
    //   - {layout}  -> resolved snap layout.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<PhosphorZones::Layout*> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt; // genuine miss — the caller may retry
        }
        // An entry exists; the cascade is settled — never retry. An Autotile
        // entry (or a snap entry with an empty layout id) has no snap Layout*.
        if (entry->mode == AssignmentEntry::Autotile || entry->snappingLayout.isEmpty()) {
            return std::optional<PhosphorZones::Layout*>(nullptr);
        }
        return std::optional<PhosphorZones::Layout*>(layoutById(QUuid::fromString(entry->snappingLayout)));
    };

    if (const auto result = tryResolve(screenId)) {
        return *result ? *result : defaultLayout();
    }
    // Connector-name fallback.
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result ? *result : defaultLayout();
            }
        }
    }
    // Virtual-screen fallback — inherit the physical screen's assignment.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result ? *result : defaultLayout();
        }
    }

    // No explicit assignment in the cascade — defer to the registry-wide
    // default. layoutForScreen returns a snap Layout* and has no autotile
    // counterpart; autotile-mode resolution is the autotile engine's job.
    return defaultLayout();
}

void LayoutRegistry::clearAssignment(const QString& screenId, int virtualDesktop, const QString& activity)
{
    assignLayout(screenId, virtualDesktop, activity, nullptr);
}

bool LayoutRegistry::hasExplicitAssignment(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Exact-shape store lookup — NEVER a resolve() (which always returns the
    // catch-all). True iff a rule whose match is exactly this tuple's shape
    // exists in the rule set.
    //
    // A DISABLED exact-context rule still counts as an explicit assignment:
    // "explicit" means the user stored an entry for this tuple, not that the
    // entry is currently active. resolveAssignmentEntry() (via the evaluator)
    // skips disabled rules, so the two introspection APIs intentionally
    // diverge — hasExplicitAssignment reports stored intent, the resolvers
    // report the effective cascade result. The KCM relies on this so a
    // disabled assignment is not lost from the UI.
    return hasExactContextRule(screenId, virtualDesktop, activity);
}

QString LayoutRegistry::assignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                              const QString& activity) const
{
    // Shared cascade with layoutForScreen, but accepts any entry whose
    // activeLayoutId() is non-empty (incl. Autotile entries). Connector /
    // virtual-screen fallback applies here too.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<QString> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        const QString id = entry->activeLayoutId();
        return id.isEmpty() ? std::nullopt : std::optional<QString>(id);
    };

    if (const auto result = tryResolve(screenId)) {
        return *result;
    }
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result;
        }
    }

    // No stored entry in the cascade — fall through to the level-1 global
    // default via the injected providers.
    const AssignmentEntry def = resolveDefaultAssignmentEntry();
    return def.activeLayoutId();
}

AssignmentEntry LayoutRegistry::assignmentEntryForScreen(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
{
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<AssignmentEntry> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        if (entry->activeLayoutId().isEmpty()) {
            return std::nullopt;
        }
        return entry;
    };

    if (const auto result = tryResolve(screenId)) {
        return *result;
    }
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result;
        }
    }

    // Cascade miss — synthesize from the level-1 global default.
    return resolveDefaultAssignmentEntry();
}

AssignmentEntry::Mode LayoutRegistry::modeForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).mode;
}

QString LayoutRegistry::snappingLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).snappingLayout;
}

QString LayoutRegistry::tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop,
                                                 const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).tilingAlgorithm;
}

void LayoutRegistry::clearAutotileAssignments()
{
    // Flip every Autotile assignment rule to Snapping (preserving both layout
    // fields) and drop autotile quick-layout slots.
    QList<PWR::WindowRule> updated = m_ruleStore->ruleSet().rules();
    QList<QPair<QString, int>> affected; // (screenId, virtualDesktop) for signals
    bool changed = false;

    for (PWR::WindowRule& rule : updated) {
        // Only pure context-assignment rules are flipped — a window-property
        // rule that legitimately carries a SetEngineMode action must not be
        // rebuilt (makeAssignmentActions would drop its other actions).
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        const AssignmentEntry entry = entryFromRuleMatchActions(rule);
        if (entry.mode != AssignmentEntry::Autotile) {
            continue;
        }
        // Flip to Snapping — preserve both layout fields so re-enabling
        // autotile can restore the previous algorithm.
        rule.actions =
            PWR::ContextRuleBridge::makeAssignmentActions(false, entry.snappingLayout, entry.tilingAlgorithm);
        changed = true;

        // Recover (screen, desktop) for the layoutAssigned signal.
        const ContextDims dims = decodeDims(rule.match);
        affected.append(qMakePair(dims.screenId, dims.virtualDesktop));
    }

    // Drop autotile quick-layout slots.
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
        if (PhosphorLayout::LayoutId::isAutotile(it.value())) {
            it = m_quickLayoutShortcuts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        m_ruleStore->setAllRules(updated);
        writeQuickLayouts();
        for (const auto& [sid, desk] : std::as_const(affected)) {
            qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << sid
                                << "desktop=" << desk;
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
        qCInfo(lcZonesLib) << "Cleared all autotile assignments";
    }
}

// ── Batch setters ───────────────────────────────────────────────────────────

namespace {

// The decoded (screen, desktop, activity) cascade context a single
// batch-assignment hash key pins — each batch setter supplies a key→context
// decoder, the shared driver below works in terms of this tuple.
struct BatchContext
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
};

} // anonymous namespace

// Shared driver for the three per-family batch setters — the numbered steps
// below replace the ~150 near-identical lines the setters used to inline.
// @p decode maps a hash key to its cascade context; @p valid rejects an
// ill-formed context (a desktop entry must pin desktop > 0, an activity
// entry a non-empty activity, else a wrong-family base rule is built);
// @p familyMatches selects which existing rules to drop; @p emitDesktop /
// @p emitActivity are the context the closing layoutAssigned is computed
// under; @p label names the family in log output.
template<typename KeyT, typename DecodeFn, typename ValidFn, typename FamilyFn>
void LayoutRegistry::applyBatchAssignments(const QHash<KeyT, QString>& assignments, DecodeFn decode, ValidFn valid,
                                           FamilyFn familyMatches, int emitDesktop, const QString& emitActivity,
                                           const char* label)
{
    // Step 1 — snapshot existing exact-context entries for every incoming key.
    QHash<KeyT, AssignmentEntry> oldEntries;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        if (const PWR::WindowRule* existing = findExactContextRule(ctx.screenId, ctx.virtualDesktop, ctx.activity)) {
            oldEntries.insert(it.key(), entryFromRuleMatchActions(*existing));
        }
    }

    // Step 2 — drop every rule belonging to this family; keep the rest.
    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (hasEngineModeAction(rule) && familyMatches(rule.match)) {
            continue;
        }
        kept.append(rule);
    }

    // Step 3 — rebuild the family from the incoming assignments.
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        const QString& layoutId = it.value();
        if (!valid(ctx)) {
            qCWarning(lcZonesLib) << "Skipping invalid" << label << "assignment:" << ctx.screenId << ctx.virtualDesktop
                                  << ctx.activity;
            continue;
        }
        const QString logContext = contextRuleName(ctx.screenId, ctx.virtualDesktop, ctx.activity);
        if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldEntries.value(it.key()));
        kept.append(PWR::ContextRuleBridge::makeAssignmentRule(logContext, ctx.screenId, ctx.virtualDesktop,
                                                               ctx.activity, entry.mode == AssignmentEntry::Autotile,
                                                               entry.snappingLayout, entry.tilingAlgorithm));
        storedScreens.insert(ctx.screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    // Step 4 — one commit, then signal per stored screen.
    m_ruleStore->setAllRules(kept);
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, emitDesktop, assignmentIdForScreen(screenId, emitDesktop, emitActivity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << label << "assignments";
}

void LayoutRegistry::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QString& screenId) {
            return BatchContext{screenId, 0, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty();
        },
        matchIsExactContextBase,
        /*emitDesktop=*/0, /*emitActivity=*/QString(), "screen");
}

void LayoutRegistry::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, int>& key) {
            return BatchContext{key.first, key.second, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && ctx.virtualDesktop >= 1;
        },
        matchIsExactContextDesktop, m_currentVirtualDesktop, m_currentActivity, "desktop");
}

void LayoutRegistry::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, QString>& key) {
            return BatchContext{key.first, 0, key.second};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && !ctx.activity.isEmpty();
        },
        matchIsExactContextActivity, /*emitDesktop=*/0, /*emitActivity=*/QString(), "activity");
}

QHash<QPair<QString, int>, QString> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-desktop family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental VirtualDesktop== predicate cannot leak in.
        if (!hasEngineModeAction(rule) || !matchIsExactContextDesktop(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.virtualDesktop)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

QHash<QPair<QString, QString>, QString> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-activity family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental Activity== predicate cannot leak in.
        if (!hasEngineModeAction(rule) || !matchIsExactContextActivity(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.activity)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

} // namespace PhosphorZones
