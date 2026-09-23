// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "core/platform/logging.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QSet>
#include <QStringList>

#include <optional>

namespace PlasmaZones {

using namespace settings_detail;

// ── Per-mode disable lists — rule-backed (Phase 3b) ─────────────────────────
//
// Per-mode disable entries are `DisableEngine` context rules in the unified
// Rule store (rules.json), NOT config.json keys. A disable rule
// pins exactly the context dimensions of its axis:
//   - monitor  : ScreenId only
//   - desktop  : ScreenId + VirtualDesktop
//   - activity : ScreenId + Activity
// and carries one `DisableEngine` action whose `mode` param ("snapping" /
// "autotile") scopes which engine it gates. The deterministic shape lets the
// getters enumerate the store and the setters replace a whole (axis, mode)
// family without touching the other axes / the other mode.

namespace {

// Classify a disable rule's MATCH into a disable axis. Returns nullopt for a
// shape no disable axis can represent — those are not managed disable entries,
// so the getter must not report them and the write path's kept-walk must not
// rewrite them. Routed through `ContextRuleBridge::contextAxisFor` so the
// cascade-axis formula lives in one place, and so the three admission gates that
// formula folds in apply here identically to the assignment side:
//   - `isContextOnly()` — a rule carrying a window-property leaf is not a
//     context rule at all. Rebuilding it from the decoded dims would drop that
//     leaf, and `disableEntryFor` has nowhere to put it.
//   - `pinsNonDimensionContextField()` — a rule pinning Mode /
//     TiledWindowCount / ScreenOrientation / ActiveLayout is more specific than
//     the (screen, desktop, activity) shape the entry strings can key. Taking it
//     as a bare axis rule would BOTH over-report the gate (an
//     `All{ScreenId==A, ScreenOrientation=="portrait"}` disable would switch the
//     engine off on screen A in every orientation) and destroy the extra leaf on
//     the next rebuild of the family.
//   - the axis decode itself, below.
//
// STRICT, like the assignment side's `matchIsExactContextActivityStrict`: a
// Combined (screen+desktop+activity) tuple is NOT the Activity axis. The three
// disable-list keys are `screenId`, `screenId/desktop` and `screenId/activity`
// — none of them can carry all three dimensions, so projecting a Combined rule
// onto the Activity axis loses information both ways:
//   - disableEntriesFor() would serialize it as `screenId/activity`, dropping
//     the desktop pin, and the rebuild loop below would recreate it from that
//     entry with desktop=0 — silently widening a rule that gated ONE desktop
//     into one that gates ALL desktops (under a different disableRuleIdFor
//     UUID, so it is a different rule, not an edit).
//   - a Combined rule and a plain Activity rule for the same (screen,
//     activity) produce the SAME entry string, so canonicalDisableEntries'
//     de-duplication would collapse the pair and the rebuild would emit only
//     one — losing the other outright.
// Excluded here instead, a Combined disable rule is invisible to the disable
// lists (it cannot be keyed by them) and the rebuild's kept-walk preserves it
// untouched, exactly as it already does for a rule that pins a non-dimension
// context field. Nothing in-tree authors one — the v3→v4 migration emits
// desktop-only and activity-only disables — so this is only reachable for a
// rule hand-built in the rule editor, which is precisely the rule the disable
// lists must not rewrite.
std::optional<DisableAxis> axisOf(const PhosphorRules::MatchExpression& match)
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    switch (CRB::contextAxisFor(match)) {
    case CRB::ContextAxis::Monitor:
        return DisableAxis::Monitor;
    case CRB::ContextAxis::Desktop:
        return DisableAxis::Desktop;
    case CRB::ContextAxis::Activity:
        return DisableAxis::Activity;
    case CRB::ContextAxis::Combined:
    case CRB::ContextAxis::CatchAll:
        return std::nullopt;
    }
    return std::nullopt;
}

// Serialize a decoded (screen, desktop, activity) tuple into the disable-list
// entry string for @p axis. The ONE place the entry shape is spelled, shared by
// the getter (disableEntriesFor) and the write path's kept-walk — the two have
// to agree on what "the entry for this rule" is, or the kept-walk would fail to
// recognise the very entries the getter produced.
QString disableEntryFor(DisableAxis axis, const QString& screenId, int desktop, const QString& activity)
{
    switch (axis) {
    case DisableAxis::Monitor:
        return screenId;
    case DisableAxis::Desktop:
        return screenId + QLatin1Char('/') + QString::number(desktop);
    case DisableAxis::Activity:
        return screenId + QLatin1Char('/') + activity;
    }
    return QString();
}

} // namespace

