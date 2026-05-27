// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSettingsUi/PageController.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include "windowrulemodel.h"

namespace PlasmaZones {

/**
 * @brief Q_PROPERTY surface for the unified "Window Rules" settings page.
 *
 * Replaces `AnimationsPageController`'s app-rule block, `SnappingBridge`, and
 * `TilingBridge` with a single controller. It owns one `WindowRuleModel`
 * (`QAbstractListModel`) and a staging copy of the rule set:
 *
 *   1. On construction (and on a daemon `rulesChanged` D-Bus signal while the
 *      page is clean) it fetches the rule set from
 *      `org.plasmazones.WindowRules` and loads it into the model.
 *   2. Every QML CRUD call mutates the in-memory model and flips the dirty bit.
 *   3. On `commit()` the staged rule set is pushed back to the daemon via
 *      `setAllRules` — the daemon stays the sole writer of `windowrules.json`.
 *   4. On `revert()` the staged set is re-fetched from the daemon and the
 *      staged edits discarded — but only if the daemon is reachable; a failed
 *      re-fetch leaves the page dirty rather than silently dropping the edits.
 *
 * Dirty-tracking + revert/commit mirror `AnimationsPageController`'s
 * pending-changes contract: `SettingsController` connects `dirtyChanged` to
 * `setNeedsSave`, calls `commit()` from `save()`, and `revert()` from `load()`.
 *
 * The controller is the only thing on the settings side that knows the D-Bus
 * shape; the model and QML never touch the wire.
 */
class WindowRuleController : public PhosphorSettingsUi::PageController
{
    Q_OBJECT

    Q_PROPERTY(WindowRuleModel* model READ model CONSTANT)
    // `dirty` Q_PROPERTY + `dirtyChanged()` signal are inherited from
    // PhosphorSettingsUi::StagingDomain via PageController — do not redeclare.
    Q_PROPERTY(bool daemonReachable READ daemonReachable NOTIFY daemonReachableChanged)
    /// True when a daemon `rulesChanged` broadcast arrived while the page had
    /// unsaved staged edits — `reload()` skipped the refresh to avoid stomping
    /// them, so the staged set is now divergent from the daemon's. The page
    /// surfaces a "rules changed on disk — review before saving" notice. While
    /// this flag is set, `commit(false)` refuses the push (returns false) so it
    /// cannot silently overwrite the daemon's newer rules — the user must
    /// review/revert or call `commit(true)` to force the overwrite.
    Q_PROPERTY(bool daemonChangedWhileDirty READ daemonChangedWhileDirty NOTIFY daemonChangedWhileDirtyChanged)

public:
    explicit WindowRuleController(QObject* parent = nullptr);
    ~WindowRuleController() override;

    /// PhosphorSettingsUi::StagingDomain contract. apply() forwards to
    /// commit() (the staged set goes to the daemon); discard() forwards to
    /// revert() (the daemon's set is re-fetched into the model).
    bool isDirty() const override;
    void apply() override;
    void discard() override;

    /// Wire the screen-id / activity-uuid / layout-id → display-name lookups
    /// used by the model's `matchSummary` and by the controller's own
    /// `monitorOverview` summary. The lookups capture the SettingsController
    /// snapshot lists by reference, so they resolve against the live snapshot
    /// every call. Install-once: when the underlying snapshot lists update,
    /// the parent calls `WindowRuleModel::refreshLabels()` on the model to
    /// invalidate the cached label cells — re-installing the lookups would
    /// just churn the closures without changing behaviour.
    ///
    /// All FOUR lookups (screen + activity + snappingLayout +
    /// tilingAlgorithm) must be wired for the model to render rich
    /// `matchSummary` / `actionSummary` cells — a missing lookup falls
    /// back to printing the raw id/UUID. SettingsController is the
    /// single intended caller and installs all four during page
    /// registration. The legacy `setLayoutLookup(fn)` wires the same
    /// resolver into BOTH snappingLayout and tilingAlgorithm; new
    /// callers should prefer the typed pair below.
    void setScreenLookup(WindowRuleModel::LabelLookup fn);
    void setActivityLookup(WindowRuleModel::LabelLookup fn);
    void setLayoutLookup(WindowRuleModel::LabelLookup fn);
    /// layoutId UUID → display label resolver for SetSnappingLayout actions.
    void setSnappingLayoutLookup(WindowRuleModel::LabelLookup fn);
    /// Algorithm token ("bsp", …) → display label resolver for SetTilingAlgorithm actions.
    void setTilingAlgorithmLookup(WindowRuleModel::LabelLookup fn);

