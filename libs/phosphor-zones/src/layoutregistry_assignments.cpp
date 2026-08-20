// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.
//
// Phase 3b: per-context assignment is resolved on the unified Rule engine.
// There is one resolution model: a windowless WindowQuery evaluated through
// RuleEvaluator against the RuleStore's rule set, and the winner of each action
// slot is the highest-priority matching rule (ties broken by list order). There
// is no specificity formula and no provider-default tail — priority is the only
// precedence value, and a genuine miss (no rule fills any slot) routes to the
// settings-gated default. Connector-name / virtual-screen fallback is NOT a
// priority band — it is a query-side recursive key rewrite, kept here in
// layoutForScreen() / the shared resolve helper.

#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorRules;

// The rule-shape classification / context helpers (contextRuleName,
// decodeDims, the matchIsExactContext* family, hasEngineModeAction,
// isContextAssignmentRule, entryFromRuleMatchActions, makeContextQuery)
// are pure functions with no LayoutRegistry-member dependency and live in
// layoutregistry_rulehelpers.cpp / _p.h, keeping the rule-shape vocabulary in
// one place separate from the registry's member-bound resolution logic here.
using namespace RuleHelpers;

namespace {

/// Run @p tryOne against the query-side screen-id fallback chain — the original
/// id, then its connector-name → stable-id rewrite, then a virtual screen's
/// physical id — and return the first engaged optional, or an empty optional if
/// every variant misses. @p tryOne must return a @c std::optional; an engaged
/// optional holding a "settled" value (e.g. a non-null-but-empty Layout*) stops
/// the chain, exactly as the inline retries did. Centralizes the rewrite shared
/// by layoutForScreen / storedAssignmentIdForScreen (which assignmentIdForScreen
/// delegates to) / assignmentEntryForScreen / hasMatchingAssignmentRule /
/// scrollingTemplateForContext / scrollingTemplateExplicitlyNone so the six
/// callers cannot drift.
template<typename TryFn>
auto resolveWithScreenFallback(const QString& screenId, TryFn&& tryOne) -> decltype(tryOne(screenId))
{
    if (auto result = tryOne(screenId)) {
        return result;
    }
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (auto result = tryOne(resolved)) {
                return result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        if (auto result = tryOne(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId))) {
            return result;
        }
    }
    return {};
}

} // namespace

bool LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    // Pass the entry's mode through `modeToWireString` so a future Mode
    // (e.g. Scrolling) round-trips without collapsing to Snapping/Autotile.
    // The earlier `bool autotile` shape silently produced "snapping" for any
    // mode that wasn't Autotile, which would corrupt a Scrolling assignment
    // on save and round-trip back as Snapping on load.
    const QString modeToken = modeToWireString(entry.mode);

    const PWR::Rule* existing = findExactContextRule(screenId, virtualDesktop, activity);
    // Priority is the only precedence value (highest wins per slot). On UPDATE
    // preserve the rule's stored priority; on CREATE seed a winning top value so
    // a freshly authored assignment outranks any prior one.
    const int priority =
        existing != nullptr ? existing->priority : nextAssignmentPriority(m_ruleStore->ruleSet().rules());
    // Pass an empty rule name — the settings UI renders an auto-friendly
    // title from the rule's match (with lookup-resolved screen/activity
    // labels). Stamping a raw `screenId · Desktop N · Activity` here would
    // bake connector strings and activity UUIDs into the stored rule.
    PWR::Rule rule = PWR::ContextRuleBridge::makeAssignmentRule(QString(), screenId, virtualDesktop, activity,
                                                                modeToken, entry.snappingLayout, entry.tilingAlgorithm,
                                                                priority, entry.scrollingTemplateLayout);

    if (existing == nullptr) {
        // A rule may already hold this DETERMINISTIC id without being claimable
        // as an assignment — removeAssignmentRule strips the slot actions and
        // keeps the rule alive for whatever else it carried, and a user can
        // hand-edit one the same way. addRule rejects the colliding id, and
        // reporting that as success made the context permanently unassignable
        // with no diagnostic. Update the existing rule in place instead,
        // merging the new slot actions onto its surviving ones.
        const std::optional<PWR::Rule> byId = m_ruleStore->ruleSet().ruleById(rule.id);
        if (byId.has_value()) {
            PWR::Rule merged = rule;
            merged.name = byId->name;
            merged.managed = byId->managed;
            merged.enabled = byId->enabled;
            // Preserve the stored priority like the exact-rule branch above:
            // a reclaim is an update of an existing rule, and re-seeding it to
            // the fresh top value would silently change its precedence against
            // the other context rules (and defeat the no-op guard below, since
            // Rule::operator== compares priority).
            merged.priority = byId->priority;
            carryOverNonAssignmentActions(merged, *byId);
            if (merged == *byId) {
                return false;
            }
            return m_ruleStore->updateRule(merged);
        }
        return m_ruleStore->addRule(rule);
    }
    rule.id = existing->id; // preserve the rule's identity across the update
    // makeAssignmentRule always stamps enabled = true; an upsert must not
    // silently re-enable a rule the user disabled. A disabled context
    // assignment is still an explicit assignment — preserve the flag.
    rule.enabled = existing->enabled;
    // Preserve the user-facing identity too. makeAssignmentRule is handed an
    // empty name, and the shape fallback in findExactContextRule claims
    // user-authored pure assignment rules — which the rules editor lets the
    // user name — so rebuilding without these blanked the name on the next
    // Monitors-page write. It also broke the guard below: Rule::operator==
    // compares name, so a named rule never compared equal and every
    // identical re-apply still bumped the revision.
    rule.name = existing->name;
    rule.managed = existing->managed;
    // And every action that is not one of the four assignment slots. The
    // deterministic context id means this rebuild lands on the stored rule
    // regardless of any purity gate upstream, so a merge is the only
    // non-destructive rebuild. Runs before the no-op guard so a rule whose
    // extra actions are unchanged still compares equal.
    carryOverNonAssignmentActions(rule, *existing);
    // NO-OP GUARD. RuleSet::updateRule has no equality check of its own, so an
    // identical re-apply (the KCM's "apply all" over unchanged values) still
    // bumped the store's monotonic revision — which drops all five context
    // caches, rewrites rules.json and fans out rulesChanged — and the callers
    // then emitted layoutAssigned for a layout that did not change. Violates
    // "only emit signals when the value actually changes".
    //
    // Scoped deliberately to the WRITE, not to any downstream apply pass:
    // suppressing an apply is what previously broke the scrolling→snapping
    // restore. A genuine change still writes and still emits.
    if (rule == *existing) {
        return false;
    }
    m_ruleStore->updateRule(rule);
    return true;
}