QStringList Settings::disableEntriesFor(PhosphorZones::AssignmentEntry::Mode mode, int axisInt) const
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    const auto axis = static_cast<DisableAxis>(axisInt);
    const QString wantToken = PhosphorZones::modeToWireString(mode);
    QStringList out;
    for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
        if (!rule.enabled) {
            // A disabled rule does not gate anything. The evaluator skips
            // !enabled before filling any slot, and these lists ARE the gate
            // (isMonitorDisabled reads them straight), so honouring a disabled
            // rule here would keep an engine switched off that the user had
            // explicitly toggled off in the rule editor. The rule itself is
            // preserved — see the kept-walk in writeDisableEntries, which must
            // not delete what this getter no longer reports.
            continue;
        }
        const auto ruleToken = CRB::disableRuleMode(rule);
        if (!ruleToken || *ruleToken != wantToken) {
            continue; // not a disable rule, or scoped to a different mode
        }
        // axisOf() is the single admission gate — see its comment for the three
        // shapes it refuses (window-property leaves, a pinned non-dimension
        // context field, an axis the entry strings cannot key).
        const auto ruleAxis = axisOf(rule.match);
        if (!ruleAxis || *ruleAxis != axis) {
            continue;
        }
        QString screenId;
        int desktop = 0;
        QString activity;
        CRB::contextDimsOf(rule.match, screenId, desktop, activity);
        out.append(disableEntryFor(axis, screenId, desktop, activity));
    }
    return out;
}

