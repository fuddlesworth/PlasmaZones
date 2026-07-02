// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The managed baseline rule definitions (border, title bar, gap, zone overlay,
// general min-size, animation min-size). Moved out of the daemon's anonymous
// namespace so the settings app's per-page reset shares one source of truth
// with the daemon's startup seeding (see baselinerules.h).

#include "baselinerules.h"

#include "../config/configdefaults.h"
#include "../phosphor_i18n.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <QJsonValue>
#include <QLatin1StringView>

#include <limits>

namespace PlasmaZones {

PhosphorRules::Rule makeBaselineSkeleton(const QUuid& id, const QString& name)
{
    using namespace PhosphorRules;
    Rule rule;
    rule.id = id;
    rule.name = name;
    rule.managed = true;
    // Lowest possible precedence — any user rule (all of which carry a higher
    // priority) overrides the baseline per slot. renormalizePriorities in the
    // settings controller deliberately leaves managed rules pinned here.
    rule.priority = std::numeric_limits<int>::min();
    rule.match = MatchExpression{}; // empty All{} — matches every window
    return rule;
}

// Build the managed baseline BORDER rule: the lowest-priority baseline rule
// carrying only the "show border" parent action, which defaults OFF (opt-in).
// Its match is scoped to tiled / snapped windows on a fresh install, so it is no
// longer a catch-all. The dependent border details (width, radius, colours) are
// not seeded here — the Appearance page adds them when "show border" turns on
// and removes them when it turns off, so the baseline stays minimal.
PhosphorRules::Rule makeBaselineBorderRule()
{
    using namespace PhosphorRules;
    namespace DD = PhosphorCompositor::DecorationDefaults;

    const auto action = [](QLatin1StringView type, QLatin1StringView key, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(key), value);
        return a;
    };

    Rule rule = makeBaselineSkeleton(ConfigDefaults::baselineBorderRuleId(), PhosphorI18n::tr("Default borders"));
    // Fresh-install default: draw the baseline border only on tiled / snapped
    // windows, the behaviour before appearance moved onto rules. The Appearance
    // page's "Apply to" selector rewrites this match; the seeder never re-pins it,
    // so an existing install keeps whatever scope it already carries.
    rule.match = ConfigDefaults::tiledAndSnappedScopeMatch();
    rule.actions = {
        action(ActionType::SetBorderVisible, ActionParam::Value, DD::ShowBorder),
    };
    return rule;
}

// Build the managed baseline TITLE BAR rule: the lowest-priority baseline rule
// carrying the default hide-title-bar value. Its match is scoped to tiled /
// snapped windows on a fresh install, so it is not a catch-all.
PhosphorRules::Rule makeBaselineTitleBarRule()
{
    using namespace PhosphorRules;
    namespace DD = PhosphorCompositor::DecorationDefaults;

    const auto action = [](QLatin1StringView type, QLatin1StringView key, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(key), value);
        return a;
    };

    Rule rule = makeBaselineSkeleton(ConfigDefaults::baselineTitleBarRuleId(), PhosphorI18n::tr("Default title bars"));
    // Fresh-install default: hide the title bar only on tiled / snapped windows,
    // the behaviour before appearance moved onto rules. The Appearance page's
    // "Apply to" selector rewrites this match; the seeder never re-pins it, so an
    // existing install keeps whatever scope it already carries.
    rule.match = ConfigDefaults::tiledAndSnappedScopeMatch();
    rule.actions = {
        action(ActionType::SetHideTitleBar, ActionParam::Value, DD::HideTitleBars),
    };
    return rule;
}

// Build the managed baseline GAP rule: the catch-all, lowest-priority rule that
// is the single source of truth for the shared inner/outer gap model (Settings
// reads these actions back as its innerGap()/outerGap*() getters). These are
// Context-domain actions; resolveContextGaps EXCLUDES this managed rule so the
// values surface only as the level-4 global default, never as a top-tier context
// override. Only the parent actions (inner gap, outer gap, and the per-side
// toggle, which defaults off) are seeded. The four per-side outer-gap actions are
// added by the Appearance page when the user turns per-side gaps on and removed
// when off, so an absent per-side action falls back to the uniform outer gap.
PhosphorRules::Rule makeBaselineGapRule()
{
    using namespace PhosphorRules;

    const auto action = [](QLatin1StringView type, QLatin1StringView key, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(key), value);
        return a;
    };

    Rule rule = makeBaselineSkeleton(ConfigDefaults::baselineGapRuleId(), PhosphorI18n::tr("Default gaps"));
    rule.actions = {
        action(ActionType::SetInnerGap, ActionParam::Value, ConfigDefaults::innerGap()),
        action(ActionType::SetOuterGap, ActionParam::Value, ConfigDefaults::outerGap()),
        action(ActionType::SetUsePerSideOuterGap, ActionParam::Value, ConfigDefaults::usePerSideOuterGap()),
    };
    return rule;
}

