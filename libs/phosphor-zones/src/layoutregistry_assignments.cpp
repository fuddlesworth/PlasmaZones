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

#include <QFile>
#include <QJsonDocument>
#include <optional>

namespace PhosphorZones {

namespace {

namespace PWR = PhosphorWindowRule;

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

// True if a leaf predicate is an exact `Field == value` equality.
bool isEqualsLeaf(const PWR::MatchExpression& expr, PWR::Field field)
{
    return expr.isLeaf() && expr.predicate().field == field && expr.predicate().op == PWR::Operator::Equals;
}

// True if @p rule's match is exactly the context shape for the pinned
// dimensions of (screenId, virtualDesktop, activity) — i.e. the rule the
// ContextRuleBridge would emit for that tuple. A rule pinning more or fewer
// dimensions, or pinning different values, is NOT an exact match. This is
// what hasExplicitAssignment relies on to distinguish a stored entry from a
// wider cascade entry or the synthesized provider default.
bool matchIsExactContext(const PWR::MatchExpression& match, const QString& screenId, int virtualDesktop,
                         const QString& activity)
{
    const bool wantScreen = !screenId.isEmpty();
    const bool wantDesktop = virtualDesktop > 0;
    const bool wantActivity = !activity.isEmpty();
    const int wantCount = (wantScreen ? 1 : 0) + (wantDesktop ? 1 : 0) + (wantActivity ? 1 : 0);

    // Collect the (field → value) equality leaves of the match expression.
    QList<PWR::MatchExpression> leaves;
    if (match.isCatchAll()) {
        // empty All{} — only an exact match for the all-default tuple.
        return wantCount == 0;
    }
    if (match.isLeaf()) {
        leaves.append(match);
    } else if (match.kind() == PWR::MatchExpression::Kind::All) {
        leaves = match.children();
    } else {
        return false; // Any{} / None{} are never a context-assignment shape.
    }
    if (leaves.size() != wantCount) {
        return false;
    }

    bool sawScreen = false;
    bool sawDesktop = false;
    bool sawActivity = false;
    for (const PWR::MatchExpression& leaf : leaves) {
        if (!leaf.isLeaf()) {
            return false;
        }
        const PWR::MatchExpression::Predicate& p = leaf.predicate();
        if (p.op != PWR::Operator::Equals) {
            return false;
        }
        if (p.field == PWR::Field::ScreenId) {
            if (!wantScreen || p.value.toString() != screenId) {
                return false;
            }
            sawScreen = true;
        } else if (p.field == PWR::Field::VirtualDesktop) {
            if (!wantDesktop || p.value.toInt() != virtualDesktop) {
                return false;
            }
            sawDesktop = true;
        } else if (p.field == PWR::Field::Activity) {
            if (!wantActivity || p.value.toString() != activity) {
                return false;
            }
            sawActivity = true;
        } else {
            return false;
        }
    }
    return sawScreen == wantScreen && sawDesktop == wantDesktop && sawActivity == wantActivity;
}

// True if @p rule carries an engine-mode action — i.e. it is an assignment
// rule (as opposed to a pure DisableEngine / Exclude / animation rule).
bool isAssignmentRule(const PWR::WindowRule& rule)
{
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QString(PWR::ActionType::SetEngineMode)) {
            return true;
        }
    }
    return false;
}

// Decompose a context rule's match into (screenId, virtualDesktop, activity).
void contextDimsOf(const PWR::MatchExpression& match, QString& screenId, int& virtualDesktop, QString& activity)
{
    screenId.clear();
    virtualDesktop = 0;
    activity.clear();
    if (match.isCatchAll()) {
        return;
    }
    const QList<PWR::MatchExpression> leaves = match.isLeaf() ? QList<PWR::MatchExpression>{match} : match.children();
    for (const PWR::MatchExpression& leaf : leaves) {
        if (isEqualsLeaf(leaf, PWR::Field::ScreenId)) {
            screenId = leaf.predicate().value.toString();
        } else if (isEqualsLeaf(leaf, PWR::Field::VirtualDesktop)) {
            virtualDesktop = leaf.predicate().value.toInt();
        } else if (isEqualsLeaf(leaf, PWR::Field::Activity)) {
            activity = leaf.predicate().value.toString();
        }
    }
}

