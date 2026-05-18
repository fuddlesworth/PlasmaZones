// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <PhosphorWindowRule/WindowRule.h>

namespace PlasmaZones {

/**
 * @brief A single flat list model over the unified WindowRule set.
 *
 * This is the *only* model behind the Window Rules page. There are no
 * per-section proxy models — `WindowRulesPage.qml` derives section buckets,
 * search filtering, and chip filtering in QML over this one model.
 *
 * The model is staging: it holds an in-memory `QList<WindowRule>` that the
 * `WindowRuleController` populates from the daemon's `org.plasmazones.WindowRules`
 * D-Bus surface, mutates locally as the user edits, and flushes back to the
 * daemon on commit. The model never talks to D-Bus itself.
 *
 * ## SectionRole — derived in C++
 *
 * Every rule is bucketed into exactly one section, computed from its match
 * expression + actions:
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
class WindowRuleModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    /// Canonical section ids — also the chip-filter values in QML.
    enum class Section {
        Monitor, ///< context-only layout/engine/disable rule
        Application, ///< window-property float/exclude/opacity rule
        Activity, ///< context-only rule pinned to an activity
        Animation, ///< carries an OverrideAnimation* action
        Advanced, ///< composite / mixed — graduated out of every section
    };
    Q_ENUM(Section)

    enum Roles {
        IdRole = Qt::UserRole + 1, ///< QUuid string (with braces)
        NameRole, ///< human-readable rule name
        EnabledRole, ///< bool
        PriorityRole, ///< int
        SectionRole, ///< Section enum value (derived)
        SectionLabelRole, ///< localized section header label
        MatchSummaryRole, ///< one-line human match summary
        ActionSummaryRole, ///< one-line human action summary
        ConditionCountRole, ///< number of leaf predicates in the match tree
        ActionCountRole, ///< number of actions
        IsCompositeRole, ///< true if the match is a non-trivial composite
        MatchJsonRole, ///< the match expression as a JSON object
        ActionsJsonRole, ///< the actions as a JSON array
        RuleJsonRole, ///< the whole rule as a JSON object
    };

    explicit WindowRuleModel(QObject* parent = nullptr);

    // ── QAbstractListModel ──
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── Bulk population (from the controller's D-Bus fetch) ──

    /// Replace the entire backing list. Resets the model.
    void setRules(const QList<PhosphorWindowRule::WindowRule>& rules);

    /// The current backing list — used by the controller to build a D-Bus
    /// payload on commit.
    const QList<PhosphorWindowRule::WindowRule>& rules() const
    {
        return m_rules;
    }

    // ── Per-rule lookup / mutation by UUID (never by index) ──

    /// The rule with @p id, or a default-constructed (null-id) rule if absent.
    PhosphorWindowRule::WindowRule ruleById(const QUuid& id) const;

    /// True if a rule with @p id is present.
    bool contains(const QUuid& id) const;

    /// Append @p rule. Returns false if its id collides or it is invalid.
    bool addRule(const PhosphorWindowRule::WindowRule& rule);

    /// Replace the rule with the same id. Returns false if no such rule.
    bool updateRule(const PhosphorWindowRule::WindowRule& rule);

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
    static Section sectionFor(const PhosphorWindowRule::WindowRule& rule);

    /// Localized header label for @p section.
    static QString sectionLabel(Section section);

    /// Localized human label for a match Field. Shared with
    /// `WindowRuleController` so the 14-case table lives in exactly one place.
    static QString fieldLabel(PhosphorWindowRule::Field field);

Q_SIGNALS:
    void countChanged();
    /// Emitted when an `updateRule()` moved a rule into a different section
    /// (its `sectionFor()` changed). A plain `dataChanged` does not prompt the
    /// QML section view to re-bucket the rule, so the page listens for this.
    void ruleSectionChanged();

private:
    /// Index of the rule with @p id, or -1.
    int indexOf(const QUuid& id) const;

    /// One-line human summary of a match expression.
    static QString matchSummary(const PhosphorWindowRule::MatchExpression& match);
    /// One-line human summary of an action list.
    static QString actionSummary(const QList<PhosphorWindowRule::RuleAction>& actions);
    /// Total leaf-predicate count in a match tree.
    static int conditionCount(const PhosphorWindowRule::MatchExpression& match);

    QList<PhosphorWindowRule::WindowRule> m_rules;
};

} // namespace PlasmaZones