    WindowRuleModel* model()
    {
        return &m_model;
    }

    bool daemonReachable() const
    {
        return m_daemonReachable;
    }

    bool daemonChangedWhileDirty() const
    {
        return m_daemonChangedWhileDirty;
    }

    // ── Lifecycle / staging contract ──────────────────────────────────────

    /// (Re)fetch the rule set from the daemon and load it into the model.
    /// Clears the dirty bit. Safe to call when the daemon is down — the model
    /// is then left empty and `daemonReachable` is false.
    Q_INVOKABLE void reload();

    /// Push the staged rule set back to the daemon. Clears the dirty bit on a
    /// successful write. Called by `SettingsController::save()`. Returns false
    /// if the push failed (daemon down, or the daemon rejected/partially
    /// dropped rules) — the caller must keep the page dirty in that case.
    ///
    /// Refuses (returns false, leaves the page dirty) when
    /// `daemonChangedWhileDirty` is set and @p force is false: the daemon's
    /// rules changed under the staged edits, so an unconditional push would be
    /// a silent lost update. The page surfaces the refusal and the user must
    /// explicitly review (revert) or force the overwrite. Pass @p force true
    /// once the user has chosen "overwrite anyway".
    bool commit(bool force = false);

    /// QML-callable shorthand for `commit(true)`. Surfaces the "overwrite
    /// anyway" escape hatch the daemonChangedWhileDirty banner needs — the
    /// inherited StagingDomain `apply()` slot calls `commit(false)` and
    /// discards the bool return, so without this Q_INVOKABLE QML had no way
    /// to reach the force path and the user could get stuck in a permanent
    /// "page dirty / save refused" loop.
    Q_INVOKABLE bool forceCommit();

    /// Async sibling of `commit()` — pushes the staged rule set to the
    /// daemon via QDBusPendingCallWatcher and emits the inherited
    /// `applyResult(ok, error)` signal on the reply. UI threads no
    /// longer block waiting for the daemon (a stuck/firewalled daemon
    /// would freeze the whole Settings window with the sync path).
    ///
    /// The sync `commit()` above stays callable for back-compat:
    /// `SettingsController::save()` and the inherited `apply()` slot
    /// still use it. Once the chrome footer surfaces the async
    /// applyResult state-machine, the sync helper can become an
    /// internal implementation detail.
    Q_INVOKABLE void asyncCommit(bool force = false);

    /// Discard staged edits and re-fetch from the daemon. The fetch is async,
    /// so this returns immediately — the model is repopulated when the reply
    /// arrives (signalled by `rulesLoaded()`). The dirty +
    /// `daemonChangedWhileDirty` bits are cleared in the reply handler only
    /// if the re-fetch succeeded; if the daemon is unreachable the staged
    /// edits are left in place (there is nothing authoritative to reload
    /// to), and `daemonReachable` is cleared so the page surfaces the
    /// failure. Called by `SettingsController::load()` (Discard).
    ///
    /// @note While the revert fetch is in flight, any in-QML edits to rules
    /// are discarded when the daemon snapshot lands — the reply handler
    /// unconditionally replaces the model with the fetched set. Callers
    /// should avoid surfacing edit UI between `revert()` and
    /// `revertFinished()`; that window is short (a single D-Bus round-trip)
    /// and the "Discard" affordance that drives revert is itself a modal
    /// confirmation, so a user editing during the window is not a realistic
    /// path. Documenting the discard semantics rather than blocking edits
    /// keeps the controller stateless from QML's perspective.
    void revert();