// Shape predicates for the per-screen-base / per-desktop / per-activity
// context rule families — used by the batch setters to drop one family
// before writing the new entries.
bool matchIsExactContextBase(const PWR::MatchExpression& match)
{
    QString sid;
    int desk = 0;
    QString act;
    contextDimsOf(match, sid, desk, act);
    return !sid.isEmpty() && desk == 0 && act.isEmpty();
}
bool matchIsExactContextDesktop(const PWR::MatchExpression& match)
{
    QString sid;
    int desk = 0;
    QString act;
    contextDimsOf(match, sid, desk, act);
    return !sid.isEmpty() && desk > 0 && act.isEmpty();
}
bool matchIsExactContextActivity(const PWR::MatchExpression& match)
{
    QString sid;
    int desk = 0;
    QString act;
    contextDimsOf(match, sid, desk, act);
    return !sid.isEmpty() && !act.isEmpty();
}

// Build the AssignmentEntry encoded directly by a rule's action list (no
// evaluation — used by introspection helpers like desktopAssignments()).
AssignmentEntry entryFromRuleMatchActions(const PWR::WindowRule& rule)
{
    AssignmentEntry entry;
    for (const PWR::RuleAction& action : rule.actions) {
        if (action.type == QString(PWR::ActionType::SetEngineMode)) {
            entry.mode = action.params.value(QLatin1String("mode")).toString() == QLatin1String("autotile")
                ? AssignmentEntry::Autotile
                : AssignmentEntry::Snapping;
        } else if (action.type == QString(PWR::ActionType::SetSnappingLayout)) {
            entry.snappingLayout = action.params.value(QLatin1String("layoutId")).toString();
        } else if (action.type == QString(PWR::ActionType::SetTilingAlgorithm)) {
            entry.tilingAlgorithm = action.params.value(QLatin1String("algorithm")).toString();
        }
    }
    return entry;
}

} // anonymous namespace

// ── Rule-backed cascade resolution ──────────────────────────────────────────

AssignmentEntry LayoutRegistry::entryFromRuleActions(const PWR::ResolvedActions& actions)
{
    // Retained for API stability — reads what a ResolvedActions can express.
    // Note: SetSnappingLayout and SetTilingAlgorithm share the `layout` slot,
    // so a ResolvedActions can only carry one of the two layout fields. The
    // cascade resolver (resolveAssignmentEntry) therefore reads the winning
    // rule's full action list directly to preserve mode-toggle losslessness.
    AssignmentEntry entry;
    if (const auto modeSlot = actions.slot(QString(PWR::ActionSlot::EngineMode))) {
        const QString mode = modeSlot->params.value(QLatin1String("mode")).toString();
        entry.mode = (mode == QLatin1String("autotile")) ? AssignmentEntry::Autotile : AssignmentEntry::Snapping;
    }
    if (const auto layoutSlot = actions.slot(QString(PWR::ActionSlot::Layout))) {
        entry.snappingLayout = layoutSlot->params.value(QLatin1String("layoutId")).toString();
        entry.tilingAlgorithm = layoutSlot->params.value(QLatin1String("algorithm")).toString();
    }
    return entry;
}

std::optional<AssignmentEntry> LayoutRegistry::resolveAssignmentEntry(const QString& screenId, int virtualDesktop,
                                                                      const QString& activity) const
{
    // The cascade is reproduced by the rule set's descending-priority order.
    // The legacy walkCascade treated the provider default as a MISS
    // (resolveDefaultAssignmentEntry handled it via injected lambdas), so the
    // catch-all rule is excluded here — only PINNED context rules count as a
    // cascade hit.
    //
    // SetSnappingLayout and SetTilingAlgorithm share the `layout` action slot,
    // so the winning rule's full action list — not a ResolvedActions — is
    // read, preserving the mode-toggle losslessness invariant (an entry keeps
    // BOTH snappingLayout and tilingAlgorithm regardless of active mode).
    const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);

    const PWR::WindowRule* winner = nullptr;
    int winnerPriority = 0;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!rule.enabled || !isAssignmentRule(rule) || rule.match.isCatchAll()) {
            continue;
        }
        if (!rule.match.evaluate(query)) {
            continue;
        }
        // Descending priority; ties broken by list order (first wins). The
        // rule set preserves insertion order, so the first rule seen at the
        // highest priority is the tie-break winner.
        if (winner == nullptr || rule.priority > winnerPriority) {
            winner = &rule;
            winnerPriority = rule.priority;
        }
    }
    if (winner == nullptr) {
        return std::nullopt;
    }
    return entryFromRuleMatchActions(*winner);
}

bool LayoutRegistry::hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    return !exactContextRuleId(screenId, virtualDesktop, activity).isNull();
}

QUuid LayoutRegistry::exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!isAssignmentRule(rule)) {
            continue;
        }
        if (matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
            return rule.id;
        }
    }
    return QUuid();
}

void LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    const bool autotile = (entry.mode == AssignmentEntry::Autotile);
    PWR::WindowRule rule = PWR::ContextRuleBridge::makeAssignmentRule(
        screenId + (virtualDesktop > 0 ? QStringLiteral(" · Desktop ") + QString::number(virtualDesktop) : QString())
            + (activity.isEmpty() ? QString() : QStringLiteral(" · Activity")),
        screenId, virtualDesktop, activity, autotile, entry.snappingLayout, entry.tilingAlgorithm);

    const QUuid existing = exactContextRuleId(screenId, virtualDesktop, activity);
    if (existing.isNull()) {
        m_ruleStore->addRule(rule);
    } else {
        rule.id = existing; // preserve the rule's identity across the update
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

// ── Mutators ────────────────────────────────────────────────────────────────

void LayoutRegistry::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                  PhosphorZones::Layout* layout)
{
    if (layout) {
        // Preserve an existing tilingAlgorithm — only mode + snappingLayout
        // change (the mode-toggle losslessness invariant).
        AssignmentEntry entry;
        if (const auto resolved = resolveAssignmentEntry(screenId, virtualDesktop, activity);
            resolved && hasExactContextRule(screenId, virtualDesktop, activity)) {
            entry = *resolved;
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
        // existing snappingLayout.
        AssignmentEntry entry;
        if (const auto resolved = resolveAssignmentEntry(screenId, virtualDesktop, activity);
            resolved && hasExactContextRule(screenId, virtualDesktop, activity)) {
            entry = *resolved;
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
    // rewrite, not a priority band. Resolve with the given screenId; on a
    // miss, re-resolve with the rewritten id.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<PhosphorZones::Layout*> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        if (entry->mode == AssignmentEntry::Autotile) {
            // An Autotile entry has no snap Layout* counterpart — the legacy
            // cascade visitor returned nullopt for it so the lookup fell
            // through to the global default. Mirror that.
            return std::nullopt;
        }
        if (entry->snappingLayout.isEmpty()) {
            return std::nullopt;
        }
        PhosphorZones::Layout* layout = layoutById(QUuid::fromString(entry->snappingLayout));
        return layout ? std::optional<PhosphorZones::Layout*>(layout) : std::nullopt;
    };

    if (const auto result = tryResolve(screenId)) {
        return *result;
    }
    // Connector-name fallback.
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result;
            }
        }
    }
    // Virtual-screen fallback — inherit the physical screen's assignment.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result;
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
        if (!isAssignmentRule(rule)) {
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
        QString sid;
        int desk = 0;
        QString act;
        contextDimsOf(rule.match, sid, desk, act);
        affected.append(qMakePair(sid, desk));
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

void LayoutRegistry::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    // Snapshot the existing per-screen base entries so the "other" mode field
    // is preserved (batch-setting a snapping layout keeps the tilingAlgorithm).
    QHash<QString, AssignmentEntry> oldBase;
    for (const QString& screenId : assignments.keys()) {
        if (hasExactContextRule(screenId, 0, QString())) {
            if (const auto entry = resolveAssignmentEntry(screenId, 0, QString())) {
                oldBase.insert(screenId, *entry);
            }
        }
    }

    // Remove every per-screen base assignment rule, then write the new ones.
    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (isAssignmentRule(rule) && matchIsExactContextBase(rule.match)) {
            continue; // drop — a per-screen base rule
        }
        kept.append(rule);
    }

    int count = 0;
    QSet<QString> storedScreens;
    QList<PWR::WindowRule> additions;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key();
        const QString& layoutId = it.value();
        if (screenId.isEmpty()) {
            qCWarning(lcZonesLib) << "Skipping assignment with empty screen ID";
            continue;
        }
        if (shouldSkipLayoutAssignment(layoutId, QStringLiteral("screen ") + screenId)) {
            continue;
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldBase.value(screenId));
        additions.append(PWR::ContextRuleBridge::makeAssignmentRule(screenId, screenId, 0, QString(),
                                                                    entry.mode == AssignmentEntry::Autotile,
                                                                    entry.snappingLayout, entry.tilingAlgorithm));
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to screen" << screenId;
    }
    kept.append(additions);
    m_ruleStore->setAllRules(kept);

    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "screen assignments";
}