void Settings::writeDisableEntries(PhosphorZones::AssignmentEntry::Mode mode, int axisInt, const QStringList& entries,
                                   DisableModeSignalFn signalFn)
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    const auto axis = static_cast<DisableAxis>(axisInt);
    const QString modeToken = PhosphorZones::modeToWireString(mode);

    // Snapshot the current entries for this (axis, mode) so a no-op write does
    // not fire a spurious changed signal. Both sides go through
    // canonicalDisableEntries — see it for what the canonical form is and why
    // a raw compare is not enough.
    const QStringList before = canonicalDisableEntries(axis, disableEntriesFor(mode, axisInt));
    const QStringList after = canonicalDisableEntries(axis, entries);

    // No-op guard: when the canonical disable sets are equal there is nothing
    // to persist. Skip the kept-rebuild + setAllRules walk entirely — the
    // rebuild would produce a structurally identical rule list
    // (makeDisableRule is deterministic via disableRuleIdFor's createUuidV5
    // over (screenId, desktop, activity, modeToken), so the ids match
    // byte-for-byte and setAllRules would correctly detect "no change"),
    // but walking every rule in the store and allocating fresh Rule
    // copies just to discover that is wasted work. Pure micro-optimisation,
    // not a correctness guard against UUID churn.
    if (before == after) {
        return;
    }

    // Rebuild the rule list: keep every rule that is NOT a disable rule of
    // this exact (axis, mode) family, then append the new entries.
    QList<PhosphorRules::Rule> kept;
    for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
        const auto ruleToken = CRB::disableRuleMode(rule);
        // axisOf() gates admission with exactly the shapes disableEntriesFor
        // reports, so this walk drops only rules the append loop below rebuilds.
        // Anything it refuses — a window-property leaf, a pinned non-dimension
        // context field (Mode / TiledWindowCount / ScreenOrientation /
        // ActiveLayout), a Combined tuple — is NOT the pure (screen, desktop,
        // activity) shape this (axis, mode) rewrite owns, so it falls through to
        // `kept` untouched. Dropping it would destroy the rule outright: the
        // append loop rebuilds only the bare tuple, so the extra leaf would be
        // gone and the rule's derived UUID with it.
        if (ruleToken && *ruleToken == modeToken) {
            const auto ruleAxis = axisOf(rule.match);
            if (ruleAxis && *ruleAxis == axis) {
                QString screenId;
                int desktop = 0;
                QString activity;
                CRB::contextDimsOf(rule.match, screenId, desktop, activity);
                // A DISABLED rule of this family is invisible to
                // disableEntriesFor, so it is in neither `before` nor `after` and
                // the append loop below will not recreate it. Dropping it here
                // would silently delete the user's rule on the next unrelated
                // write to this (axis, mode) family. Keep it — unless this write
                // re-asserts its exact entry, in which case the append loop
                // rebuilds the same tuple as an ENABLED rule with the same
                // deterministic id. The append loop runs after this walk, so
                // keeping the disabled one too would hand setAllRules the
                // disabled rule first, and RuleSet::setRules keeps the FIRST
                // entry for any id and drops later collisions — the rebuilt
                // ENABLED rule would be the one thrown away, leaving the
                // re-enable a silent no-op. Ticking a monitor back on in the UI
                // is precisely that case, and re-enabling the rule is what the
                // user asked for.
                const QStringList canonical =
                    canonicalDisableEntries(axis, {disableEntryFor(axis, screenId, desktop, activity)});
                const bool reasserted = !canonical.isEmpty() && after.contains(canonical.first());
                if (rule.enabled || reasserted) {
                    continue; // drop — replaced below
                }
            }
        }
        kept.append(rule);
    }

    for (const QString& canonicalEntry : after) {
        QString screenId;
        int desktop = 0;
        QString activity;
        switch (axis) {
        case DisableAxis::Monitor:
            screenId = canonicalEntry;
            break;
        case DisableAxis::Desktop: {
            // No shape check: `after` came out of canonicalDisableEntries, which
            // already dropped every entry this loop could reject and rebuilt the
            // survivors as `<non-empty screen>/<QString::number(desktop > 0)>`.
            // The screen segment is non-empty (resolveScreenId returns its input
            // unchanged when it doesn't resolve) and the desktop segment never
            // contains '/', so lastIndexOf always lands on the separator we put
            // there and the tail always parses back to the same positive int.
            const int slash = canonicalEntry.lastIndexOf(QLatin1Char('/'));
            desktop = canonicalEntry.mid(slash + 1).toInt();
            screenId = canonicalEntry.left(slash);
            break;
        }
        case DisableAxis::Activity: {
            // Use lastIndexOf so a disambiguated screen ID
            // (`Manuf:Model:Serial/CONNECTOR`, see
            // libs/phosphor-screens/include/PhosphorScreens/ScreenIdentity.h)
            // splits at the activity boundary, not at the connector
            // boundary inside the screen ID. Activity UUIDs are
            // canonical and never contain `/`, so the trailing segment
            // is unambiguous. Mirrors the Desktop axis above, including
            // the shape guarantee canonicalDisableEntries provides.
            const int slash = canonicalEntry.lastIndexOf(QLatin1Char('/'));
            screenId = canonicalEntry.left(slash);
            activity = canonicalEntry.mid(slash + 1);
            break;
        }
        }
        // Compose the rule name with the same axis suffix the v3→v4
        // migration uses (see `configmigration.cpp::disableRuleForDesktop`
        // / `disableRuleForActivity`). Without the suffix, a runtime-
        // authored desktop or activity disable rule shows up in the rule
        // editor as the bare monitor-prefix label (`"Snapping off · DP-1"`),
        // visually indistinguishable from a monitor-axis rule for the same
        // screen — even though the underlying `(screen, desktop, activity)`
        // tuple is what `disableRuleIdFor` keys the v5 UUID off. Two
        // different rules with the same display name confuses the editor's
        // dedup-on-name heuristic and obscures the actual scope.
        QString name = disableRulePrefixFor(mode) + screenId;
        if (axis == DisableAxis::Desktop) {
            name += disableRuleDesktopSuffix(desktop);
        } else if (axis == DisableAxis::Activity) {
            name += disableRuleActivitySuffix();
        }
        kept.append(CRB::makeDisableRule(name, screenId, desktop, activity, modeToken, CRB::kContextBandBase));
    }

    if (!m_ruleStore->setAllRules(kept)) {
        // Persistence failed — the in-memory rule set still advanced,
        // so consumers wired to `rulesChanged(persisted=false)` already
        // know. Surface it on the settings-side log too so users
        // grepping `lcConfig` see the failure. Then try to roll the
        // in-memory store back to its on-disk state (mirrors the
        // reset() rollback below) so subsequent reads through this
        // Settings instance return the same view as cross-process
        // consumers reading the unmodified file.
        //
        // The aggregate `settingsChanged()` emit is GATED on whether that
        // rollback actually restored the original set (see the
        // emit-on-change note below it), NOT skipped outright. In the
        // double-failure case the rollback leaves the value diverged and
        // the emit does fire, so a dirty-state tracker can re-read the
        // advanced in-memory value and mark itself clean while disk still
        // holds the old list. That is the lesser of the two divergences:
        // staying silent instead strands every disable-list UI on a set
        // the getters no longer report.
        //
        // BEST-EFFORT, not guaranteed: load() deliberately keeps the
        // in-memory set when it cannot read the file, and an unreadable
        // file is a plausible reason the persist failed in the first
        // place. In that double failure the in-memory state stays
        // diverged from disk until the next successful save or load.
        // Nothing better is available here — inventing a second recovery
        // path for a store we already know we cannot write would be
        // guessing.
        qCWarning(lcConfig) << "writeDisableEntries: failed to persist window-rule store for mode" << mode << "axis"
                            << axisInt;
        m_ruleStore->load();
        // Emit only if the rollback did NOT restore the original set — which is exactly the
        // emit-on-change rule, applied to the state we actually ended up in.
        //
        // The usual case is that load() puts the entries back where they started, so nothing
        // changed and nothing is announced; firing then would make every disable-list UI
        // re-read a set identical to the one it is already showing. But load() is best-effort
        // (it keeps the advanced in-memory set when it cannot read the file, and an unreadable
        // file is a plausible reason the write failed in the first place), so the rollback can
        // leave the value MOVED. Announcing nothing there would strand every UI on the old
        // list while the getters report the new one — which is the same silent divergence,
        // just from the other side.
        if (canonicalDisableEntries(axis, disableEntriesFor(mode, axisInt)) != before) {
            Q_EMIT(this->*signalFn)(mode);
            Q_EMIT settingsChanged();
        }
        return;
    }

    Q_EMIT(this->*signalFn)(mode);
    Q_EMIT settingsChanged();
}