    /// True iff there are unsaved staged edits. Mirror of `isDirty()` for the
    /// `SettingsController` pending-changes gate.
    bool hasPendingChanges() const
    {
        return m_dirty;
    }

    // ── Rule CRUD — keyed by UUID, never by index ─────────────────────────

    /// Build a fresh, never-yet-stored rule for the given guided subject and
    /// return it as a JSON map ready for the editor sheet. @p subject is one
    /// of "monitor" / "desktop" / "application" / "activity" / "animation" /
    /// "custom". The returned rule has a fresh UUID, a sensible starting
    /// match for the subject, and an empty action list. It is NOT added to
    /// the model — the editor sheet calls `addRuleFromJson` once the user
    /// confirms.
    Q_INVOKABLE QVariantMap newEmptyRule(const QString& subject) const;

    /// Catalogue of pre-fab rule templates surfaced as quick-starts in the
    /// AddRuleSheet. Each entry is `{ id, label, description, icon }`.
    /// Use `newRuleFromTemplate(id)` to materialise the rule.
    Q_INVOKABLE QVariantList ruleTemplates() const;

    /// Build a fully-seeded rule for @p templateId (one of the ids returned
    /// by `ruleTemplates()`). Returns an empty map for an unknown id. Like
    /// `newEmptyRule`, the rule is NOT added — the editor sheet commits it
    /// after the user fills in the remaining match values (e.g. picks the
    /// app for a Float template).
    Q_INVOKABLE QVariantMap newRuleFromTemplate(const QString& templateId) const;

    /// Add a rule from its JSON map (the editor sheet's output). Returns the
    /// rule's UUID string on success, or an empty string on failure.
    Q_INVOKABLE QString addRuleFromJson(const QVariantMap& ruleJson);

    /// Replace the rule with the matching id from its JSON map. Returns false
    /// on a malformed map or an unknown id. An in-place edit never reorders the
    /// list, so priorities are NOT renormalized — an explicit `priority` edit
    /// in the Advanced editor is honoured verbatim.
    Q_INVOKABLE bool updateRuleFromJson(const QVariantMap& ruleJson);

    /// Remove the rule with @p ruleId (a UUID string). Returns false if absent.
    Q_INVOKABLE bool removeRule(const QString& ruleId);

    /// Clone the rule with @p ruleId. The clone gets a fresh UUID, the same
    /// priority as the source (so its evaluation order is unchanged), and an
    /// auto-suffixed name ("X (copy)") when the source's name is non-empty so
    /// the two are distinguishable in the list. The clone is inserted just
    /// after the source so it appears next to it in the section.
    ///
    /// Returns the new rule's UUID string on success, or an empty string if
    /// the source id is unknown or the clone could not be added (id collision
    /// or invalid rule — neither should happen for a freshly-stamped clone).
    Q_INVOKABLE QString duplicateRule(const QString& ruleId);

    /// Toggle the enabled flag of the rule with @p ruleId.
    Q_INVOKABLE bool setRuleEnabled(const QString& ruleId, bool enabled);

    /// Reorder: move @p ruleId to sit just before @p beforeRuleId (empty =
    /// move to the end). Drives the Animations drag-to-reorder. After a move
    /// the affected rules' priorities are renormalized so list order and
    /// evaluation order agree.
    Q_INVOKABLE bool moveRule(const QString& ruleId, const QString& beforeRuleId);

    /// The rule with @p ruleId as a JSON map, or an empty map if absent.
    Q_INVOKABLE QVariantMap ruleJson(const QString& ruleId) const;

    // ── List / section metadata for the page ──────────────────────────────

    /// The ordered section descriptors for the grouped list and chip filter.
    /// Each entry: `{ value: int (Section enum), label }`. The order is the
    /// canonical display order; QML must not hardcode the enum values.
    Q_INVOKABLE QVariantList sections() const;

