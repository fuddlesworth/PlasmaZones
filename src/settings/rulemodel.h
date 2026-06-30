// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <PhosphorRules/Rule.h>

#include <functional>

namespace PlasmaZones {

/**
 * @brief A single flat list model over the unified Rule set.
 *
 * This is the *only* model behind the Rules page. There are no
 * per-section proxy models — `RulesPage.qml` derives its flat,
 * priority-ordered view, search filtering, and the section / source / status
 * filters in QML over this one model.
 *
 * The model is staging: it holds an in-memory `QList<Rule>` that the
 * `RuleController` populates from the daemon's `org.plasmazones.Rules`
 * D-Bus surface, mutates locally as the user edits, and flushes back to the
 * daemon on commit. The model never talks to D-Bus itself.
 *
 * ## SectionRole — derived in C++
 *
 * Every rule is classified into exactly one section, computed from its match
 * expression + actions (the section drives the per-row badge and the filter):
 *   - `monitor`     — context-only rule (ScreenId / VirtualDesktop / Activity)
 *                     whose actions are layout / engine / disable. The
 *                     "Monitor & Layout" group.
 *   - `application` — a window-property rule (AppId / WindowClass / …) whose
 *                     actions are float / exclude / opacity / engine.
 *   - `activity`    — reserved alias for context-only rules pinned to an
 *                     Activity only (still surfaces under `monitor` visually,
 *                     but the chip filter distinguishes them).
 *   - `animation`   — any rule carrying an OverrideAnimation* action.
 *   - `advanced`    — anything that "graduated": a composite match a
 *                     specialized section cannot represent, or a mixed action
 *                     set. The Advanced / Custom group.
 *
 * A rule "graduates" to `advanced` the moment its shape exceeds what a
 * specialized section can edit without data loss — the specialized views then
 * never see it, so they cannot corrupt it.
 */
class RuleModel : public QAbstractListModel
{
    Q_OBJECT
    // Expose the type to QML as `RuleModel` so QML can reference the
    // `Section` and `Roles` enum members by name (e.g.
    // `RuleModel.Animation`, `RuleModel.SectionRole`). Uncreatable
    // because the page receives the live instance via
    // `RuleController::model` — instantiating one from QML would yield
    // an empty disconnected model. `qt_add_qml_module` picks up the
    // QML_NAMED_ELEMENT macro and emits the registration entry into
    // `plasmazones-settings.qmltypes`.
    QML_NAMED_ELEMENT(RuleModel)
    QML_UNCREATABLE(
        "RuleModel is owned by RuleController; access "
        "the live instance via SettingsController.rulesPage.model.")

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    /// Canonical section ids — also the chip-filter values in QML.
    enum class Section {
        Monitor, ///< context-only layout/engine/disable rule
        Application, ///< window-property float/exclude/opacity rule
        Activity, ///< context-only rule pinned to an activity
        Animation, ///< carries an OverrideAnimation* action
        Advanced, ///< composite / mixed — graduated out of every section
        System, ///< app-managed baseline rule (the seeded defaults)
    };
    Q_ENUM(Section)

    enum Roles {
        IdRole = Qt::UserRole + 1, ///< QUuid string (with braces)
        NameRole, ///< human-readable rule name
        EnabledRole, ///< bool
        PriorityRole, ///< int — also the role of setPriorities()'s dataChanged
        SectionRole, ///< Section enum value (derived)
        MatchSummaryRole, ///< one-line human match summary
        ActionSummaryRole, ///< one-line human action summary
        ConditionCountRole, ///< number of leaf predicates in the match tree
        ActionCountRole, ///< number of actions
        IsCompositeRole, ///< true if the match is a non-trivial composite
        ScreenIdsRole, ///< QStringList of ScreenId leaf values in the match
        ValidationIssueCountRole, ///< int — number of semantic validation
                                  ///< issues the rule carries (a context-domain
                                  ///< action paired with a window-property match,
                                  ///< etc). The row delegate shows a warning
                                  ///< badge when this is non-zero.
        ManagedRole, ///< bool — true for built-in rules the app owns (the
                     ///< baseline appearance rule). The row delegate hides the
                     ///< delete and drag-reorder affordances when set.
    };
    Q_ENUM(Roles)

    explicit RuleModel(QObject* parent = nullptr);