QStringList Settings::disabledMonitors(PhosphorZones::AssignmentEntry::Mode mode) const
{
    // Resolve connector names → stable screen ids on every read, through the
    // same resolveScreenId helper canonicalDisableEntries uses — an
    // unresolvable name therefore stays the name here too, instead of
    // collapsing to an empty entry. Stored connector names stay human-readable;
    // consumers see canonical ids.
    // Desktop/activity getters intentionally skip this resolution because
    // their composite keys (`screenId/desktop`, `screenId/activity`) embed
    // the screen id in a non-isolatable way; the connector↔id matching is
    // done at lookup time inside isDesktopDisabled / isActivityDisabled.
    QStringList entries = disableEntriesFor(mode, static_cast<int>(DisableAxis::Monitor));
    for (auto& name : entries) {
        name = resolveScreenId(name);
    }
    // Order-preserving dedup: a connector-name entry and an id-form entry can
    // resolve to the same screen id, and consumers expect one entry per screen.
    // The QSet carries the membership check so the pass stays O(n) instead of
    // the O(n^2) list-contains scan; the list preserves first-seen order.
    QStringList deduped;
    deduped.reserve(entries.size());
    QSet<QString> seen;
    seen.reserve(entries.size());
    for (const QString& entry : std::as_const(entries)) {
        if (!seen.contains(entry)) {
            seen.insert(entry);
            deduped.append(entry);
        }
    }
    return deduped;
}

void Settings::setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& screenIdOrNames)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Monitor), screenIdOrNames,
                        &Settings::disabledMonitorsChanged);
}

bool Settings::isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName) const
{
    const QStringList entries = disabledMonitors(mode);
    for (const QString& name : PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName)) {
        if (entries.contains(name)) {
            return true;
        }
    }
    return false;
}

// Composite keys (`screenId/desktop`) are returned verbatim — connector-name
// resolution is deferred to isDesktopDisabled, which knows how to split the
// composite and match either the connector or the resolved id segment.
QStringList Settings::disabledDesktops(PhosphorZones::AssignmentEntry::Mode mode) const
{
    return disableEntriesFor(mode, static_cast<int>(DisableAxis::Desktop));
}

void Settings::setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Desktop), entries, &Settings::disabledDesktopsChanged);
}

bool Settings::isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                                 int desktop) const
{
    if (desktop <= 0) {
        return false;
    }
    const QStringList entries = disabledDesktops(mode);
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    const QString desktopStr = QString::number(desktop);
    for (const QString& name : namesToCheck) {
        if (entries.contains(name + QLatin1Char('/') + desktopStr)) {
            return true;
        }
    }
    return false;
}