// Build the managed baseline ZONE-OVERLAY appearance rule: the catch-all,
// lowest-priority rule that is the single source of truth for the global
// drag-overlay appearance (Settings reads these actions back as its
// highlightColor()/activeOpacity()/… getters). All seven appearance actions are
// seeded at their ConfigDefaults values; colours are `#AARRGGBB` hex. The
// `showZoneNumbers` toggle is deliberately NOT here — it lives in the effects
// group and stays plain config.
PhosphorRules::Rule makeBaselineOverlayRule()
{
    using namespace PhosphorRules;

    const auto action = [](QLatin1StringView type, QLatin1StringView key, const QJsonValue& value) {
        RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(key), value);
        return a;
    };

    Rule rule = makeBaselineSkeleton(ConfigDefaults::baselineOverlayRuleId(), PhosphorI18n::tr("Default zone overlay"));
    rule.actions = {
        action(ActionType::SetOverlayHighlightColor, ActionParam::Value,
               ConfigDefaults::highlightColor().name(QColor::HexArgb)),
        action(ActionType::SetOverlayInactiveColor, ActionParam::Value,
               ConfigDefaults::inactiveColor().name(QColor::HexArgb)),
        action(ActionType::SetOverlayBorderColor, ActionParam::Value,
               ConfigDefaults::borderColor().name(QColor::HexArgb)),
        action(ActionType::SetOverlayActiveOpacity, ActionParam::Value, ConfigDefaults::activeOpacity()),
        action(ActionType::SetOverlayInactiveOpacity, ActionParam::Value, ConfigDefaults::inactiveOpacity()),
        action(ActionType::SetOverlayBorderWidth, ActionParam::Value, ConfigDefaults::borderWidth()),
        action(ActionType::SetOverlayBorderRadius, ActionParam::Value, ConfigDefaults::borderRadius()),
    };
    return rule;
}

namespace {
// Shared builder for the managed min-size baselines (general exclusion and
// animation exclusion): a lowest-priority rule carrying the single param-less
// terminal @p actionType whose match is @p field LessThan @p threshold. The
// threshold lives in the MATCH (the terminal actions carry no params), so the
// owning settings page edits the match; a 0 threshold never matches (disabled).
PhosphorRules::Rule makeBaselineMinSizeRule(const QUuid& id, const QString& name, PhosphorRules::Field field,
                                            int threshold, QLatin1StringView actionType)
{
    using namespace PhosphorRules;
    Rule rule = makeBaselineSkeleton(id, name);
    rule.match = MatchExpression::makeLeaf(field, Operator::LessThan, QVariant(threshold));
    RuleAction terminal;
    terminal.type = QString(actionType);
    rule.actions = {terminal};
    return rule;
}
} // namespace

PhosphorRules::Rule makeBaselineGeneralMinWidthRule()
{
    return makeBaselineMinSizeRule(ConfigDefaults::generalMinWidthRuleId(), PhosphorI18n::tr("Exclude narrow windows"),
                                   PhosphorRules::Field::Width, ConfigDefaults::minimumWindowWidth(),
                                   PhosphorRules::ActionType::Exclude);
}

PhosphorRules::Rule makeBaselineGeneralMinHeightRule()
{
    return makeBaselineMinSizeRule(ConfigDefaults::generalMinHeightRuleId(), PhosphorI18n::tr("Exclude short windows"),
                                   PhosphorRules::Field::Height, ConfigDefaults::minimumWindowHeight(),
                                   PhosphorRules::ActionType::Exclude);
}

PhosphorRules::Rule makeBaselineAnimationMinWidthRule()
{
    return makeBaselineMinSizeRule(ConfigDefaults::animationMinWidthRuleId(),
                                   PhosphorI18n::tr("Skip animations for narrow windows"), PhosphorRules::Field::Width,
                                   ConfigDefaults::animationMinimumWindowWidth(),
                                   PhosphorRules::ActionType::ExcludeAnimations);
}

PhosphorRules::Rule makeBaselineAnimationMinHeightRule()
{
    return makeBaselineMinSizeRule(ConfigDefaults::animationMinHeightRuleId(),
                                   PhosphorI18n::tr("Skip animations for short windows"), PhosphorRules::Field::Height,
                                   ConfigDefaults::animationMinimumWindowHeight(),
                                   PhosphorRules::ActionType::ExcludeAnimations);
}

} // namespace PlasmaZones