    /// A snapshot of every rule as a map keyed by the model's role names
    /// (`ruleId`, `name`, `enabled`, `section`, `matchSummary`,
    /// `actionSummary`, `conditionCount`, `actionCount`, `isComposite`,
    /// `screenIds`). Lets the page bucket / filter rules without ever
    /// referencing raw `Qt.UserRole + N` integers.
    Q_INVOKABLE QVariantList rulesSnapshot() const;

    // ── Monitor overview strip ────────────────────────────────────────────

    /// Read-only per-monitor summary for the overview strip. Each entry:
    /// `{ screenId, layoutName, tilingEnabled, ruleCount, assigned }`.
    /// @p screens is the `SettingsController::screens` list (each a map with
    /// at least a `name`/`id` field) so the overview can list every connected
    /// monitor — including ones with no rule at all (the "Not assigned" tile).
    Q_INVOKABLE QVariantList monitorOverview(const QVariantList& screens) const;

    /// The screen-ids a rule pins, or an empty list if it is not a
    /// monitor-scoped rule. Lets QML filter the list by the rule's actual
    /// ScreenId predicate(s) rather than substring-scanning the localized
    /// match summary.
    Q_INVOKABLE QStringList ruleScreenIds(const QString& ruleId) const;

    // ── Authoring metadata for the QML editors ────────────────────────────

    /// Match fields suitable for the leaf-editor field dropdown. Each entry:
    /// `{ value: int (Field enum), wire: QString (JSON wire string),
    ///    label, valueKind: "string"|"number"|"bool" }`. QML keys off `wire`
    /// so it never has to reconstruct the enum↔wire-string table.
    Q_INVOKABLE QVariantList matchFields() const;

    /// Operators valid for @p fieldValue (a `Field` enum int). Each entry:
    /// `{ value: int (Operator enum), wire: QString (JSON wire string), label }`.
    Q_INVOKABLE QVariantList operatorsForField(int fieldValue) const;

    /// Registered action types for the action-editor dropdown. Each entry:
    /// `{ value: QString (action type id), label, params: [ ... ],
    ///   domain: "context"|"window" }` where each param descriptor is
    /// `{ key, kind: "string"|"number"|"enum"|"percent", label }` plus, for
    /// `kind == "enum"`, an `options` string list, and for `kind == "number"`
    /// /`"percent"`, `min`/`max`/`scale` (the value stored is `display * scale`).
    /// QML drives the per-type editor entirely from this descriptor; the
    /// `domain` field lets the picker disable types incompatible with the
    /// current match expression (a context-domain action against a
    /// window-property match never fires).
    Q_INVOKABLE QVariantList actionTypes() const;

    /// A complete, default-seeded action payload for @p typeWire — a JSON map
    /// of the form `{ type: <typeWire>, ...defaults }` ready to drop into a
    /// rule's `actions` list. Each parameter declared by the type's descriptor
    /// gets a kind-appropriate starting value (first enum option, minimum
    /// number, empty string for picker kinds with no implicit default). Used
    /// by the QML action row when the user switches an existing row's type
    /// (so the new param set is pre-seeded and `canSave` doesn't immediately
    /// gate on missing values) and by the action-list editor when appending
    /// a freshly-added action. An unknown @p typeWire returns just
    /// `{ type: <typeWire> }`. Kept C++-side so the seeding rules live next
    /// to the descriptor that drives them (single source of truth).
    Q_INVOKABLE QVariantMap defaultPayloadFor(const QString& typeWire) const;

    /// Semantic validation issues for the rule represented by @p ruleJson —
    /// the editor sheet's working copy. Same check `WindowRuleSet::fromJson`
    /// runs at load time and `WindowRuleModel::ValidationIssueCountRole`
    /// exposes per-row; surfaced live so the editor can show an inline
    /// message and block save until the user resolves the mismatch.
    ///
    /// Each entry: `{ code: int (ValidationIssue::Code),
    ///   actionIndex: int, actionType: QString, message: QString }`.
    /// An empty list means no issues. Tolerates a partial working rule (no
    /// id / no actions yet) — only the @c match and @c actions sub-trees
    /// drive the check.
    Q_INVOKABLE QVariantList validationIssuesForJson(const QVariantMap& ruleJson) const;