// Composite keys (`screenId/activityUuid`) are returned verbatim — see the
// comment on disabledDesktops for why connector-name resolution is deferred
// to isActivityDisabled rather than applied per-read here.
QStringList Settings::disabledActivities(PhosphorZones::AssignmentEntry::Mode mode) const
{
    return disableEntriesFor(mode, static_cast<int>(DisableAxis::Activity));
}

void Settings::setDisabledActivities(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Activity), entries, &Settings::disabledActivitiesChanged);
}

bool Settings::isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                                  const QString& activityId) const
{
    if (activityId.isEmpty()) {
        return false;
    }
    const QStringList entries = disabledActivities(mode);
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    for (const QString& name : namesToCheck) {
        if (entries.contains(name + QLatin1Char('/') + activityId)) {
            return true;
        }
    }
    return false;
}

// ── Screen / context locking (PhosphorConfig::Store-backed) ─────────────────

QStringList Settings::lockedScreens() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey()));
}
void Settings::setLockedScreens(const QStringList& screens)
{
    // Whole-replace writers (the settings app pushing a full list) still
    // deserve a fresh no-op compare, so refresh here. The composite path
    // (setContextLocked) refreshed before ITS read and calls
    // writeLockedScreens below instead — refreshing again here would reopen
    // the read-to-write window that its top-of-function refresh closed, by
    // pulling in a concurrent writer's commit AFTER the merged list was built
    // and then overwriting it.
    refreshCleanBackendFromDisk();
    writeLockedScreens(screens);
}

void Settings::writeLockedScreens(const QStringList& screens)
{
    // Post-write compare — see writeDisableEntries for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey(),
                   screens.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    if (before == after) {
        return;
    }
    Q_EMIT lockedScreensChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isScreenLocked(const QString& screenIdOrName) const
{
    return isContextLocked(screenIdOrName, 0, QString());
}
void Settings::setScreenLocked(const QString& screenIdOrName, bool locked)
{
    setContextLocked(screenIdOrName, 0, QString(), locked);
}

bool Settings::isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const
{
    const QStringList locked = lockedScreens();
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    for (const QString& name : namesToCheck) {
        if (virtualDesktop > 0 && !activity.isEmpty()) {
            const QString k = name + QLatin1Char(':') + QString::number(virtualDesktop) + QLatin1Char(':') + activity;
            if (locked.contains(k)) {
                return true;
            }
        }
        if (virtualDesktop > 0) {
            const QString k = name + QLatin1Char(':') + QString::number(virtualDesktop);
            if (locked.contains(k)) {
                return true;
            }
        }
        if (locked.contains(name)) {
            return true;
        }
    }
    return false;
}

void Settings::setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity, bool locked)
{
    // Read-modify-write over the whole lockedScreens list, and the daemon's
    // lock toggle and the settings app's D-Bus writes are concurrent writers.
    // Refresh a clean backend BEFORE the lockedScreens() read below — a
    // refresh inside setLockedScreens would run after the merge was already
    // built from a stale cache and persist a list missing any entry another
    // process committed in the meantime.
    refreshCleanBackendFromDisk();

    // Composite-key format mirrors isContextLocked():
    //   name                          → per-screen lock
    //   name:<desktop>                → per-(screen,desktop) lock
    //   name:<desktop>:<activity>     → per-(screen,desktop,activity) lock
    //
    // (screen, 0, "activity") has no representable form in the current
    // schema. Silently composing just `name` would land the caller on a
    // whole-screen lock while their intent was activity-scoped — that's
    // data loss masquerading as success. Reject loudly at the boundary so
    // the caller can either supply a desktop or accept the screen scope
    // explicitly with an empty activity.
    if (virtualDesktop <= 0 && !activity.isEmpty()) {
        qCWarning(lcConfig) << "Settings::setContextLocked: activity supplied without a virtual desktop —"
                            << "(screen, 0, activity) cannot be expressed in the lockedScreens schema."
                            << "Refusing to write a whole-screen lock that would silently drop the activity.";
        return;
    }
    QString key = screenIdOrName;
    if (virtualDesktop > 0) {
        key += QLatin1Char(':') + QString::number(virtualDesktop);
        if (!activity.isEmpty()) {
            key += QLatin1Char(':') + activity;
        }
    }
    QStringList current = lockedScreens();
    if (locked && !current.contains(key)) {
        current.append(key);
        writeLockedScreens(current);
    } else if (!locked && current.removeAll(key) > 0) {
        writeLockedScreens(current);
    }
}

} // namespace PlasmaZones
