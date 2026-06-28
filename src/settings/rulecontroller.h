// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <PhosphorRules/Rule.h>

#include "rulemodel.h"

namespace PlasmaZones {

/**
 * @brief Q_PROPERTY surface for the unified "Rules" settings page.
 *
 * Replaces `AnimationsPageController`'s app-rule block, `SnappingBridge`, and
 * `TilingBridge` with a single controller. It owns one `RuleModel`
 * (`QAbstractListModel`) and a staging copy of the rule set:
 *
 *   1. On construction (and on a daemon `rulesChanged` D-Bus signal while the
 *      page is clean) it fetches the rule set from
 *      `org.plasmazones.Rules` and loads it into the model.
 *   2. Every QML CRUD call mutates the in-memory model and flips the dirty bit.
 *   3. On `apply()` the staged rule set is pushed back to the daemon
 *      asynchronously (`asyncCommit` → `setAllRules`) — the daemon stays the
 *      sole writer of `rules.json`.
 *   4. On `revert()` the staged set is re-fetched from the daemon and the
 *      staged edits discarded — but only if the daemon is reachable; a failed
 *      re-fetch leaves the page dirty rather than silently dropping the edits.
 *
 * Dirty-tracking + revert/commit mirror `AnimationsPageController`'s
 * pending-changes contract: `SettingsController` connects `dirtyChanged` to
 * `setNeedsSave`, drives `apply()` (which dispatches `asyncCommit`) from
 * `save()`, and `revert()` from `load()`.
 *
 * The controller is the only thing on the settings side that knows the D-Bus
 * shape; the model and QML never touch the wire.
 */
class RuleController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(RuleModel* model READ model CONSTANT)
    // `dirty` Q_PROPERTY + `dirtyChanged()` signal are inherited from
    // PhosphorControl::StagingDomain via PageController — do not redeclare.
    Q_PROPERTY(bool daemonReachable READ daemonReachable NOTIFY daemonReachableChanged)
    /// True when a daemon `rulesChanged` broadcast arrived while the page had
    /// unsaved staged edits — `reload()` skipped the refresh to avoid stomping
    /// them, so the staged set is now divergent from the daemon's. The page
    /// surfaces a "rules changed on disk — review before saving" notice. While
    /// this flag is set, `asyncCommit(false)` refuses the push so it cannot
    /// silently overwrite the daemon's newer rules — the user must review/revert
    /// or call `asyncCommit(true)` to force the overwrite.
    Q_PROPERTY(bool daemonChangedWhileDirty READ daemonChangedWhileDirty NOTIFY daemonChangedWhileDirtyChanged)

public:
    explicit RuleController(QObject* parent = nullptr);
    ~RuleController() override;

    /// PhosphorControl::StagingDomain contract. apply() dispatches
    /// asyncCommit() (the staged set goes to the daemon); discard() forwards to
    /// revert() (the daemon's set is re-fetched into the model).
    bool isDirty() const override;
    void apply() override;
    void discard() override;

    /// Wire the screen-id / activity-uuid / layout-id → display-name lookups
    /// used by the model's `matchSummary` and by the controller's own
    /// `monitorOverview` summary. The lookups capture the SettingsController
    /// snapshot lists by reference, so they resolve against the live snapshot
    /// every call. Install-once: when the underlying snapshot lists update,
    /// the parent calls `RuleModel::refreshLabels()` on the model to
    /// invalidate the cached label cells — re-installing the lookups would
    /// just churn the closures without changing behaviour.
    ///
    /// All lookups (screen, activity, zone, snappingLayout, tilingAlgorithm,
    /// shaderEffect, overlayShader) must be wired for the model to render rich
    /// `matchSummary` / `actionSummary` cells — a missing lookup falls
    /// back to printing the raw id/UUID. SettingsController is the
    /// single intended caller and installs all of them during page
    /// registration via the typed snappingLayout/tilingAlgorithm pair.
    void setScreenLookup(RuleModel::LabelLookup fn);
    void setActivityLookup(RuleModel::LabelLookup fn);
    /// zone UUID → zone-name resolver for `Zone` match-leaf labels.
    void setZoneLookup(RuleModel::LabelLookup fn);
    /// layoutId UUID → display label resolver for SetSnappingLayout actions.
    void setSnappingLayoutLookup(RuleModel::LabelLookup fn);
    /// Algorithm token ("bsp", …) → display label resolver for SetTilingAlgorithm actions.
    void setTilingAlgorithmLookup(RuleModel::LabelLookup fn);
    /// Effect id ("dissolve", …) → display name resolver for
    /// OverrideAnimationShader actions. SettingsController wires this from the
    /// animation shader registry (the same source the rule editor's shader
    /// picker uses), so the list renders "Dissolve" rather than the raw id.
    void setShaderEffectLookup(RuleModel::LabelLookup fn);
    /// Overlay shader id → display name resolver for OverrideOverlayShader
    /// actions. SettingsController wires this from the overlay/snapping shader
    /// registry (the source the rule editor's overlay-shader picker uses), so
    /// the list renders the friendly name rather than the raw id.
    void setOverlayShaderLookup(RuleModel::LabelLookup fn);
    /// Curve wire-string → display name resolver for OverrideAnimationCurve
    /// actions. Q_INVOKABLE and QJSValue-typed because the canonical curve
    /// naming (easing-preset matching + spring formatting + i18n labels) lives
    /// in the QML CurvePresets singleton; the rules page passes that JS
    /// resolver here and this wraps it into a model LabelLookup, so the list
    /// reuses the editor's naming with no easing tables duplicated in C++.
    /// Passing a non-callable value clears the resolver (raw value shown).
    Q_INVOKABLE void setCurveLabelResolver(const QJSValue& resolver);