void LayoutRegistry::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    QHash<QPair<QString, int>, AssignmentEntry> oldDesktop;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const int desktop = it.key().second;
        if (hasExactContextRule(screenId, desktop, QString())) {
            if (const auto entry = resolveAssignmentEntry(screenId, desktop, QString())) {
                oldDesktop.insert(it.key(), *entry);
            }
        }
    }

    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (isAssignmentRule(rule) && matchIsExactContextDesktop(rule.match)) {
            continue; // drop — a per-desktop rule
        }
        kept.append(rule);
    }

    int count = 0;
    QSet<QString> storedScreens;
    QList<PWR::WindowRule> additions;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const int virtualDesktop = it.key().second;
        const QString& layoutId = it.value();
        if (screenId.isEmpty() || virtualDesktop < 1) {
            qCWarning(lcZonesLib) << "Skipping invalid desktop assignment:" << screenId << virtualDesktop;
            continue;
        }
        const QString context = QStringLiteral("%1 desktop %2").arg(screenId).arg(virtualDesktop);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldDesktop.value(it.key()));
        additions.append(PWR::ContextRuleBridge::makeAssignmentRule(
            screenId + QStringLiteral(" · Desktop ") + QString::number(virtualDesktop), screenId, virtualDesktop,
            QString(), entry.mode == AssignmentEntry::Autotile, entry.snappingLayout, entry.tilingAlgorithm));
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << screenId << "desktop" << virtualDesktop;
    }
    kept.append(additions);
    m_ruleStore->setAllRules(kept);

    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, m_currentVirtualDesktop,
                           assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "desktop assignments";
}

void LayoutRegistry::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    QHash<QPair<QString, QString>, AssignmentEntry> oldActivity;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const QString& activityId = it.key().second;
        if (hasExactContextRule(screenId, 0, activityId)) {
            if (const auto entry = resolveAssignmentEntry(screenId, 0, activityId)) {
                oldActivity.insert(it.key(), *entry);
            }
        }
    }

    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (isAssignmentRule(rule) && matchIsExactContextActivity(rule.match)) {
            continue; // drop — a per-activity rule
        }
        kept.append(rule);
    }

    int count = 0;
    QSet<QString> storedScreens;
    QList<PWR::WindowRule> additions;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const QString& activityId = it.key().second;
        const QString& layoutId = it.value();
        if (screenId.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcZonesLib) << "Skipping invalid activity assignment:" << screenId << activityId;
            continue;
        }
        const QString context = QStringLiteral("%1 activity %2").arg(screenId, activityId);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldActivity.value(it.key()));
        additions.append(PWR::ContextRuleBridge::makeAssignmentRule(
            screenId + QStringLiteral(" · Activity"), screenId, 0, activityId, entry.mode == AssignmentEntry::Autotile,
            entry.snappingLayout, entry.tilingAlgorithm));
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << screenId << "activity" << activityId;
    }
    kept.append(additions);
    m_ruleStore->setAllRules(kept);

    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "activity assignments";
}

QHash<QPair<QString, int>, QString> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!isAssignmentRule(rule)) {
            continue;
        }
        QString sid;
        int desk = 0;
        QString activity;
        contextDimsOf(rule.match, sid, desk, activity);
        // Per-desktop: virtualDesktop > 0 and activity empty.
        if (desk > 0 && activity.isEmpty()) {
            result[qMakePair(sid, desk)] = entryFromRuleMatchActions(rule).activeLayoutId();
        }
    }
    return result;
}

QHash<QPair<QString, QString>, QString> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!isAssignmentRule(rule)) {
            continue;
        }
        QString sid;
        int desk = 0;
        QString activity;
        contextDimsOf(rule.match, sid, desk, activity);
        // Per-activity: activity non-empty (any desktop value).
        if (!activity.isEmpty()) {
            result[qMakePair(sid, activity)] = entryFromRuleMatchActions(rule).activeLayoutId();
        }
    }
    return result;
}

// Autotile layout overrides

QJsonObject LayoutRegistry::loadAllAutotileOverrides() const
{
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void LayoutRegistry::saveAllAutotileOverrides(const QJsonObject& all)
{
    ensureLayoutDirectory();
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to save autotile overrides:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(all).toJson());
}

QJsonObject LayoutRegistry::loadAutotileOverrides(const QString& algorithmId) const
{
    return loadAllAutotileOverrides().value(algorithmId).toObject();
}

void LayoutRegistry::saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides)
{
    QJsonObject all = loadAllAutotileOverrides();
    if (overrides.isEmpty()) {
        all.remove(algorithmId);
    } else {
        all[algorithmId] = overrides;
    }
    saveAllAutotileOverrides(all);
}

} // namespace PhosphorZones
