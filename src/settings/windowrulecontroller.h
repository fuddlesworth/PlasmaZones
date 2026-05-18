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
 *   4. On `revert()` the staged set is discarded and re-fetched from the daemon.
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

public:
    explicit WindowRuleController(QObject* parent = nullptr);
    ~WindowRuleController() override;

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

    // ── Lifecycle / staging contract ──────────────────────────────────────

    /// (Re)fetch the rule set from the daemon and load it into the model.
    /// Clears the dirty bit. Safe to call when the daemon is down — the model
    /// is then left empty and `daemonReachable` is false.
    Q_INVOKABLE void reload();

    /// Push the staged rule set back to the daemon. Clears the dirty bit on a
    /// successful write. Called by `SettingsController::save()`. Returns false
    /// if the push failed (daemon down, or the daemon rejected/partially
    /// dropped rules) — the caller must keep the page dirty in that case.
    bool commit();

    /// Discard staged edits and re-fetch from the daemon. Clears the dirty
    /// bit. Called by `SettingsController::load()` (Discard).
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
    /// of "monitor" / "application" / "activity" / "custom". The returned rule
    /// has a fresh UUID, a sensible starting match for the subject, and an
    /// empty action list. It is NOT added to the model — the editor sheet
    /// calls `addRuleFromJson` once the user confirms.
    Q_INVOKABLE QVariantMap newEmptyRule(const QString& subject) const;

    /// Add a rule from its JSON map (the editor sheet's output). Returns the
    /// rule's UUID string on success, or an empty string on failure.
    Q_INVOKABLE QString addRuleFromJson(const QVariantMap& ruleJson);

    /// Replace the rule with the matching id from its JSON map. Returns false
    /// on a malformed map or an unknown id.
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
    /// `{ value: QString (action type id), label, params: [ ... ] }` where
    /// each param descriptor is
    /// `{ key, kind: "string"|"number"|"enum"|"percent", label }` plus, for
    /// `kind == "enum"`, an `options` string list, and for `kind == "number"`
    /// /`"percent"`, `min`/`max`/`scale` (the value stored is `display * scale`).
    /// QML drives the per-type editor entirely from this descriptor.
    Q_INVOKABLE QVariantList actionTypes() const;

Q_SIGNALS:
    void dirtyChanged();
    void daemonReachableChanged();
    /// Emitted when a `commit()` push to the daemon fails or partially drops
    /// rules — `SettingsController` keeps the window-rules page dirty.
    void commitFailed();

private:
    /// Flip the dirty bit to true and emit `dirtyChanged()`.
    void markDirty();
    void setDirty(bool dirty);
    void setDaemonReachable(bool reachable);

    /// Re-fetch the rule set from the daemon and load it into the model,
    /// unconditionally clearing the dirty bit. The public `reload()` slot
    /// guards on the dirty bit so a daemon broadcast can't stomp staged
    /// edits; `revert()`/`commit()` call this directly to bypass that guard.
    void fetchAndLoad();

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
};

} // namespace PlasmaZones
