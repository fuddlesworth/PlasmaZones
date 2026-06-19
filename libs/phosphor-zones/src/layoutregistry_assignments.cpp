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

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorWindowRules/ContextRuleBridge.h>
#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowQuery.h>
#include <PhosphorWindowRules/WindowRule.h>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorWindowRules;

// The rule-shape classification / context helpers (contextRuleName,
// decodeDims, the matchIsExactContext* family, hasEngineModeAction,
// isContextAssignmentRule, entryFromRuleMatchActions, makeContextQuery)
// are pure functions with no LayoutRegistry-member dependency and live in
// layoutregistry_rulehelpers.cpp / _p.h, keeping the rule-shape vocabulary in
// one place separate from the registry's member-bound resolution logic here.
using namespace RuleHelpers;

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
    //
    // Hot-path cache via the shared revision-invalidated memoizer. The linear
    // walk is O(N rules × M predicates per match); overlay/OSD callers issue it
    // per cursor-move, and the connector + virtual-screen fallback chain in the
    // public resolvers triples that on a miss. A @c nullopt is cached too — a
    // genuine miss must not re-walk three times per cursor frame.
    return resolveCachedContext(m_contextResolveCache, m_contextResolveCacheRevision, screenId, virtualDesktop,
                                activity, [&]() -> std::optional<AssignmentEntry> {
                                    const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
                                    const PWR::WindowRule* winner =
                                        m_evaluator->highestPriorityMatch(query, [](const PWR::WindowRule& rule) {
                                            return hasEngineModeAction(rule) && !rule.match.isCatchAll();
                                        });
                                    if (winner != nullptr) {
                                        return entryFromRuleMatchActions(*winner);
                                    }
                                    return std::nullopt;
                                });
}

ContextGapOverride LayoutRegistry::resolveContextGaps(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const
{
    // Unlike resolveAssignmentEntry (single winning engine-mode rule), gap
    // overrides are read PER SLOT from the evaluator's ResolvedActions, so a
    // gap-only rule and, say, a separate engine-mode rule on the same context
    // compose — and two gap rules touching different slots both apply. No
    // engine-mode gate and no isValid() filter: a gap-only context rule is a
    // first-class override here.
    if (!m_evaluator) {
        return ContextGapOverride{};
    }

    // Hot-path cache via the shared revision-invalidated memoizer: the geometry
    // path resolves the same context twice per op (zone padding + outer gaps)
    // and N× inside a multi-zone snap, all with identical arguments.
    return resolveCachedContext(
        m_contextGapCache, m_contextGapCacheRevision, screenId, virtualDesktop, activity, [&]() -> ContextGapOverride {
            ContextGapOverride gaps;
            const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            const PWR::ResolvedActions resolved = m_evaluator->resolve(query);

            const auto readInt = [&resolved](QLatin1StringView slot, std::optional<int>& out) {
                if (const auto action = resolved.slot(QString(slot))) {
                    out = action->params.value(PWR::ActionParam::Value).toInt();
                }
            };
            const auto readBool = [&resolved](QLatin1StringView slot, std::optional<bool>& out) {
                if (const auto action = resolved.slot(QString(slot))) {
                    out = action->params.value(PWR::ActionParam::Value).toBool();
                }
            };
            readInt(PWR::ActionSlot::ZonePadding, gaps.zonePadding);
            readInt(PWR::ActionSlot::OuterGap, gaps.outerGap);
            readBool(PWR::ActionSlot::UsePerSideOuterGap, gaps.usePerSideOuterGap);
            readInt(PWR::ActionSlot::OuterGapTop, gaps.outerGapTop);
            readInt(PWR::ActionSlot::OuterGapBottom, gaps.outerGapBottom);
            readInt(PWR::ActionSlot::OuterGapLeft, gaps.outerGapLeft);
            readInt(PWR::ActionSlot::OuterGapRight, gaps.outerGapRight);
            return gaps;
        });
}

bool LayoutRegistry::resolveContextLocked(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Mirror resolveContextGaps: a per-slot read off the evaluator's
    // ResolvedActions, not the single-winner assignment walk. The Locked slot
    // is filled by the highest-priority matching context rule carrying a
    // LockContext action; we report its boolean value. No engine-mode gate —
    // a lock-only context rule is a first-class overlay.
    if (!m_evaluator) {
        return false;
    }

    // Hot-path cache via the shared revision-invalidated memoizer: the lock
    // check runs per cursor-move while a selector is open and on every
    // layout-switch attempt.
    return resolveCachedContext(m_contextLockCache, m_contextLockCacheRevision, screenId, virtualDesktop, activity,
                                [&]() -> bool {
                                    const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
                                    const PWR::ResolvedActions resolved = m_evaluator->resolve(query);
                                    if (const auto action = resolved.slot(QString(PWR::ActionSlot::Locked))) {
                                        return action->params.value(PWR::ActionParam::Value).toBool();
                                    }
                                    return false;
                                });
}

ContextOverlayOverride LayoutRegistry::resolveContextOverlay(const QString& screenId, int virtualDesktop,
                                                             const QString& activity) const
{
    // Per-slot read across all matching context rules (mirrors resolveContextGaps):
    // independent overlay-shader / overlay-style / overlay-layer rules compose, and
    // each populated field overrides the active layout's own value at the overlay
    // build site. No engine-mode gate — an overlay-only context rule is first-class.
    if (!m_evaluator) {
        return ContextOverlayOverride{};
    }

    return resolveCachedContext(
        m_contextOverlayCache, m_contextOverlayCacheRevision, screenId, virtualDesktop, activity,
        [&]() -> ContextOverlayOverride {
            ContextOverlayOverride overlay;
            const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            const PWR::ResolvedActions resolved = m_evaluator->resolve(query);

            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayShader))) {
                const QString id = action->params.value(PWR::ActionParam::EffectId).toString();
                if (!id.isEmpty()) {
                    overlay.shaderId = id;
                    // Optional shader uniform overrides — empty when the rule
                    // overrides only the shader id (shader defaults apply).
                    overlay.shaderParams = action->params.value(PWR::ActionParam::Params).toObject().toVariantMap();
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayStyle))) {
                // Wire token → OverlayDisplayMode int so consumers compare against
                // the same enum Layout::overlayDisplayMode() exposes (0 =
                // ZoneRectangles, 1 = LayoutPreview).
                const QString token = action->params.value(PWR::ActionParam::Value).toString();
                if (token == QLatin1String("rectangles")) {
                    overlay.style = 0;
                } else if (token == QLatin1String("preview")) {
                    overlay.style = 1;
                }
            }
            return overlay;
        });
}