bool LayoutRegistry::removeAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity)
{
    const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    if (rule == nullptr) {
        return false;
    }
    // Symmetric with the rebuild paths' merge. findExactContextRule claims
    // MIXED rules (a context assignment the user also hung a SetOpacity or
    // LockContext on), so a wholesale removeRule here would destroy those
    // extra actions when the user merely CLEARS the context's assignment on
    // the Monitors page. Strip the four assignment slots instead and keep the
    // rule alive for whatever else it carries; only delete it outright when
    // nothing survives. Same shape purgeLayoutIdFromAssignments already
    // uses for its Shape-2 rules.
    PWR::Rule stripped;
    carryOverNonAssignmentActions(stripped, *rule);
    if (stripped.actions.isEmpty()) {
        return m_ruleStore->removeRule(rule->id);
    }
    PWR::Rule kept = *rule;
    kept.actions = stripped.actions;
    return m_ruleStore->updateRule(kept);
}

bool LayoutRegistry::purgeLayoutIdFromAssignments(const QString& layoutId)
{
    // An id-keyed scrub serving BOTH deletion flows: a deleted manual
    // layout's id is scrubbed from SetSnappingLayout actions, and a deleted
    // native scrolling template's id from SetScrollingTemplate actions (the
    // two id namespaces are disjoint UUID sets, so one walk matching either
    // action type against the id is exact for both). The rule must lose the
    // dead reference, but NOT the whole rule, and NOT its other actions.
    //
    // Two rule shapes can carry such an action for the deleted id:
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
    QList<PWR::Rule> kept;
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
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        const bool referencesDeleted =
            std::any_of(rule.actions.cbegin(), rule.actions.cend(), [&layoutId](const PWR::RuleAction& action) {
                return (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                        || action.type == QLatin1String(PWR::ActionType::SetScrollingTemplate))
                    && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId;
            });
        if (!referencesDeleted) {
            kept.append(rule);
            continue;
        }
        changed = true;

        // Gate on isPureAssignmentRule (not isContextAssignmentRule) — the
        // Shape-1 rebuild path emits ONLY the four assignment slot actions
        // via makeAssignmentActions, so a mixed context rule carrying
        // SetOpacity / OverrideAnimation* / Float / Exclude / LockContext /
        // DefaultLayoutAssignment alongside its assignment actions would silently lose those
        // non-assignment actions on rebuild. Mixed rules fall through to Shape 2's
        // surgical SetSnappingLayout / SetScrollingTemplate removal, which
        // preserves every other action verbatim.
        if (isContextAssignmentRule(rule) && isPureAssignmentRule(rule)) {
            // Shape 1: rebuild the lossless context-action set with the dead
            // reference(s) cleared; every slot that does not point at the
            // deleted layout survives.
            AssignmentEntry entry = entryFromRuleMatchActions(rule);
            if (entry.snappingLayout == layoutId) {
                // Same verdict as the template arm below, for the same
                // reason: a Snapping context whose layout was just deleted
                // must not silently adopt the registry-wide default layout
                // (which is what an empty slot resolves to), so it takes the
                // explicit no-layout word. A non-Snapping context keeps the
                // plain clear — the slot is dormant data there, and the
                // sentinel would defeat the drop-if-nothing-remains test
                // below.
                entry.snappingLayout = entry.mode == AssignmentEntry::Snapping ? QString(NoSnappingLayout) : QString();
            }
            if (entry.scrollingTemplateLayout == layoutId) {
                // The reserved word rather than empty, but ONLY where the
                // template is actually in force. Empty means "inherit the
                // configured default", so clearing a Scrolling context's slot
                // handed a screen whose template was just deleted to whatever
                // OTHER template happened to be the default — silently
                // reshaping it with a template the user never picked. Writing
                // "explicitly none" leaves it with no template, which is what
                // deleting its template means and what the dangling-id path
                // already resolves to.
                //
                // A non-Scrolling context keeps the plain clear: the slot is
                // only a remembered value for a mode this context is not in,
                // the user never asked for an opt-out, and the sentinel would
                // also defeat the drop-if-nothing-remains test below and
                // leave the rule behind as clutter.
                entry.scrollingTemplateLayout =
                    entry.mode == AssignmentEntry::Scrolling ? QString(NoScrollingTemplate) : QString();
            }
            const ContextDims dims = decodeDims(rule.match);
            // Track the affected (screen, desktop) for the post-update
            // layoutAssigned emit — every observer keyed on this rule's
            // context needs to refresh, whether the rule was dropped or just
            // rebuilt.
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
            // No drop-if-nothing-remains test here, deliberately. It used to
            // delete a rule left with mode Snapping and three empty payloads,
            // on the reasoning that a bare Snapping engine-mode is just the
            // default. That reasoning predates the explicit no-layout state:
            // a bare Snapping mode on an EXACT context rule is now a
            // first-class pin (hasExplicitSnappingModePin reads exactly this
            // shape, and resolveStoredAssignmentId answers engaged-empty for
            // it rather than falling through to default synthesis), and the
            // Monitors page stages it whenever a context leaves Scrolling
            // under a suppressed default.
            //
            // Every rule reaching this point is that pin: Shape 1 is gated on
            // isContextAssignmentRule, which already requires both an exact
            // context match and a SetEngineMode action. So the only way to
            // arrive with all payloads empty is a pin whose one dormant
            // template slot the purge just cleared — dropping it silently
            // changed the context's mode resolution once suppression lifted
            // or the level-1 default resolved to something other than
            // Snapping. A rule whose SNAPPING layout was deleted never
            // arrives empty: it takes the reserved word above.
            PWR::Rule rebuilt = rule;
            rebuilt.actions =
                PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(entry.mode), entry.snappingLayout,
                                                              entry.tilingAlgorithm, entry.scrollingTemplateLayout);
            kept.append(rebuilt);
            qCDebug(lcZonesLib) << "purgeLayoutIdFromAssignments: rebuilt context rule" << rule.id.toString()
                                << "— cleared the deleted layout's references, preserved the other slots";
            continue;
        }

        // Shape 2: a window-property (or otherwise non-context) rule. Only the
        // SetSnappingLayout / SetScrollingTemplate actions referencing the
        // deleted id are touched (removed, or rewritten to the no-template word
        // for a Scrolling context — see the gate below); every other action is
        // preserved verbatim.
        //
        // A MIXED context rule (context-only match + assignment actions +
        // some non-assignment action) lands here too — it fails
        // isPureAssignmentRule but still carries a context whose observers
        // must refresh, so record it in `affected` for the layoutAssigned
        // emit below, exactly like the Shape-1 branch. A PURE window-property
        // rule deliberately gets no entry: it has no context dims to key on,
        // and it cannot influence context resolution anyway (the windowless
        // context query never matches its window leaves, and slotMatch drops
        // window-negating rules), so there is no engine state to re-derive —
        // the store write's rulesChanged still reaches every projection
        // consumer.
        // Context-shaped for this purpose means "pins one exact context AND
        // fills at least one assignment slot" — deliberately WIDER than
        // isContextAssignmentRule, which additionally demands a SetEngineMode.
        // A LAYOUT-ONLY exact-context rule (a SetSnappingLayout or
        // SetScrollingTemplate with no mode action) is a first-class supported
        // shape: findExactContextRule claims it through the same
        // hasAnyAssignmentSlotAction test, and the batch rebuild spares it.
        // Gating on the narrower predicate meant such a rule got neither the
        // sentinel rewrite nor an `affected` entry, so deleting its layout
        // erased the action outright, dropped the rule, and handed the context
        // to the registry-wide default — the exact silent reshape the reserved
        // word exists to prevent, and a contract this method publishes.
        const bool contextShape = rule.match.isContextOnly()
            && (matchIsExactContextBase(rule.match) || matchIsExactContextDesktop(rule.match)
                || matchIsExactContextActivity(rule.match));
        const bool contextRule = contextShape && (hasEngineModeAction(rule) || hasAnyAssignmentSlotAction(rule));
        if (contextRule) {
            const ContextDims dims = decodeDims(rule.match);
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
        }
        PWR::Rule trimmed = rule;
        // A Scrolling context takes the SAME opt-out Shape 1 writes, for the
        // same reason: empty means "inherit the configured default", so
        // ERASING the action here handed a screen whose template was just
        // deleted to whatever other template happened to be the default. The
        // rewrite runs BEFORE the erase below and replaces the id with a word
        // no UUID matches, so the erase leaves the action standing.
        //
        // Gated on the rule being a CONTEXT assignment in Scrolling mode.
        // A non-Scrolling context, and any pure window-property rule, keeps
        // the plain erase: the slot there is either a dormant value for a mode
        // the context is not in or not a context slot at all, the user asked
        // for no opt-out, and the sentinel would keep a rule alive that the
        // empty-actions drop below should have taken.
        if (contextRule) {
            const AssignmentEntry::Mode ruleMode = entryFromRuleMatchActions(rule).mode;
            // A rule that declares NO engine mode puts none of its slots out
            // of force — the slot it carries IS its assignment, which is why
            // the exact-context lookup admits the shape at all. Keying the
            // rewrite on the mode would be wrong there:
            // entryFromRuleMatchActions defaults a mode-less rule to Snapping,
            // so a template-only rule would take the snapping arm, miss the
            // template rewrite, and have its action erased instead. Key on
            // whether the rule declares a mode, and fall back to rewriting
            // whichever slot actually names the deleted id.
            const bool declaresMode = hasEngineModeAction(rule);
            const bool scrollingSlotInForce = declaresMode ? ruleMode == AssignmentEntry::Scrolling : true;
            const bool snappingSlotInForce = declaresMode ? ruleMode == AssignmentEntry::Snapping : true;
            if (scrollingSlotInForce) {
                for (PWR::RuleAction& action : trimmed.actions) {
                    if (action.type == QLatin1String(PWR::ActionType::SetScrollingTemplate)
                        && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId) {
                        action.params.insert(PWR::ActionParam::LayoutId, QString(NoScrollingTemplate));
                    }
                }
            }
            // The snapping twin, gated the same way: only a context that is
            // IN Snapping mode takes the opt-out word when its layout is
            // deleted — erasing the action would hand the screen to the
            // registry-wide default layout the user never picked. A dormant
            // SetSnappingLayout on a non-Snapping context keeps the plain
            // erase.
            if (snappingSlotInForce) {
                for (PWR::RuleAction& action : trimmed.actions) {
                    if (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                        && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId) {
                        action.params.insert(PWR::ActionParam::LayoutId, QString(NoSnappingLayout));
                    }
                }
            }
        }
        trimmed.actions.erase(
            std::remove_if(trimmed.actions.begin(), trimmed.actions.end(),
                           [&layoutId](const PWR::RuleAction& action) {
                               return (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                                       || action.type == QLatin1String(PWR::ActionType::SetScrollingTemplate))
                                   && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId;
                           }),
            trimmed.actions.end());
        if (trimmed.actions.isEmpty()) {
            // The rule's only action was the dead reference — nothing
            // meaningful remains, so drop it.
            qCDebug(lcZonesLib) << "purgeLayoutIdFromAssignments: dropped rule" << rule.id.toString()
                                << "— its only action referenced the deleted layout or template reference";
            continue;
        }
        // No bare-default drop here, deliberately. Shape 1 needs one; this
        // branch keeps what it trims. Any rule reaching here either carries a
        // non-slot action alongside its mode (which Shape 1 would have
        // destroyed on rebuild, so it was routed here on purpose), or is a
        // catch-all we deliberately keep — isContextAssignmentRule rejects
        // catch-alls, so a pure global assignment rule lands in this branch,
        // and a lone SetEngineMode on a catch-all is a real global default,
        // not the no-op a per-context bare mode would be.
        kept.append(trimmed);
        qCDebug(lcZonesLib) << "purgeLayoutIdFromAssignments: trimmed rule" << rule.id.toString()
                            << "— cleared the deleted id (a Scrolling context's template slot and a Snapping "
                               "context's layout slot are rewritten to the reserved none word rather than removed), "
                               "kept all other actions";
    }
    if (changed) {
        if (!m_ruleStore->setAllRules(kept)) {
            // The in-memory rule set is mutated and rulesChanged(false) has
            // fired even on a save failure, so the running session stays
            // consistent — but rules.json still holds the deleted id and the
            // scrub (including the sentinel rewrites) is undone on the next
            // daemon start. Nothing here can retry a failed save; leave the
            // trace the silent discard used to swallow.
            qCWarning(lcZonesLib) << "purgeLayoutIdFromAssignments: rule-store save failed — the scrub of" << layoutId
                                  << "is not persisted; the stale store reloads on next start and the dangling id"
                                  << "degrades at resolve time until something rewrites the rules";
        }
        // Notify per-screen observers (overlays, autotile state, settings
        // tile caption, etc.) so they refresh against the new cascade —
        // without it, a layout delete left those consumers showing stale
        // assignments. Unlike clearAutotileAssignments, which routes through
        // emitLayoutAssigned to carry the layout the context now resolves to,
        // this path emits a bare nullptr payload. Consumers are null-safe and
        // re-derive from the cascade, and the deletion has already invalidated
        // whatever the old payload named.
        for (const auto& [sid, desk] : std::as_const(affected)) {
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
    }

    // Quick slots carry the same ids, so the scrub is not complete until they
    // are swept too — a slot still holding a deleted id would resurrect it on
    // the next shortcut press. Every mode's array is swept by id: the manual
    // layout and native template id namespaces are disjoint UUID sets (the
    // same property the rule walk above relies on), so an id-keyed sweep
    // across all three arrays cannot touch a live binding of another kind.
    bool slotRemoved = false;
    for (auto& slots : m_quickLayoutSlots) {
        for (auto it = slots.begin(); it != slots.end();) {
            if (it.value() == layoutId) {
                it = slots.erase(it);
                slotRemoved = true;
            } else {
                ++it;
            }
        }
    }
    if (slotRemoved) {
        writeQuickLayouts();
        // No slot-changed signal exists at this level, deliberately: the
        // refresh hint is the daemon's D-Bus quickLayoutSlotsChanged, and both
        // adaptor delete verbs already emit it after a purge. The template
        // verb gates the emit on this function's bool return
        // (LayoutAdaptor::deleteScrollingTemplate); the layout verb
        // (LayoutAdaptor::deleteLayout) emits unconditionally, since the
        // registry call it drives the purge through returns void and a spare
        // refresh is harmless. So the registry stays signal-free.
    }

    return changed || slotRemoved;
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
        if (const PWR::Rule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
            entry = entryFromRuleMatchActions(*existing);
        }
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layout->id().toString();
        // The return value is deliberately IGNORED. upsertAssignmentRule
        // suppresses a redundant WRITE (no revision bump, no rules.json
        // rewrite, no cache drop), which is the part that was violating
        // emit-on-change. layoutAssigned is not a value-changed signal
        // though: it is the sole trigger for updateEngineScreens() and
        // updateLayoutFilter(), so an idempotent re-apply that a caller
        // issues precisely to force a re-derive must still fan out. This is
        // the shape that previously broke the scrolling→snapping restore
        // when a "duplicate" pass was suppressed.
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        qCDebug(lcZonesLib) << "assignLayout: screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity)
                            << "layout=" << layout->name();
    } else {
        // Clearing: strip the exact-shape rule's assignment slots.
        // removeAssignmentRule deletes the rule outright only when nothing
        // else survives on it — a rule carrying other actions keeps them.
        // Skip the signal when there was nothing to remove.
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
    // The tiling-family sentinels ("autotile:…" and the bare "scrolling:")
    // carry no Layout* to resolve, so both route through the entry-upsert
    // path. Classification goes through AssignmentEntry::fromLayoutId —
    // the ONE mode cascade — seeded from the exact-shape rule so the
    // sibling mode's stored fields survive (the lossless-toggle contract).
    // A hand-rolled isAutotile-only branch here once made a D-Bus
    // assign("scrolling:") fall into the null-Layout arm of assignLayout,
    // which CLEARS the assignment.
    //
    // The reserved no-layout word routes through the same entry-upsert path,
    // for the same reason: it names no Layout*, so the else-arm below would
    // parse it to a null id and CLEAR the assignment — turning "explicitly
    // none" into "inherit the default", the exact state the caller opted out
    // of. fromLayoutId's non-UUID arm stores the word verbatim under Snapping
    // mode (its autotile arm handles "autotile:none" with no help). This is
    // the snapping/autotile write choke point the token's doc names.
    if (PhosphorLayout::LayoutId::isAutotile(layoutId) || PhosphorLayout::LayoutId::isScrolling(layoutId)
        || layoutId == NoSnappingLayout) {
        AssignmentEntry existing;
        if (const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity)) {
            existing = entryFromRuleMatchActions(*rule);
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, existing);
        // Emit unconditionally — see assignLayout: the write suppression is
        // real, but layoutAssigned drives the engine-screen re-derive and an
        // idempotent D-Bus assign must still reach it.
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    } else {
        // Deliberate asymmetry with assignScrollingTemplate's unresolvable-id
        // arm (which stores the no-template word): an unknown or stale layout
        // UUID here resolves to nullptr and assignLayout's null arm CLEARS
        // the assignment, so the context inherits the default. The D-Bus
        // assign verbs pre-validate layout existence, so this arm fires only
        // for internal callers or an id deleted between validation and apply
        // — states where the caller never expressed an opt-out, and where
        // inheriting the default matches what every pre-opt-out caller
        // expects of a failed assign. Rewriting to the opt-out word here
        // would turn an internal error into a user-visible "this screen now
        // has no layout".
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutRegistry::assignScrollingTemplate(const QString& screenId, int virtualDesktop, const QString& activity,
                                             const QString& templateId)
{
    // Seed from the exact-shape rule so the sibling mode's stored fields
    // survive (the lossless-toggle contract), exactly like assignLayoutById's
    // sentinel branch. The mode flips to Scrolling: picking a template from
    // the picker IS choosing scrolling semantics for the context, and a
    // template on a non-scrolling entry would be dead data the cascade never
    // reads.
    AssignmentEntry entry;
    if (const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity)) {
        entry = entryFromRuleMatchActions(*rule);
    }
    entry.mode = AssignmentEntry::Scrolling;
    // Three arguments, three stored values: the reserved word stores itself,
    // an empty argument clears the slot so the context inherits the configured
    // default, and anything else is a template id — normalized to the canonical
    // braced form when it resolves, and turned into the reserved word when it
    // does not.
    if (templateId == NoScrollingTemplate) {
        // The reserved word bypasses the UUID normalization below, which is
        // the WRITE half of the token's contract (scrollingTemplateForContext
        // is the read half). Normalizing it would parse to a null QUuid and
        // store an empty field, silently turning "explicitly none" into
        // "inherit the default" — the exact state the user asked not to be in.
        entry.scrollingTemplateLayout = QString(NoScrollingTemplate);
    } else if (templateId.isEmpty()) {
        // The documented CLEAR form: drop this context's own choice so it
        // inherits the configured default again. Only an empty argument means
        // that — see the unresolvable arm below, which deliberately does not.
        entry.scrollingTemplateLayout.clear();
    } else {
        QUuid parsed = QUuid::fromString(templateId);
        // Existence validation against the native template store. Without a
        // wired store (some tests, embedders) a well-formed id is stored as-is
        // and the resolver degrades it at read time instead.
        const bool unknownToStore =
            !parsed.isNull() && m_scrollingTemplateStore && !m_scrollingTemplateStore->contains(parsed);
        if (parsed.isNull() || unknownToStore) {
            // The caller named a template that does not resolve, and the
            // reserved word — not empty — is what that means. Empty would
            // inherit the configured DEFAULT template, so a mistyped or
            // already-deleted id silently reshaped the screen with a template
            // nobody picked for it. This is the same verdict
            // purgeLayoutIdFromAssignments reaches when the template a
            // Scrolling context was using is deleted, and the entry is
            // Scrolling by construction here (the mode is stamped above).
            qCDebug(lcZonesLib) << "assignScrollingTemplate: unresolvable template" << templateId
                                << "— storing the explicit no-template word rather than inheriting the default";
            entry.scrollingTemplateLayout = QString(NoScrollingTemplate);
        } else {
            // Normalized to the canonical braced form at this ONE choke point
            // so every caller (adaptor, controller, registry) stores one
            // spelling. A braceless bus-supplied uuid stored verbatim would
            // defeat the purge's exact string compare on template delete and
            // the upsert no-op guard's byte-wise action compare.
            entry.scrollingTemplateLayout = parsed.toString();
        }
    }
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
    // Emit unconditionally — see assignLayout: the write suppression is real,
    // but layoutAssigned drives the engine-screen re-derive (which pushes the
    // template vocabulary), and an idempotent re-apply must still reach it.
    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
}