    /// True iff the @p matchJson sub-tree references only context fields
    /// (ScreenId / VirtualDesktop / Activity) — i.e. it is compatible with
    /// every action's domain. The picker uses this to flag context-domain
    /// action types as incompatible when the current match has a
    /// window-property leaf. An empty / catch-all match counts as
    /// context-only.
    Q_INVOKABLE bool matchIsContextOnly(const QVariantMap& matchJson) const;

Q_SIGNALS:
    // dirtyChanged() inherited from StagingDomain.
    void daemonReachableChanged();
    void daemonChangedWhileDirtyChanged();
    /// Emitted on the async fetch reply path after the model has been
    /// repopulated with the daemon's authoritative rule set. Lets tests and
    /// QML observers know when the initial load (or a daemon-broadcast
    /// reload) has completed.
    void rulesLoaded();
    /// Emitted on the async reply path that resolves a user-driven `revert()`
    /// call (Discard). `success` is true iff the re-fetch landed and the
    /// staged edits were dropped; false on a D-Bus / daemon error, in which
    /// case the staged edits are intentionally preserved (the page stays
    /// dirty). `SettingsController::load()` listens once so it can re-add the
    /// "window-rules" entry to its dirty-pages set when revert failed during
    /// the `setNeedsSave(false)` blanket-clear it does for every other page.
    void revertFinished(bool success);
    /// Emitted exactly once when ALL three label resolvers
    /// (screen / activity / snapping-layout + tiling-algorithm) have
    /// been wired by the parent SettingsController. QML pages (notably
    /// WindowRulesPage and monitorOverview consumers) gate "show raw
    /// model" on this signal so the brief startup window where
    /// `monitorOverview` would return raw UUIDs / wire tokens never
    /// becomes visible to the user. Emitted at most once per controller
    /// instance — the resolvers are install-once, not refreshable.
    void lookupsReady();

private:
    void setDirty(bool dirty);
    void setDaemonReachable(bool reachable);
    void setDaemonChangedWhileDirty(bool changed);

    /// Subscribe / unsubscribe the rulesChanged → reload() D-Bus
    /// pipe. Routed through these private helpers (rather than
    /// inlining the QDBusConnection calls in the ctor/dtor) so the
    /// (service, path, interface, signal, slot) tuple is captured in
    /// exactly one place. Without this, a rename of the signal name
    /// must touch BOTH ends of the connect+disconnect pair or the
    /// dtor leaks a dangling slot binding referencing the destroyed
    /// controller.
    bool subscribeRulesChanged();
    void unsubscribeRulesChanged();

    /// Canonical (service, path, interface, signal) tuple for the
    /// rulesChanged D-Bus signal. Used by both subscribe / unsubscribe
    /// helpers so a rename of any field touches exactly one place — the
    /// dtor's disconnect must spell the same tuple as the ctor's connect
    /// or the bus keeps a dangling slot reference.
    struct RulesChangedSubscription
    {
        QString service;
        QString objectPath;
        QString interface;
        QString signalName;
    };
    static RulesChangedSubscription rulesChangedSubscriptionArgs();

