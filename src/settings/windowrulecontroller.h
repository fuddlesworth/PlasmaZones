// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <optional>

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
class WindowRuleController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(WindowRuleModel* model READ model CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
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

    /// Wire the screen-id / activity-uuid / layout-id → display-name lookups
    /// used by the model's `matchSummary` and by the controller's own
    /// `monitorOverview` summary. The lookups capture the SettingsController
    /// snapshot lists by reference; the parent re-invokes these setters when
    /// the underlying data changes so the resolved strings stay fresh.
    void setScreenLookup(WindowRuleModel::LabelLookup fn);
    void setActivityLookup(WindowRuleModel::LabelLookup fn);
    void setLayoutLookup(WindowRuleModel::LabelLookup fn);

    WindowRuleModel* model()
    {
        return &m_model;
    }

    bool isDirty() const
    {
        return m_dirty;
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

    /// Discard staged edits and re-fetch from the daemon. Clears the dirty bit
    /// only if the re-fetch actually succeeded. Called by
    /// `SettingsController::load()` (Discard). If the daemon is unreachable the
    /// staged edits cannot be safely discarded (there is nothing authoritative
    /// to reload), so the page stays dirty and `daemonReachable` stays false —
    /// the revert is reported as having failed rather than silently dropping
    /// the dirty bit while the stale edits survive. Returns true only if the
    /// staged set was actually replaced with the daemon's authoritative set.
    bool revert();

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
    void dirtyChanged();
    void daemonReachableChanged();
    void daemonChangedWhileDirtyChanged();

private:
    void setDirty(bool dirty);
    void setDaemonReachable(bool reachable);
    void setDaemonChangedWhileDirty(bool changed);

    /// Re-fetch the rule set from the daemon and load it into the model.
    /// Returns true and clears the dirty + `daemonChangedWhileDirty` bits only
    /// if the daemon was reachable and the model was actually replaced with the
    /// authoritative set. Returns false (model and dirty bit left untouched)
    /// when the daemon is unreachable — the caller must not pretend a reload
    /// happened. The public `reload()` slot guards on the dirty bit so a daemon
    /// broadcast can't stomp staged edits; `revert()` calls this directly to
    /// bypass that guard.
    bool fetchAndLoad();

    /// Fetch the rule set JSON from the daemon. Returns nullopt on any D-Bus
    /// failure (daemon down, timeout). Updates `daemonReachable`.
    std::optional<PhosphorWindowRule::WindowRuleSet> fetchFromDaemon();

    /// Push @p rules to the daemon via `setAllRules`. Returns true only if the
    /// daemon accepted every rule (no partial drop).
    bool pushToDaemon(const QList<PhosphorWindowRule::WindowRule>& rules);

    /// Renormalize every rule's priority so descending list order ⇒
    /// descending priority. Keeps the migrated-context bands roughly intact
    /// by scaling rather than flattening: animation/application rules are
    /// list-ordered, context rules keep their derived bands.
    void renormalizePriorities();

    WindowRuleModel m_model;
    bool m_dirty = false;
    bool m_daemonReachable = false;
    bool m_daemonChangedWhileDirty = false;
    /// Resolves a layoutId to the layout's display name. Used by
    /// `monitorOverview` to turn the raw UUID in `SetSnappingLayout` /
    /// `SetTilingAlgorithm` params into the friendly layout/algorithm name.
    WindowRuleModel::LabelLookup m_layoutLookup;
};

} // namespace PlasmaZones