PhosphorZones::ScrollingTemplate
LayoutRegistry::scrollingTemplateForContext(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Mode-gated BY DESIGN: a template preserved on a non-Scrolling context
    // (the lossless-toggle contract) must not resolve — the engine push and
    // the picker consume the LIVE template only. The raw field-inspection
    // twin is scrollingTemplateLayoutForScreen, which reads the stored field
    // regardless of mode (parity with snappingLayoutForScreen).
    //
    // Connector-name / virtual-screen fallback applies here exactly as it does
    // to the sibling cascade readers: a virtual sub-screen inheriting its
    // physical screen's Scrolling assignment must inherit that assignment's
    // template too. The mode gate stays on the RESOLVED entry, so a fallback
    // hop that lands on a non-Scrolling entry still resolves no template.
    const auto entry = resolveWithScreenFallback(screenId, [this, virtualDesktop, &activity](const QString& sid) {
        return resolveAssignmentEntry(sid, virtualDesktop, activity);
    });
    if (!entry || entry->mode != AssignmentEntry::Scrolling) {
        return {};
    }
    // "Explicitly none" short-circuits BEFORE the default fallback, and that
    // ordering is the whole feature: this context opted out of templates, so
    // a default someone set later must not creep back in. Checked here and
    // nowhere else — see the token's own doc for why every other reader can
    // stay ignorant of it.
    if (entry->scrollingTemplateLayout == NoScrollingTemplate) {
        return {};
    }
    QUuid id = QUuid::fromString(entry->scrollingTemplateLayout);
    // A cascade entry naming no template falls back to the configured
    // DEFAULT template (parity with snapping's default layout). The
    // provider is daemon-injected; local settings/KCM registries without
    // one resolve no default.
    if (id.isNull() && m_defaultScrollingTemplateProvider) {
        id = QUuid::fromString(m_defaultScrollingTemplateProvider());
    }
    if (id.isNull() || !m_scrollingTemplateStore) {
        return {};
    }
    // A deleted or unknown template id degrades to "no template" — the
    // caller falls back to the engine's compiled defaults. templateById
    // answers an invalid template for unknown ids, so no extra validation
    // is needed.
    return m_scrollingTemplateStore->templateById(id);
}

