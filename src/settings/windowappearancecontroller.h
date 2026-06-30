// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorControl/PageController.h>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

/// Q_PROPERTY surface for the slim "Window Appearance" settings page.
///
/// The page is a friendly editor for the three managed baseline appearance
/// Rules (borders, title bars, gaps — the catch-all, lowest-priority
/// rules the daemon seeds, one per concern). It reads and writes those rules'
/// actions through `settingsController.rulesPage` (RuleController),
/// so this controller only carries the CONSTANT slider bounds and the three
/// baseline rule ids. It holds no editable state of its own, so it is never
/// dirty and its apply/discard are no-ops; the dirty/save path runs through the
/// Rules page controller.
class WindowAppearanceController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int borderWidthMin READ borderWidthMin CONSTANT)
    Q_PROPERTY(int borderWidthMax READ borderWidthMax CONSTANT)
    Q_PROPERTY(int borderRadiusMin READ borderRadiusMin CONSTANT)
    Q_PROPERTY(int borderRadiusMax READ borderRadiusMax CONSTANT)
    // Seed defaults for the dependent border details. The baseline border rule
    // carries only the "show border" parent action; the Appearance page seeds
    // these width/radius defaults into the rule when the user first turns the
    // border on. Sourced from the same DecorationDefaults the daemon's seeder
    // used so the on-enable value matches the historical baseline default.
    Q_PROPERTY(int borderWidthDefault READ borderWidthDefault CONSTANT)
    Q_PROPERTY(int borderRadiusDefault READ borderRadiusDefault CONSTANT)
    // Gap slider bounds — the unified inner/outer gap controls moved onto this
    // page (they edit the same baseline rule). Sourced from the ConfigDefaults
    // accessors the gap action validators clamp against so the UI range and the
    // clamp range can never drift apart.
    Q_PROPERTY(int innerGapMin READ innerGapMin CONSTANT)
    Q_PROPERTY(int innerGapMax READ innerGapMax CONSTANT)
    Q_PROPERTY(int outerGapMin READ outerGapMin CONSTANT)
    Q_PROPERTY(int outerGapMax READ outerGapMax CONSTANT)
    /// Stable ids (UUID strings, braces) of the three managed baseline rules the
    /// page edits, one per concern. QML routes each action's read/write to the
    /// matching rule and passes the id to the RuleController's
    /// ruleJson() / updateRuleFromJson() calls.
    Q_PROPERTY(QString borderBaselineRuleId READ borderBaselineRuleId CONSTANT)
    Q_PROPERTY(QString titleBarBaselineRuleId READ titleBarBaselineRuleId CONSTANT)
    Q_PROPERTY(QString gapBaselineRuleId READ gapBaselineRuleId CONSTANT)

public:
    explicit WindowAppearanceController(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("window-appearance"), parent)
    {
    }

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    int borderWidthMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMin;
    }
    int borderWidthMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMax;
    }
    int borderRadiusMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMin;
    }
    int borderRadiusMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMax;
    }
    int borderWidthDefault() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidth;
    }
    int borderRadiusDefault() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadius;
    }
    QString borderBaselineRuleId() const
    {
        return ConfigDefaults::baselineBorderRuleId().toString();
    }
    QString titleBarBaselineRuleId() const
    {
        return ConfigDefaults::baselineTitleBarRuleId().toString();
    }
    QString gapBaselineRuleId() const
    {
        return ConfigDefaults::baselineGapRuleId().toString();
    }

    /// Deterministic, reproducible id (UUID string, braces) of the per-monitor
    /// gap-override Rule for @p screenId. A per-monitor gap override is an
    /// ordinary (non-managed) screen-scoped rule whose match is
    /// `ScreenId Equals screenId` and whose actions carry the gap values; it
    /// rides the context-gap cascade so it overrides the global baseline for
    /// that monitor only. The incoming identifier is resolved to its stable EDID
    /// form (Settings::canonicalPerScreenKey) before hashing, so the id agrees
    /// with the v4→v5 migration and the gap reader regardless of whether the
    /// scope chooser passed a connector name or a stable id. The id is a v5 UUID
    /// namespaced under the baseline gap rule so it is stable across restarts and
    /// reproducible from the screen id alone — QML cannot compute v5, so the page
    /// resolves it here and then drives find-or-create through the Rules
    /// controller's ruleJson() / addRuleFromJson() / updateRuleFromJson() /
    /// removeRule(). Returns an empty string for an empty screen id (the "all
    /// monitors" / global scope, which edits the gap baseline rule directly).
    Q_INVOKABLE QString perScreenGapRuleId(const QString& screenId) const;

    /// Stable EDID form of @p screenId (Settings::canonicalPerScreenKey), or the
    /// input unchanged when it is already a stable id or cannot be resolved.
    /// QML uses this to author a per-monitor gap rule's `ScreenId Equals` match
    /// in the same canonical form the rule id is keyed by.
    Q_INVOKABLE QString canonicalScreenId(const QString& screenId) const;

    // ── "Apply to" scope selector for the border / title-bar baselines ────────
    //
    // The baseline border and title-bar rules carry a window-property match that
    // scopes which windows their appearance applies to. The Appearance page
    // exposes this as an "Apply to" picker; these two helpers translate between
    // the picker's scope token and the rule's `match` JSON so the QML never has
    // to author the wire shape (and so the WindowType-as-int encoding stays in
    // C++). Scope tokens: "tiled" (snapped or autotile-managed), "normal" (all
    // ordinary application windows), "all" (every window — the catch-all).

    /// The `match` JSON for @p scope, ready to drop onto a rule. An unrecognized
    /// token returns the catch-all (the "all" scope), so a bad value never
    /// narrows the baseline unexpectedly.
    Q_INVOKABLE QJsonObject matchJsonForScope(const QString& scope) const;

    /// Classify a rule's @p match JSON back to its scope token: "tiled",
    /// "normal", "all" (catch-all or unparseable), or "custom" (a recognized but
    /// non-preset expression, e.g. one hand-authored on the Rules page).
    Q_INVOKABLE QString scopeOfMatch(const QJsonObject& match) const;

    int innerGapMin() const
    {
        return ConfigDefaults::innerGapMin();
    }
    int innerGapMax() const
    {
        return ConfigDefaults::innerGapMax();
    }
    int outerGapMin() const
    {
        return ConfigDefaults::outerGapMin();
    }
    int outerGapMax() const
    {
        return ConfigDefaults::outerGapMax();
    }
};

} // namespace PlasmaZones