bool LayoutRegistry::hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    return findExactContextRule(screenId, virtualDesktop, activity) != nullptr;
}
const PhosphorWindowRules::WindowRule* LayoutRegistry::findExactContextRule(const QString& screenId, int virtualDesktop,
                                                                            const QString& activity) const
{
    // The deterministic v5 derivation lets us look up a stored assignment by
    // its identity tuple — the bridge guarantees identical tuples produce
    // identical ids. We scan the rule list once for the id (the linear walk
    // is unavoidable here because callers need a stable in-set pointer, not
    // the value-copy `ruleById` returns), then guard with
    // hasEngineModeAction + matchIsExactContext so a hand-edited match that
    // no longer satisfies the canonical context shape can never be returned
    // even if its id happens to match the deterministic derivation.
    //
    // The win over the previous implementation is the predicate cost: the
    // old scan called matchIsExactContext on EVERY rule's match expression;
    // this scan compares ids first and only evaluates the context-shape
    // predicate on the unique candidate.
    //
    // O(N) intentional: a hash lookup would need WindowRuleSet to expose a
    // pointer-returning accessor into its in-set storage with a documented
    // dangle-on-setRules contract — too sharp an edge for the win at the
    // rule counts we care about (<= ~1000 rules per profile). Revisit only
    // if rule counts grow well into the thousands.
    // Two-pass scan: prefer the deterministic-id rule (cheap id compare,
    // the canonical shape the bridge produces). If no rule has the
    // canonical id, fall back to a shape-based scan that picks up
    // user-authored PURE-assignment rules whose UUIDs were generated by
    // the settings UI (windowruletemplates.cpp's `QUuid::createUuid()`
    // path). The shape fallback gates on `isPureAssignmentRule` — a
    // user rule that ALSO carries SetOpacity / OverrideAnimation* /
    // Float / Exclude / LockContext alongside the assignment slots is
    // intentionally NOT claimed here. Admitting it would feed it through
    // upsertAssignmentRule / assignLayout / applyBatchAssignments which
    // rebuild via makeAssignmentActions and emit only the three slot
    // actions — silently stripping the user's other actions. Falling
    // through to the addRule branch instead preserves the user's data
    // (the duplicate-shadow at the same cascade band is a known limit;
    // documented at upsertAssignmentRule's contract).
    const QUuid candidateId = PWR::ContextRuleBridge::assignmentRuleIdFor(screenId, virtualDesktop, activity);
    const PWR::WindowRule* shapeMatch = nullptr;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (rule.id == candidateId) {
            if (!hasEngineModeAction(rule) || !matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
                // Deterministic-id rule exists but its match shape was
                // hand-edited away from the canonical form. Return
                // nothing so callers don't act on a malformed rule.
                return nullptr;
            }
            return &rule;
        }
        // Remember the first PURE-assignment rule we see — we only
        // return it if no deterministic-id rule exists in the set.
        if (shapeMatch == nullptr && isPureAssignmentRule(rule)
            && matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
            shapeMatch = &rule;
        }
    }
    return shapeMatch;
}