bool LayoutRegistry::scrollingTemplateExplicitlyNone(const QString& screenId, int virtualDesktop,
                                                     const QString& activity) const
{
    // Same resolution and same mode gate as scrollingTemplateForContext, so
    // the two can never disagree about which entry they are describing.
    const auto entry = resolveWithScreenFallback(screenId, [this, virtualDesktop, &activity](const QString& sid) {
        return resolveAssignmentEntry(sid, virtualDesktop, activity);
    });
    return entry && entry->mode == AssignmentEntry::Scrolling && entry->scrollingTemplateLayout == NoScrollingTemplate;
}

void LayoutRegistry::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                              const AssignmentEntry& entry)
{
    // Store the entry — mode-only entries (empty snapping + empty tiling) are
    // valid when explicitly set by the KCM to preserve mode at a context
    // level. An identical re-apply writes nothing, but still emits: this is a
    // D-Bus entry point and layoutAssigned is what re-derives the engine
    // screens, so an idempotent call made to force that re-derive must reach
    // it. See assignLayout for the full reasoning.
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);

    qCDebug(lcZonesLib) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                        << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm << "template=" << entry.scrollingTemplateLayout;

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
    //   - {nullptr} -> an entry exists but yields no snap Layout* (any
    //                  non-Snapping mode, or a snap entry with empty/unknown
    //                  layout id) — the cascade is settled, fall through to
    //                  defaultLayout().
    //   - {layout}  -> resolved snap layout.
    //
    // The explicit no-layout state cannot be expressed in that triple —
    // "settled with no Layout*" already means "fall through to
    // defaultLayout()" — so it rides a side flag: a Snapping entry carrying
    // the reserved NoSnappingLayout word settles the chain AND suppresses the
    // default fallback below. Mode-gated like the template resolver's None
    // arm: a "none" preserved on a non-Snapping entry (the lossless
    // mode-toggle contract) is dormant data, not an opt-out in force.
    bool explicitlyNone = false;
    auto tryResolve = [this, virtualDesktop, &activity,
                       &explicitlyNone](const QString& sid) -> std::optional<PhosphorZones::Layout*> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt; // genuine miss — the caller may retry
        }
        // The autotile opt-out is an opt-out here too, and the test must run
        // BEFORE the non-Snapping settle below (which would swallow it into
        // the plain {nullptr} that still falls through to defaultLayout()).
        // Without this arm an autotile:none screen resolves the registry-wide
        // default, and the ungated consumers (zone detection, window drag,
        // resnap) snap windows into zones no overlay ever draws — the exact
        // defeat of the opt-out this flag exists to prevent on the snapping
        // side. A REAL algorithm id must not take this arm: those screens
        // legitimately fall through (autotile-mode resolution is the engine's
        // job; the default layout here is transition scaffolding).
        if (entry->mode == AssignmentEntry::Autotile && entry->tilingAlgorithm == NoTilingAlgorithm) {
            explicitlyNone = true;
            return std::optional<PhosphorZones::Layout*>(nullptr);
        }
        // An entry exists; the cascade is settled — never retry. Any
        // NON-Snapping mode has no snap Layout*: an Autotile or Scrolling
        // entry may still CARRY a non-empty snappingLayout (the lossless
        // mode-toggle contract preserves the field), and resolving it here
        // would hand ungated consumers (zone detection, window drag) zones
        // from a layout the screen is not using. A snap entry with an empty
        // layout id settles the same way.
        if (entry->mode != AssignmentEntry::Snapping || entry->snappingLayout.isEmpty()) {
            return std::optional<PhosphorZones::Layout*>(nullptr);
        }
        // "Explicitly none" short-circuits BEFORE the default fallback, and
        // that ordering is the whole feature (the exact shape
        // scrollingTemplateForContext gives templates): this context opted
        // out of layouts, so the registry-wide default must not creep back
        // in. Every hop that sets the flag also settles the chain, so the
        // flag cannot go stale across fallback retries.
        if (entry->snappingLayout == NoSnappingLayout) {
            explicitlyNone = true;
            return std::optional<PhosphorZones::Layout*>(nullptr);
        }
        return std::optional<PhosphorZones::Layout*>(layoutById(QUuid::fromString(entry->snappingLayout)));
    };

    // Connector-name then virtual-screen fallback (the physical screen's
    // assignment is inherited). A settled {nullptr} stops the chain; a genuine
    // miss (nullopt) and a {nullptr} both defer to the registry-wide default —
    // unless the settle was the explicit opt-out above, which answers no
    // layout at all. Every snap-engine consumer already tolerates a null here
    // (SnapResult::noSnap and friends), so "no zones" is a safe engine state.
    // layoutForScreen returns a snap Layout* and has no autotile counterpart;
    // autotile-mode resolution is the autotile engine's job.
    const auto result = resolveWithScreenFallback(screenId, tryResolve);
    if (explicitlyNone) {
        return nullptr;
    }
    return (result && *result) ? *result : defaultLayout();
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