    /// Re-fetch the rule set from the daemon and load it into the model. The
    /// fetch is asynchronous — the function returns immediately and the model
    /// is repopulated on the D-Bus reply. On success the dirty +
    /// `daemonChangedWhileDirty` bits are cleared in the reply handler and
    /// `rulesLoaded()` is emitted; on a D-Bus error `daemonReachable` is
    /// cleared and the staged model is left untouched. The public `reload()`
    /// slot guards on the dirty bit so a daemon broadcast can't stomp staged
    /// edits; `revert()` calls this directly to bypass that guard.
    /// @p fromRevert tags this fetch as originating from an explicit
    /// revert() (true) vs. ctor/reload() (false). The reply handler
    /// only emits revertFinished when fromRevert is true. Passing
    /// the bool explicitly instead of inferring from
    /// `m_pendingRevertFetches > 0` prevents a concurrent daemon
    /// broadcast (reload path) from spoofing a `revertFinished(true)`
    /// when a real revert is in flight on a sibling watcher.
    void fetchAndLoad(bool fromRevert = false);

    /// Push @p rules to the daemon via `setAllRules`. Returns true only if the
    /// daemon accepted every rule (no partial drop).
    bool pushToDaemon(const QList<PhosphorWindowRule::WindowRule>& rules);

    /// Async sibling of pushToDaemon — dispatches `setAllRules` via
    /// QDBusPendingCallWatcher and emits applyResult on the reply.
    /// Returns false ONLY for the up-front validation failure (a rule
    /// was rejected client-side) — the async leg covers transport
    /// errors via the applyResult signal.
    bool pushToDaemonAsync(const QList<PhosphorWindowRule::WindowRule>& rules);

    /// Renormalize every rule's priority so descending list order ⇒
    /// descending priority. Keeps the migrated-context bands roughly intact
    /// by scaling rather than flattening: animation/application rules are
    /// list-ordered, context rules keep their derived bands.
    void renormalizePriorities();

    WindowRuleModel m_model;
    bool m_dirty = false;
    bool m_daemonReachable = false;
    bool m_daemonChangedWhileDirty = false;
    /// Tracks whether the `rulesChanged` D-Bus subscription is currently
    /// active. Set true on a successful subscribeRulesChanged(), false on
    /// failure / disconnect. setDaemonReachable() consults this to retry
    /// the subscription when the daemon comes up after a failed initial
    /// connect (e.g. the daemon was down when the controller was built).
    bool m_rulesChangedSubscribed = false;
    /// In-flight guard for the StagingDomain discard() entry point.
    /// Set true on discard() dispatch, cleared in the one-shot
    /// revertFinished handler. A second discard() while the first
    /// revert is still in flight is rejected with a failed
    /// discardResult so the framework's wait-counter ticks down
    /// rather than the chrome stalling.
    bool m_discardInFlight = false;
    /// Symmetric guard for the StagingDomain apply() entry point.
    /// Cleared inside pushToDaemonAsync's reply lambda before the
    /// applyResult emit. Refuses re-entrant apply() while a setAllRules
    /// D-Bus call is still outstanding — without this, a second
    /// apply() dispatches a duplicate setAllRules push, and the reply
    /// lambdas race on setDirty(false) + applyResult emission.
    bool m_asyncCommitInFlight = false;
    /// Split lookups: monitorOverview's tile picks one based on the rule's
    /// engineMode, so a SetSnappingLayout with a UUID-shaped value can't
    /// accidentally hit the tiling-algorithm path and vice versa.
    WindowRuleModel::LabelLookup m_snappingLayoutLookup;
    WindowRuleModel::LabelLookup m_tilingAlgorithmLookup;
    /// Bit-mask of resolvers wired so far. When all four bits are set,
    /// emit lookupsReady() once. Tracks individual setters because the
    /// parent SettingsController wires them across separate calls in
    /// its constructor and the QML side wants a single "everything is
    /// ready" signal.
    enum LookupBit : unsigned {
        LookupScreen = 1u << 0,
        LookupActivity = 1u << 1,
        LookupSnappingLayout = 1u << 2,
        LookupTilingAlgorithm = 1u << 3,
        AllLookups = LookupScreen | LookupActivity | LookupSnappingLayout | LookupTilingAlgorithm,
    };
    unsigned m_wiredLookups = 0;
    bool m_lookupsReadyEmitted = false;
    void markLookupWired(LookupBit bit);
};

} // namespace PlasmaZones