    RuleModel* model()
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

    /// Push the staged rule set to the daemon via QDBusPendingCallWatcher and
    /// emit the inherited `applyResult(ok, error)` signal on the reply. UI
    /// threads never block waiting for the daemon (a stuck/firewalled daemon
    /// would otherwise freeze the whole Settings window).
    ///
    /// This is the live save path: `apply()` (the StagingDomain slot
    /// `SettingsController::save()` drives) dispatches through
    /// `asyncCommit(false)`, and the daemon-changed banner forces an
    /// overwrite via `asyncCommit(true)`. Refuses (emits
    /// `applyResult(false, …)`, leaves the page dirty) when
    /// `daemonChangedWhileDirty` is set and @p force is false, so an
    /// unconditional push can't silently clobber the daemon's newer rules.
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
    ///
    /// Q_INVOKABLE so the "Discard and reload" affordance on the
    /// daemonChangedWhileDirty banner can drive it directly; the
    /// revertFinished re-marking in SettingsController is wired off the
    /// signal, so it fires regardless of whether QML or load() calls this.
    Q_INVOKABLE void revert();

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
    /// (`ruleId`, `name`, `enabled`, `priority`, `section`, `matchSummary`,
    /// `actionSummary`, `conditionCount`, `actionCount`, `isComposite`,
    /// `screenIds`, `validationIssueCount`, `managed`). Lets the page bucket / filter
    /// rules without ever referencing raw `Qt.UserRole + N` integers.
    Q_INVOKABLE QVariantList rulesSnapshot() const;

    // ── Monitor overview strip ────────────────────────────────────────────

    /// Read-only per-monitor summary for the overview strip. Each entry:
    /// `{ screenId, layoutName, tilingEnabled, ruleCount, assigned, locked }`
    /// (`locked` is true when a LockContext rule pins the monitor's layout).
    /// @p screens is the `SettingsController::screens` list (each a map with a
    /// `name` field, and a `screenId` fallback) so the overview can list every
    /// connected monitor — including ones with no rule at all (the "Not
    /// assigned" tile).
    Q_INVOKABLE QVariantList monitorOverview(const QVariantList& screens) const;

    // ── Authoring metadata for the QML editors ────────────────────────────

    /// Match fields suitable for the leaf-editor field dropdown. Each entry:
    /// `{ value: int (Field enum), wire: QString (JSON wire string), label,
    ///    valueKind: "string"|"number"|"bool"|"windowType"|"screen"|"activity" }`
    /// (the latter three drive dedicated pickers; "windowType" also carries an
    /// `options` list). QML keys off `wire` so it never has to reconstruct the
    /// enum↔wire-string table.
    Q_INVOKABLE QVariantList matchFields() const;

    /// Operators valid for @p fieldValue (a `Field` enum int). Each entry:
    /// `{ value: int (Operator enum), wire: QString (JSON wire string), label }`.
    Q_INVOKABLE QVariantList operatorsForField(int fieldValue) const;

    /// Every operator with its translated label, independent of any field —
    /// the leaf editor uses this to size the operator dropdown to the widest
    /// possible label so the operator column lines up across condition rows.
    /// Same entry shape as operatorsForField.
    Q_INVOKABLE QVariantList allOperators() const;

    /// Optional input hint for a match condition's value editor, keyed on the
    /// operator wire token @p op (the leaf's `node.op`) — empty when the operator
    /// needs none. Shown beneath the value field for operators whose syntax or
    /// matching semantics aren't obvious from a plain text box (regex, app-id
    /// match). The match-side counterpart to the per-param action hints.
    Q_INVOKABLE QString matchValueHint(const QString& op) const;

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
    /// the editor sheet's working copy. Same check `RuleSet::fromJson`
    /// runs at load time and `RuleModel::ValidationIssueCountRole`
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
    /// "rules" entry to its dirty-pages set when revert failed during
    /// the `setNeedsSave(false)` blanket-clear it does for every other page.
    void revertFinished(bool success);

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
    /// only emits revertFinished when fromRevert is true. Each
    /// watcher carries the bool through its lambda capture, so a
    /// concurrent daemon broadcast (reload path) can't spoof a
    /// `revertFinished(true)` while a real revert is in flight on a
    /// sibling watcher.
    void fetchAndLoad(bool fromRevert = false);

    /// Dispatch `setAllRules` to the daemon via QDBusPendingCallWatcher and
    /// emit applyResult on the reply. Returns false ONLY for the up-front
    /// validation failure (a rule was rejected client-side) — the async leg
    /// covers transport errors via the applyResult signal.
    bool pushToDaemonAsync(const QList<PhosphorRules::Rule>& rules);

    /// Renormalize every rule's priority so descending list order ⇒
    /// descending priority. Keeps the migrated-context bands roughly intact
    /// by scaling rather than flattening: animation/application rules are
    /// list-ordered, context rules keep their derived bands.
    void renormalizePriorities();

    RuleModel m_model;
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
    /// Split lookups: monitorOverview's tile picks one by the assignment
    /// winner's engine mode (snapping layout vs tiling algorithm), so a
    /// SetSnappingLayout with a UUID-shaped value can't accidentally hit the
    /// tiling-algorithm path and vice versa.
    RuleModel::LabelLookup m_snappingLayoutLookup;
    RuleModel::LabelLookup m_tilingAlgorithmLookup;
    /// JS resolver supplied by the QML rules page (CurvePresets.curveLabel).
    /// Held so the model LabelLookup installed in setCurveLabelResolver can
    /// re-invoke it live on every summary rebuild.
    QJSValue m_curveResolver;
};

} // namespace PlasmaZones