// Shared predicate for the two cascade visitors below: a resolved entry
// with an EMPTY activeLayoutId() is still an EXPLICIT Snapping mode pin
// when the user stored an ENABLED exact-context rule for this tuple that
// actually carries a SetEngineMode action — the Monitors page stages one
// when leaving Scrolling while the context suppresses the default layout,
// so there is no layout to pin. Requiring the mode action keeps a
// layout-only exact rule (which pins no mode) from anchoring an entry
// whose mode came from elsewhere. Both visitors MUST honour this the same
// way, or modeForScreen and assignmentIdForScreen disagree about the same
// context (mode Snapping while the id path synthesizes the default tier's
// autotile id). (Autotile and Scrolling mode-only entries stay visible
// through their bare id sentinels; only Snapping has no sentinel because
// its ids are layout UUIDs.)
bool LayoutRegistry::hasExplicitSnappingModePin(const QString& sid, int virtualDesktop, const QString& activity,
                                                const AssignmentEntry& entry) const
{
    if (entry.mode != AssignmentEntry::Snapping) {
        return false;
    }
    const PWR::Rule* exact = findExactContextRule(sid, virtualDesktop, activity);
    // `enabled` is belt-and-braces: the evaluator already skips disabled
    // rules, so a resolution that reaches this predicate normally came from
    // an enabled rule — but findExactContextRule deliberately IGNORES the
    // flag (upsert semantics), so the conjunct keeps the pin from anchoring
    // on a disabled exact rule should the two ever pair up via a broader
    // enabled rule resolving the same tuple.
    return exact != nullptr && exact->enabled && hasEngineModeAction(*exact);
}