    // ── QAbstractListModel ──
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── Bulk population (from the controller's D-Bus fetch) ──

    /// Replace the entire backing list. Resets the model.
    void setRules(const QList<PhosphorRules::Rule>& rules);

    /// The current backing list — used by the controller to build a D-Bus
    /// payload on commit.
    const QList<PhosphorRules::Rule>& rules() const
    {
        return m_rules;
    }

    // ── Per-rule lookup / mutation by UUID (never by index) ──

    /// The rule with @p id, or a default-constructed (null-id) rule if absent.
    PhosphorRules::Rule ruleById(const QUuid& id) const;

    /// True if a rule with @p id is present.
    bool contains(const QUuid& id) const;

    /// Append @p rule. Returns false if its id collides or it is invalid.
    bool addRule(const PhosphorRules::Rule& rule);

    /// Insert @p rule at @p insertIndex with a SINGLE beginInsertRows/
    /// endInsertRows pair. Replaces the prior duplicateRule() shape
    /// of addRule + N moveRule calls, which fired up to four model
    /// signals (rowsInserted + rowsMoved + rowsMoved + dataChanged)
    /// per user "Duplicate" click. @p insertIndex is clamped to
    /// [0, rowCount()] so callers don't have to range-check. Returns
    /// false if the rule's id collides or it is invalid.
    bool addRuleAt(const PhosphorRules::Rule& rule, int insertIndex);

    /// Outcome of an `updateRule()` call — lets the caller tell a genuine
    /// no-op (identical rule) apart from an applied change so a no-op save
    /// does not churn the dirty bit.
    enum class UpdateResult {
        NotFound, ///< no rule with that id, or @p rule is invalid
        Unchanged, ///< the rule was already identical — nothing applied
        Applied, ///< the rule was replaced and a change signal emitted
        AppliedSectionChanged, ///< replaced AND the edit moved it to a different section
    };

    /// Replace the rule with the same id. Returns `NotFound` for an unknown id
    /// or an invalid rule, `Unchanged` for an identical rule (no signal),
    /// `Applied` when the rule was replaced in place, and
    /// `AppliedSectionChanged` when the replacement also moved the rule into a
    /// different section (its global list-order priority must be re-stamped by
    /// the caller).
    UpdateResult updateRule(const PhosphorRules::Rule& rule);

    /// Remove the rule with @p id. Returns false if no such rule.
    bool removeRule(const QUuid& id);

    /// Reorder: move the rule with @p id to sit just before @p beforeId
    /// (or to the end if @p beforeId is null). Used by the Animations
    /// drag-to-reorder. Returns false on an unknown id.
    bool moveRule(const QUuid& id, const QUuid& beforeId);

    /// Re-stamp every rule's priority from @p priorities (parallel to the
    /// current list order). Mutates in place and emits a single
    /// `dataChanged(PriorityRole)` over the whole list — no model reset, so
    /// QML delegates are not torn down and rebuilt. @p priorities must have
    /// `rowCount()` entries; a size mismatch is a no-op.
    void setPriorities(const QList<int>& priorities);

    // ── Section helpers (also used by the model's own data()) ──

    /// The section a rule falls into — pure function of the rule's shape.
    static Section sectionFor(const PhosphorRules::Rule& rule);

    /// Localized header label for @p section.
    static QString sectionLabel(Section section);

    /// Localized human label for a match Field. Shared with
    /// `RuleController` so the per-Field label table lives in exactly one place.
    static QString fieldLabel(PhosphorRules::Field field);

    /// Every non-empty `ScreenId` leaf value found anywhere in @p match.
    /// Shared with `RuleController` so the recursive walk lives in one
    /// place — the model exposes it as `ScreenIdsRole`.
    static QStringList screenIdsOf(const PhosphorRules::MatchExpression& match);

    /// Display label for an action type id that no built-in label covers —
    /// the unknown / legacy / future-schema fallback. Shared by the model's
    /// `actionSummary` and the controller's `actionTypes()` so the fallback
    /// lives in exactly one place. Returns the raw type id verbatim.
    static QString actionTypeFallbackLabel(const QString& type);

    /// Resolves an opaque identifier (ScreenId, Activity UUID, layoutId) to a
    /// user-friendly display label. Returns the input unchanged when no
    /// mapping is known.
    using LabelLookup = std::function<QString(const QString&)>;

    /// Inject the screen-id → display-label resolver used when rendering
    /// `ScreenId` leaf predicates. The settings layer wires this from
    /// `SettingsController::screens()` so the model never reaches into the UI.
    /// Install-once setter; lookups are read live every time `data()` is
    /// invoked. To refresh visible labels after a lookup-source change,
    /// call @ref refreshLabels.
    void setScreenLabelLookup(LabelLookup fn);

    /// Inject the activity-uuid → activity-name resolver used when rendering
    /// `Activity` leaf predicates (and when stripping auto-stamped rule names).
    /// Install-once setter; lookups are read live every time `data()` is
    /// invoked. To refresh visible labels after a lookup-source change,
    /// call @ref refreshLabels.
    void setActivityLabelLookup(LabelLookup fn);

    /// Inject the zone-uuid → zone-name resolver used when rendering `Zone` leaf
    /// predicates, so a snap-zone rule renders "Right column" rather than a raw
    /// `{uuid}`. Install-once setter; lookups are read live every time `data()` is
    /// invoked. To refresh visible labels after a lookup-source change, call
    /// @ref refreshLabels.
    void setZoneLabelLookup(LabelLookup fn);

    /// Inject the layoutId / algorithm-token → display-name resolvers used by
    /// the action summary so `SetSnappingLayout` and `SetTilingAlgorithm`
    /// render "Binary Split" rather than the wire token / UUID. Split into a
    /// typed pair so a UUID-shaped layoutId and an algorithm token can't
    /// cross-resolve. Install-once setters; lookups are read live every time
    /// `data()` is invoked. To refresh visible labels after a lookup-source
    /// change, call @ref refreshLabels.
    /// Resolver for `SetSnappingLayout` action params (layoutId UUIDs).
    void setSnappingLayoutLabelLookup(LabelLookup fn);
    /// Resolver for `SetTilingAlgorithm` action params (algorithm tokens like "bsp").
    void setTilingAlgorithmLabelLookup(LabelLookup fn);
    /// Resolver for `OverrideAnimationShader` action params (effect ids like
    /// "dissolve") so the summary renders "Dissolve" rather than the raw id.
    void setShaderEffectLabelLookup(LabelLookup fn);
    /// Resolver for `OverrideOverlayShader` action params (overlay shader ids)
    /// so the summary renders the friendly name rather than the raw id. Sourced
    /// from the overlay/snapping shader registry, NOT the animation one.
    void setOverlayShaderLabelLookup(LabelLookup fn);
    /// Resolver for `OverrideAnimationCurve` action params (curve wire strings
    /// like "0.33,1.00,0.68,1.00" / "spring:12,1") so the summary renders the
    /// friendly preset name ("Standard (Cubic)", "Spring (12, 1)", "Custom")
    /// the rule editor shows. The canonical naming lives in QML CurvePresets,
    /// so the settings layer wires this from a QML-supplied resolver.
    void setCurveLabelLookup(LabelLookup fn);

    /// Re-emit dataChanged for every row across every label-derived role,
    /// so the view rebinds resolved screen / activity / layout names. The
    /// settings layer calls this from `screensChanged` / `activitiesChanged`
    /// / `layoutsChanged` after the underlying snapshot lists update.
    void refreshLabels();

    /// Display name for a rule — strips an auto-stamped context-rule name so
    /// the matchSummary fallback can render with friendly screen/activity
    /// labels. Public because `RuleController::ruleJson` uses it to
    /// blank the Name field in the editor when the stored value is an
    /// auto-stamp rather than something the user typed.
    QString displayName(const PhosphorRules::Rule& rule) const;

Q_SIGNALS:
    void countChanged();
    /// Emitted when an `updateRule()` moved a rule into a different section
    /// (its `sectionFor()` changed). A plain `dataChanged` does not prompt the
    /// QML view to re-derive the rule's section (its badge / filter
    /// classification), so the page listens for this.
    void ruleSectionChanged();

private:
    /// Index of the rule with @p id, or -1.
    int indexOf(const QUuid& id) const;

    /// One-line human summary of a match expression — instance method because
    /// it consults the per-instance screen/activity lookups for ScreenId /
    /// Activity leaves.
    QString matchSummary(const PhosphorRules::MatchExpression& match) const;
    /// One-line human summary of an action list — instance method because it
    /// consults the per-instance layout lookup for `SetSnappingLayout` /
    /// `SetTilingAlgorithm` actions.
    QString actionSummary(const QList<PhosphorRules::RuleAction>& actions) const;
    /// Total leaf-predicate count in a match tree.
    static int conditionCount(const PhosphorRules::MatchExpression& match);
    QList<PhosphorRules::Rule> m_rules;
    LabelLookup m_screenLookup;
    LabelLookup m_activityLookup;
    LabelLookup m_zoneLookup;
    // Split so a SetSnappingLayout action whose layoutId happens to
    // tokenise (e.g. matches an algorithm token by coincidence) and a
    // SetTilingAlgorithm action with a UUID-shaped algorithm name
    // can't cross-resolve through the wrong lookup.
    LabelLookup m_snappingLayoutLookup;
    LabelLookup m_tilingAlgorithmLookup;
    LabelLookup m_shaderEffectLookup;
    LabelLookup m_overlayShaderLookup;
    LabelLookup m_curveLookup;
};

} // namespace PlasmaZones