QUuid LayoutRegistry::exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    const PWR::WindowRule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    return rule ? rule->id : QUuid();
}

void LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    // Pass the entry's mode through `modeToWireString` so a future Mode
    // (e.g. Scrolling) round-trips without collapsing to Snapping/Autotile.
    // The earlier `bool autotile` shape silently produced "snapping" for any
    // mode that wasn't Autotile, which would corrupt a Scrolling assignment
    // on save and round-trip back as Snapping on load.
    const QString modeToken = modeToWireString(entry.mode);
    // Pass an empty rule name — the settings UI renders an auto-friendly
    // title from the rule's match (with lookup-resolved screen/activity
    // labels). Stamping a raw `screenId · Desktop N · Activity` here would
    // bake connector strings and activity UUIDs into the stored rule.
    PWR::WindowRule rule = PWR::ContextRuleBridge::makeAssignmentRule(
        QString(), screenId, virtualDesktop, activity, modeToken, entry.snappingLayout, entry.tilingAlgorithm);

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
    // De-duplicate (screenId, virtualDesktop) — two distinct rules pinned
    // to the same screen/desktop with different activities would otherwise
    // produce duplicate layoutAssigned emissions at the post-update loop
    // below, causing 2-3× redundant refresh work in observers (overlays,
    // settings tiles, autotile state). Note: the `layoutAssigned` signal
    // signature has no activity field, so activity-only rules on the
    // same screen (virtualDesktop=0) collapse to one `(screen, 0)` key —
    // observers that key on activity rely on the rule-store's broader
    // `rulesChanged` signal for their refresh instead of layoutAssigned.
    QSet<QPair<QString, int>> affected;
    bool changed = false;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        const bool referencesDeleted =
            std::any_of(rule.actions.cbegin(), rule.actions.cend(), [&layoutId](const PWR::RuleAction& action) {
                return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                    && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId;
            });
        if (!referencesDeleted) {
            kept.append(rule);
            continue;
        }
        changed = true;

        // Gate on isPureAssignmentRule (not isContextAssignmentRule) — the
        // Shape-1 rebuild path emits ONLY the three assignment slot actions
        // via makeAssignmentActions, so a mixed context rule carrying
        // SetOpacity / OverrideAnimation* / Float / Exclude / LockContext
        // alongside its assignment actions would silently lose those
        // non-assignment actions on rebuild. Mixed rules fall through to Shape 2's
        // surgical SetSnappingLayout removal, which preserves every
        // other action verbatim.
        if (isContextAssignmentRule(rule) && isPureAssignmentRule(rule)) {
            // Shape 1: rebuild the lossless context-action set with the dead
            // snapping reference cleared; mode + tilingAlgorithm survive.
            const AssignmentEntry entry = entryFromRuleMatchActions(rule);
            const ContextDims dims = decodeDims(rule.match);
            // Track the affected (screen, desktop) for the post-update
            // layoutAssigned emit — every observer keyed on this rule's
            // context needs to refresh, whether the rule was dropped or just
            // rebuilt.
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
            if (entry.mode == AssignmentEntry::Snapping && entry.tilingAlgorithm.isEmpty()) {
                // Nothing meaningful remains — a bare Snapping engine-mode is
                // the default. Drop the whole rule.
                qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                    << "— only a default Snapping mode remained after clearing the deleted layout";
                continue;
            }
            PWR::WindowRule rebuilt = rule;
            rebuilt.actions = PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(entry.mode), QString(),
                                                                            entry.tilingAlgorithm);
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
                                                     && action.params.value(PWR::ActionParam::LayoutId).toString()
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
        // Notify per-screen observers (overlays, autotile state, settings
        // tile caption, etc.) so they refresh against the new cascade.
        // Mirrors `clearAutotileAssignments`'s emit pattern — without it,
        // a layout delete left those consumers showing stale assignments.
        for (const auto& [sid, desk] : std::as_const(affected)) {
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
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
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenId);
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
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenId);
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
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenId);
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

} // namespace PhosphorZones