QString LayoutRegistry::assignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                              const QString& activity) const
{
    // The stored cascade walk lives in resolveStoredAssignmentId — this
    // method is that walk plus the level-1 default tail, and delegating keeps
    // the two from ever resolving the cascade differently. The optional's
    // ENGAGED-but-empty state is load-bearing: an explicit mode-only Snapping
    // pin settles the chain with no layout identity, and treating that as a
    // miss would synthesize the default tier's autotile id here.
    if (const auto stored = resolveStoredAssignmentId(screenId, virtualDesktop, activity)) {
        return *stored;
    }

    // No stored entry in the cascade — fall through to the level-1 global
    // default via the injected providers, honoring the global suppress setting
    // and any per-context DefaultLayoutAssignment override.
    const AssignmentEntry def = resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);
    return def.activeLayoutId();
}

QString LayoutRegistry::storedAssignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return resolveStoredAssignmentId(screenId, virtualDesktop, activity).value_or(QString());
}

std::optional<QString> LayoutRegistry::resolveStoredAssignmentId(const QString& screenId, int virtualDesktop,
                                                                 const QString& activity) const
{
    // Shared cascade with layoutForScreen, accepting any entry whose
    // activeLayoutId() is non-empty (incl. Autotile entries), but a miss
    // stays a miss: no level-1 default synthesis. Distinguishes "this
    // context has its own assignment (exact or rule-based)" from "the
    // resolver would hand out the registry-wide default". Connector /
    // virtual-screen fallback applies here too.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<QString> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        const QString id = entry->activeLayoutId();
        if (id.isEmpty()) {
            // An explicit mode-only Snapping pin SETTLES the chain with an
            // empty id (there is genuinely no layout identity) instead of
            // deferring to a sibling screen or the default tier — keeping
            // this API consistent with assignmentEntryForScreen's mode.
            if (hasExplicitSnappingModePin(sid, virtualDesktop, activity, *entry)) {
                return std::optional<QString>(QString());
            }
            return std::nullopt;
        }
        return std::optional<QString>(id);
    };

    // Disengaged, NOT an empty string: an engaged-empty result means "the
    // cascade settled on a mode-only Snapping pin", which assignmentIdForScreen
    // reads as "do not synthesize the default tier".
    return resolveWithScreenFallback(screenId, tryResolve);
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
            // See hasExplicitSnappingModePin above: an explicit mode-only
            // Snapping pin is accepted; anything else defers to the
            // connector/VS fallback and then the default tier.
            if (hasExplicitSnappingModePin(sid, virtualDesktop, activity, *entry)) {
                return entry;
            }
            return std::nullopt;
        }
        return entry;
    };

    if (const auto result = resolveWithScreenFallback(screenId, tryResolve)) {
        return *result;
    }

    // Cascade miss — synthesize from the level-1 global default, honoring the
    // global suppress setting and any per-context DefaultLayoutAssignment override.
    return resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);
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

QString LayoutRegistry::scrollingTemplateLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).scrollingTemplateLayout;
}

bool LayoutRegistry::hasMatchingAssignmentRule(const QString& screenId, int virtualDesktop,
                                               const QString& activity) const
{
    return resolveWithScreenFallback(screenId,
                                     [this, virtualDesktop, &activity](const QString& sid) {
                                         return resolveAssignmentEntry(sid, virtualDesktop, activity);
                                     })
        .has_value();
}

bool LayoutRegistry::isDefaultAssignmentSuppressedForContext(const QString& screenId, int virtualDesktop,
                                                             const QString& activity) const
{
    // The "is the synthesized default suppressed" primitive: a per-context
    // DefaultLayoutAssignment override decides locally, otherwise the global gate.
    if (const auto contextOverride = resolveContextDefaultAssignment(screenId, virtualDesktop, activity)) {
        return !*contextOverride;
    }
    return m_defaultAssignmentSuppressedProvider && m_defaultAssignmentSuppressedProvider();
}

bool LayoutRegistry::isContextActiveLayoutSuppressed(const QString& screenId, int virtualDesktop,
                                                     const QString& activity) const
{
    // An active layout exists (explicit assignment at any cascade level, or a
    // default forced through by an "allow" override) — never suppressed.
    if (!assignmentIdForScreen(screenId, virtualDesktop, activity).isEmpty()) {
        return false;
    }
    // An enabled assignment-family rule covers this context (mode, layout,
    // OR algorithm slot — any authored intent overrides the global suppress
    // setting). The context stays active even when the rule sets only one
    // slot: the rest falls back to the default exactly as it did before
    // suppression existed, and the overlay / zone selector show.
    if (hasMatchingAssignmentRule(screenId, virtualDesktop, activity)) {
        return false;
    }
    // No active layout and no covering assignment rule. Report suppressed ONLY
    // when the cause is the suppress feature (per-context override or the global
    // gate). Any OTHER empty-assignment state (e.g. snapping enabled with no
    // global default layout id) returns false, so callers keep their existing
    // defaultLayout() fallback there.
    return isDefaultAssignmentSuppressedForContext(screenId, virtualDesktop, activity);
}

} // namespace PhosphorZones
