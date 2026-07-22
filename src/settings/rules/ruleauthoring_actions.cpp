// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruleauthoring.h"
#include "ruleauthoring_p.h"

#include "rulemodel.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/RuleAction.h>

#include <QLatin1StringView>
#include <QList>

#include <algorithm>

namespace PlasmaZones::RuleAuthoring {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::RuleAction;

/// Group an action type into a picker category. Most categories derive
/// straight from the descriptor's `category` field, so a new action in an
/// existing category needs no change here. The exception is `layoutEngine`,
/// which is split by action type into Engine, Snapping, and Tiling (the last
/// with Algorithm and Behavior submenus via a `/` in the label); a new
/// layoutEngine action lands in Engine unless added to the dispatch below.
PickerCategory actionCategory(const QString& type)
{
    const auto desc = PhosphorRules::ActionRegistry::instance().descriptor(type);
    if (!desc.has_value()) {
        return {PhosphorI18n::tr("Other"), 99};
    }
    const QString& cat = desc->category;
    // Two groups, alphabetised within each: the context-domain categories
    // (resolved per screen/desktop/activity/mode) come first (orders 0-4), then
    // the window-domain categories (orders 5-7). Keep these orders in lockstep
    // with each category's action domains in RuleAction.cpp.
    if (cat == QLatin1String("gap")) {
        return {PhosphorI18n::tr("Gaps"), 0};
    }
    if (cat == QLatin1String("layoutEngine")) {
        // The old flat "Layout & engine" list is split by what each action
        // drives. The engine controls stand alone; the two arrangement engines
        // (snapping and tiling) each get their own category, and tiling is
        // further split into Algorithm and Behavior submenus (a `/` in the
        // label, which CategoryMenuButton renders as a nested submenu).
        if (type == ActionType::SetSnappingLayout || type == ActionType::DefaultLayoutAssignment) {
            return {PhosphorI18n::tr("Snapping"), 2};
        }
        if (type == ActionType::SetTilingAlgorithm || type == ActionType::SetAlgorithmParam) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Algorithm"), 3};
        }
        if (type == ActionType::SetMaxWindows || type == ActionType::SetSplitRatio || type == ActionType::SetMasterCount
            || type == ActionType::SetInsertPosition || type == ActionType::SetOverflowBehavior
            || type == ActionType::SetDragBehavior) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Behavior"), 3};
        }
        // Cross-cutting engine controls: SetEngineMode / DisableEngine / LockContext.
        return {PhosphorI18n::tr("Engine"), 1};
    }
    if (cat == QLatin1String("overlay")) {
        return {PhosphorI18n::tr("Overlay"), 4};
    }
    if (cat == QLatin1String("animation")) {
        return {PhosphorI18n::tr("Animation"), 5};
    }
    if (cat == QLatin1String("appearance") || cat == QLatin1String("borderAppearance")) {
        return {PhosphorI18n::tr("Appearance"), 6};
    }
    if (cat == QLatin1String("windowManagement")) {
        return {PhosphorI18n::tr("Window"), 7};
    }
    return {PhosphorI18n::tr("Other"), 99};
}

/// Translated label for one param key on action @p type. The structural
/// schema (kind, min/max, scale, enum wire values) lives on the LGPL
/// `ActionDescriptor` in PhosphorRules; the GPL settings layer adds
/// the user-visible label per `(type, key)` pair so translation runs
/// through `PhosphorI18n::tr` and `lupdate` extracts the strings. A missing
/// entry falls back to the wire key — visible in the picker, so a missing
/// entry stands out for the next translator pass.
QString paramLabel(const QString& type, const QString& key)
{
    namespace ActionParam = PhosphorRules::ActionParam;
    if (type == ActionType::SetEngineMode && key == ActionParam::Mode) {
        return PhosphorI18n::tr("Engine mode");
    }
    if (type == ActionType::SetSnappingLayout && key == ActionParam::LayoutId) {
        return PhosphorI18n::tr("Snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm && key == ActionParam::Algorithm) {
        return PhosphorI18n::tr("Tiling algorithm");
    }
    if (type == ActionType::SetAlgorithmParam && key == ActionParam::Algorithm) {
        return PhosphorI18n::tr("Algorithm");
    }
    if (type == ActionType::SetMaxWindows && key == ActionParam::Value) {
        return PhosphorI18n::tr("Max tiled windows");
    }
    if (type == ActionType::SetSplitRatio && key == ActionParam::Value) {
        return PhosphorI18n::tr("Split ratio (%)");
    }
    if (type == ActionType::SetMasterCount && key == ActionParam::Value) {
        return PhosphorI18n::tr("Master count");
    }
    if (type == ActionType::SetInsertPosition && key == ActionParam::Value) {
        return PhosphorI18n::tr("Insert position");
    }
    if (type == ActionType::SetOverflowBehavior && key == ActionParam::Value) {
        return PhosphorI18n::tr("Overflow behavior");
    }
    if (type == ActionType::SetDragBehavior && key == ActionParam::Value) {
        return PhosphorI18n::tr("Drag behavior");
    }
    if (type == ActionType::DisableEngine && key == ActionParam::Mode) {
        return PhosphorI18n::tr("Engine to disable");
    }
    if (type == ActionType::SetOpacity && key == ActionParam::Value) {
        return PhosphorI18n::tr("Opacity (%)");
    }
    if (type == ActionType::SnapToZone && key == ActionParam::Zones) {
        return PhosphorI18n::tr("Zones");
    }
    // Unsnapped-position restore override (window-domain, single bool value).
    // off is not inert: it force-suppresses restore for matched windows,
    // overriding the per-engine restore-on-login setting, so the off meaning
    // is spelled out the same way the other bool actions do.
    if (type == ActionType::RestorePosition && key == ActionParam::Value) {
        return PhosphorI18n::tr("Restore position on login (off = don't restore)");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin && key == ActionParam::Value) {
        return PhosphorI18n::tr("Restore to zone on login (off = don't restore)");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Restore size on unsnap (off = keep zone size)");
    }
    if (type == ActionType::SetWindowLayer && key == ActionParam::Value) {
        return PhosphorI18n::tr("Layer");
    }
    // Border / title-bar overrides (all single-value, keyed ActionParam::Value).
    // SetHideTitleBar is tri-state at the effect: rule absent = mode decides,
    // ON = hide, OFF = force the title bar visible even where the mode hides
    // it — the label spells that out so the off position doesn't read as inert.
    if (type == ActionType::SetHideTitleBar && key == ActionParam::Value) {
        return PhosphorI18n::tr("Hide title bars (off = force visible)");
    }
    if (type == ActionType::SetBorderVisible && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show border (off = hide)");
    }
    if (type == ActionType::SetBorderWidth && key == ActionParam::Value) {
        return PhosphorI18n::tr("Border width (px)");
    }
    if (type == ActionType::SetBorderRadius && key == ActionParam::Value) {
        return PhosphorI18n::tr("Corner radius (px)");
    }
    if ((type == ActionType::SetBorderColorActive || type == ActionType::SetBorderColorInactive)
        && key == ActionParam::Value) {
        // The action label already carries focused/unfocused, so the single
        // colour param just reads "Border color".
        return PhosphorI18n::tr("Border color");
    }
    if (type == ActionType::SetOpacityTintVisible && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show opacity and tint (off = hide)");
    }
    if (type == ActionType::SetTintStrength && key == ActionParam::Value) {
        return PhosphorI18n::tr("Tint strength (%)");
    }
    if (type == ActionType::SetTintColor && key == ActionParam::Value) {
        return PhosphorI18n::tr("Tint color");
    }
    // Per-context gap overrides (all single-value, keyed ActionParam::Value).
    if (type == ActionType::SetInnerGap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Inner gap (px)");
    }
    if (type == ActionType::SetOuterGap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Outer gap (px)");
    }
    if (type == ActionType::SetUsePerSideOuterGap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Use per-side outer gaps (off = one uniform gap)");
    }
    if (type == ActionType::LockContext && key == ActionParam::Value) {
        // The action is the rule-driven counterpart to the ToggleLayoutLock
        // shortcut: on = the matched context's active layout can't be switched.
        // off is "don't lock" (not "no change"): the Locked slot is single-winner
        // by priority, so a higher-priority off rule cancels a lower-priority on.
        return PhosphorI18n::tr("Lock the layout (off = don't lock)");
    }
    if (type == ActionType::DefaultLayoutAssignment && key == ActionParam::Value) {
        // on = this context gets the global default layout even when the global
        // "don't assign by default" setting is on; off = suppress the default for
        // this context (no layout until one is explicitly assigned), overriding
        // the global setting the other way. Single-winner by priority.
        return PhosphorI18n::tr("Assign a default layout (off = leave unassigned)");
    }
    if (type == ActionType::SetOuterGapTop && key == ActionParam::Value) {
        return PhosphorI18n::tr("Top gap (px)");
    }
    if (type == ActionType::SetOuterGapBottom && key == ActionParam::Value) {
        return PhosphorI18n::tr("Bottom gap (px)");
    }
    if (type == ActionType::SetOuterGapLeft && key == ActionParam::Value) {
        return PhosphorI18n::tr("Left gap (px)");
    }
    if (type == ActionType::SetOuterGapRight && key == ActionParam::Value) {
        return PhosphorI18n::tr("Right gap (px)");
    }
    // Context overlay-property overrides. These come BEFORE the generic
    // EffectId / Value fallbacks so they win for the overlay actions.
    if (type == ActionType::OverrideOverlayShader && key == ActionParam::EffectId) {
        return PhosphorI18n::tr("Overlay shader");
    }
    if (type == ActionType::OverrideOverlayStyle && key == ActionParam::Value) {
        return PhosphorI18n::tr("Overlay style");
    }
    // Context overlay-appearance overrides (all single-value, keyed ActionParam::Value).
    if (type == ActionType::SetOverlayHighlightColor && key == ActionParam::Value) {
        return PhosphorI18n::tr("Highlight color");
    }
    if (type == ActionType::SetOverlayInactiveColor && key == ActionParam::Value) {
        return PhosphorI18n::tr("Inactive zone color");
    }
    if (type == ActionType::SetOverlayBorderColor && key == ActionParam::Value) {
        return PhosphorI18n::tr("Border color");
    }
    if (type == ActionType::SetOverlayActiveOpacity && key == ActionParam::Value) {
        return PhosphorI18n::tr("Active opacity (%)");
    }
    if (type == ActionType::SetOverlayInactiveOpacity && key == ActionParam::Value) {
        return PhosphorI18n::tr("Inactive opacity (%)");
    }
    if (type == ActionType::SetOverlayBorderWidth && key == ActionParam::Value) {
        return PhosphorI18n::tr("Border width (px)");
    }
    if (type == ActionType::SetOverlayBorderRadius && key == ActionParam::Value) {
        return PhosphorI18n::tr("Corner radius (px)");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show zone numbers (off = hide)");
    }
    if (type == ActionType::RouteToScreen && key == ActionParam::TargetScreenId) {
        return PhosphorI18n::tr("Monitor");
    }
    if (type == ActionType::RouteToDesktop && key == ActionParam::TargetDesktop) {
        return PhosphorI18n::tr("Desktop");
    }
    if (key == ActionParam::Event) {
        return PhosphorI18n::tr("Event");
    }
    if (key == ActionParam::Chain) {
        return PhosphorI18n::tr("Decoration packs");
    }
    if (key == ActionParam::EffectId) {
        return PhosphorI18n::tr("Shader effect");
    }
    if (key == ActionParam::DurationMs) {
        return PhosphorI18n::tr("Duration (ms)");
    }
    if (key == ActionParam::Curve) {
        return PhosphorI18n::tr("Curve");
    }
    return key;
}

/// Optional translated input hint for action @p type, param @p key — a short line
/// of guidance shown beneath the editor for params whose accepted input format is
/// not obvious from the control itself (e.g. the free-text zone-ordinal list).
/// Returns an empty string for params that need no hint (pickers, spin boxes,
/// toggles, colour swatches are self-explanatory). Mirrors paramLabel — keyed on
/// `(type, key)` so the hint stays next to the label it explains.
QString paramHint(const QString& type, const QString& key)
{
    namespace ActionParam = PhosphorRules::ActionParam;
    if (type == ActionType::SnapToZone && key == ActionParam::Zones) {
        return PhosphorI18n::tr(
            "Zone numbers like “1, 2”, or a range like “1-3”. "
            "Multiple zones snap the window to their combined area.");
    }
    return {};
}

/// The parameter schema for @p type, derived from the LGPL ActionDescriptor's
/// structural `params` and supplemented by GPL-side translated labels. The
/// QML editor's per-param Loader dispatches on `kind`, so the wire shape
/// here is the contract between the descriptor and the editor.
QVariantList paramsForActionTypeImpl(const QString& type)
{
    QVariantList params;
    const auto descriptor = PhosphorRules::ActionRegistry::instance().descriptor(type);
    if (!descriptor.has_value()) {
        return params;
    }
    for (const PhosphorRules::ParamSchema& schema : descriptor->params) {
        // A `ParamSchema` with an empty `key` is a misregistered descriptor
        // — the strict-key check in `RuleAction::fromJson` would reject any
        // payload built against it, leaving the editor with a permanently
        // un-savable row. Skip silently: rendering the row would just
        // expose a no-op input to the user with no recovery path.
        if (schema.key.isEmpty()) {
            continue;
        }
        QVariantMap p;
        p[QStringLiteral("key")] = schema.key;
        p[QStringLiteral("kind")] = schema.kind;
        p[QStringLiteral("label")] = paramLabel(type, schema.key);
        if (const QString hint = paramHint(type, schema.key); !hint.isEmpty()) {
            p[QStringLiteral("hint")] = hint;
        }
        // Bool params carry a polarity-aware on/off caption (e.g. "Show border" /
        // "Hide border") so the editor toggle reads out its current effect rather
        // than a bare On/Off. Shared with the rule-list summary through
        // boolActionStateLabel, so the editor and the summary never diverge.
        if (schema.kind == QLatin1String("bool")) {
            if (const QString onLabel = boolActionStateLabel(type, true); !onLabel.isEmpty()) {
                p[QStringLiteral("onLabel")] = onLabel;
            }
            if (const QString offLabel = boolActionStateLabel(type, false); !offLabel.isEmpty()) {
                p[QStringLiteral("offLabel")] = offLabel;
            }
        }
        if (schema.min.has_value()) {
            p[QStringLiteral("min")] = *schema.min;
        }
        if (schema.max.has_value()) {
            p[QStringLiteral("max")] = *schema.max;
        }
        if (schema.scale.has_value()) {
            p[QStringLiteral("scale")] = *schema.scale;
        }
        if (schema.defaultDisplay.has_value()) {
            p[QStringLiteral("defaultDisplay")] = *schema.defaultDisplay;
        }
        if (!schema.enumWireValues.isEmpty()) {
            QVariantList options;
            options.reserve(schema.enumWireValues.size());
            for (const QString& wire : schema.enumWireValues) {
                QVariantMap option;
                option[QStringLiteral("value")] = wire;
                option[QStringLiteral("label")] = enumOptionLabel(type, schema.key, wire);
                options.append(option);
            }
            p[QStringLiteral("options")] = options;
        }
        params.append(p);
    }
    return params;
}

QString actionTypeLabelImpl(const QString& type)
{
    if (type == ActionType::SetEngineMode) {
        return PhosphorI18n::tr("Set engine mode");
    }
    if (type == ActionType::SetSnappingLayout) {
        return PhosphorI18n::tr("Set snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm) {
        return PhosphorI18n::tr("Set tiling algorithm");
    }
    if (type == ActionType::SetMaxWindows) {
        return PhosphorI18n::tr("Set max tiled windows");
    }
    if (type == ActionType::SetSplitRatio) {
        return PhosphorI18n::tr("Set split ratio");
    }
    if (type == ActionType::SetMasterCount) {
        return PhosphorI18n::tr("Set master count");
    }
    if (type == ActionType::SetInsertPosition) {
        return PhosphorI18n::tr("Set insert position");
    }
    if (type == ActionType::SetOverflowBehavior) {
        return PhosphorI18n::tr("Set overflow behavior");
    }
    if (type == ActionType::SetDragBehavior) {
        return PhosphorI18n::tr("Set drag behavior");
    }
    if (type == ActionType::SetAlgorithmParam) {
        return PhosphorI18n::tr("Set algorithm parameter");
    }
    if (type == ActionType::DisableEngine) {
        return PhosphorI18n::tr("Disable engine");
    }
    if (type == ActionType::LockContext) {
        return PhosphorI18n::tr("Lock layout");
    }
    if (type == ActionType::DefaultLayoutAssignment) {
        return PhosphorI18n::tr("Default layout assignment");
    }
    if (type == ActionType::Exclude) {
        return PhosphorI18n::tr("Exclude window");
    }
    if (type == ActionType::Float) {
        return PhosphorI18n::tr("Float window");
    }
    if (type == ActionType::SnapToZone) {
        return PhosphorI18n::tr("Snap to zone(s)");
    }
    if (type == ActionType::RestorePosition) {
        return PhosphorI18n::tr("Restore position on login");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin) {
        return PhosphorI18n::tr("Restore to zone on login");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap) {
        return PhosphorI18n::tr("Restore size on unsnap");
    }
    if (type == ActionType::SetWindowLayer) {
        return PhosphorI18n::tr("Set window layer");
    }
    if (type == ActionType::OverrideAnimationShader) {
        return PhosphorI18n::tr("Override animation shader");
    }
    if (type == ActionType::OverrideDecorationChain) {
        return PhosphorI18n::tr("Override decoration packs");
    }
    if (type == ActionType::OverrideAnimationTiming) {
        return PhosphorI18n::tr("Override animation duration");
    }
    if (type == ActionType::OverrideAnimationCurve) {
        return PhosphorI18n::tr("Override animation curve");
    }
    if (type == ActionType::SetOpacity) {
        return PhosphorI18n::tr("Set opacity");
    }
    if (type == ActionType::OverrideOverlayShader) {
        return PhosphorI18n::tr("Set overlay shader");
    }
    if (type == ActionType::OverrideOverlayStyle) {
        return PhosphorI18n::tr("Set overlay style");
    }
    if (type == ActionType::SetOverlayHighlightColor) {
        return PhosphorI18n::tr("Set overlay highlight color");
    }
    if (type == ActionType::SetOverlayInactiveColor) {
        return PhosphorI18n::tr("Set overlay inactive color");
    }
    if (type == ActionType::SetOverlayBorderColor) {
        return PhosphorI18n::tr("Set overlay border color");
    }
    if (type == ActionType::SetOverlayActiveOpacity) {
        return PhosphorI18n::tr("Set overlay active opacity");
    }
    if (type == ActionType::SetOverlayInactiveOpacity) {
        return PhosphorI18n::tr("Set overlay inactive opacity");
    }
    if (type == ActionType::SetOverlayBorderWidth) {
        return PhosphorI18n::tr("Set overlay border width");
    }
    if (type == ActionType::SetOverlayBorderRadius) {
        return PhosphorI18n::tr("Set overlay corner radius");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return PhosphorI18n::tr("Show zone numbers");
    }
    if (type == ActionType::ExcludeAnimations) {
        return PhosphorI18n::tr("Exclude from animations");
    }
    if (type == ActionType::SetHideTitleBar) {
        // Affirmative verb phrase like the other boolean action labels (e.g.
        // SetBorderVisible's "Show border"); the on-state is "hide". The
        // parameter toggle's "(off = force visible)" wording carries the
        // tri-state detail (off forces the title bar visible even where the
        // mode would otherwise hide it).
        return PhosphorI18n::tr("Hide title bars");
    }
    if (type == ActionType::SetBorderVisible) {
        return PhosphorI18n::tr("Show border");
    }
    if (type == ActionType::SetBorderWidth) {
        return PhosphorI18n::tr("Set border width");
    }
    if (type == ActionType::SetBorderRadius) {
        return PhosphorI18n::tr("Set corner radius");
    }
    if (type == ActionType::SetBorderColorActive) {
        return PhosphorI18n::tr("Set focused border color");
    }
    if (type == ActionType::SetBorderColorInactive) {
        return PhosphorI18n::tr("Set unfocused border color");
    }
    if (type == ActionType::SetOpacityTintVisible) {
        return PhosphorI18n::tr("Show opacity and tint");
    }
    if (type == ActionType::SetTintStrength) {
        return PhosphorI18n::tr("Set tint strength");
    }
    if (type == ActionType::SetTintColor) {
        return PhosphorI18n::tr("Set tint color");
    }
    if (type == ActionType::SetInnerGap) {
        return PhosphorI18n::tr("Set inner gap");
    }
    if (type == ActionType::SetOuterGap) {
        return PhosphorI18n::tr("Set outer gap");
    }
    if (type == ActionType::SetUsePerSideOuterGap) {
        return PhosphorI18n::tr("Use per-side outer gaps");
    }
    if (type == ActionType::SetOuterGapTop) {
        return PhosphorI18n::tr("Set top gap");
    }
    if (type == ActionType::SetOuterGapBottom) {
        return PhosphorI18n::tr("Set bottom gap");
    }
    if (type == ActionType::SetOuterGapLeft) {
        return PhosphorI18n::tr("Set left gap");
    }
    if (type == ActionType::SetOuterGapRight) {
        return PhosphorI18n::tr("Set right gap");
    }
    if (type == ActionType::RouteToScreen) {
        return PhosphorI18n::tr("Open on monitor");
    }
    if (type == ActionType::RouteToDesktop) {
        return PhosphorI18n::tr("Open on desktop");
    }
    return RuleModel::actionTypeFallbackLabel(type);
}

} // namespace

QString boolActionStateLabel(const QString& type, bool on)
{
    namespace ActionType = PhosphorRules::ActionType;
    if (type == ActionType::RestorePosition) {
        return on ? PhosphorI18n::tr("Restore position on login") : PhosphorI18n::tr("Don't restore position on login");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin) {
        return on ? PhosphorI18n::tr("Restore to zone on login") : PhosphorI18n::tr("Don't restore to zone on login");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap) {
        return on ? PhosphorI18n::tr("Restore size on unsnap") : PhosphorI18n::tr("Keep zone size on unsnap");
    }
    if (type == ActionType::SetHideTitleBar) {
        return on ? PhosphorI18n::tr("Hide title bars") : PhosphorI18n::tr("Show title bars");
    }
    if (type == ActionType::LockContext) {
        return on ? PhosphorI18n::tr("Lock layout") : PhosphorI18n::tr("Don't lock layout");
    }
    if (type == ActionType::DefaultLayoutAssignment) {
        return on ? PhosphorI18n::tr("Assign default layout") : PhosphorI18n::tr("Don't assign default layout");
    }
    if (type == ActionType::SetBorderVisible) {
        return on ? PhosphorI18n::tr("Show border") : PhosphorI18n::tr("Hide border");
    }
    if (type == ActionType::SetOpacityTintVisible) {
        return on ? PhosphorI18n::tr("Show opacity and tint") : PhosphorI18n::tr("Hide opacity and tint");
    }
    if (type == ActionType::SetUsePerSideOuterGap) {
        return on ? PhosphorI18n::tr("Per-side outer gaps") : PhosphorI18n::tr("Uniform outer gap");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return on ? PhosphorI18n::tr("Show zone numbers") : PhosphorI18n::tr("Hide zone numbers");
    }
    return QString();
}

QVariantList actionTypes()
{
    const PhosphorRules::ActionRegistry& registry = PhosphorRules::ActionRegistry::instance();

    struct TypeEntry
    {
        QString type;
        QString categoryLabel;
        int categoryOrder;
        int displayOrder;
    };
    QList<TypeEntry> entries;
    for (const QString& type : registry.registeredTypes()) {
        const auto desc = registry.descriptor(type);
        if (!desc.has_value() || !desc->userAuthorable) {
            continue;
        }
        const PickerCategory acat = actionCategory(type);
        entries.append({type, acat.label, acat.order, desc->displayOrder});
    }
    std::sort(entries.begin(), entries.end(), [](const TypeEntry& a, const TypeEntry& b) {
        if (a.categoryOrder != b.categoryOrder) {
            return a.categoryOrder < b.categoryOrder;
        }
        if (a.displayOrder != b.displayOrder) {
            return a.displayOrder < b.displayOrder;
        }
        return a.type < b.type;
    });

    QVariantList out;
    for (const TypeEntry& e : entries) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = e.type;
        entry[QStringLiteral("label")] = actionTypeLabelImpl(e.type);
        entry[QStringLiteral("params")] = paramsForActionTypeImpl(e.type);
        entry[QStringLiteral("category")] = e.categoryLabel;
        entry[QStringLiteral("categoryOrder")] = e.categoryOrder;
        RuleAction probe;
        probe.type = e.type;
        const auto domain = registry.domainFor(probe);
        const QString domainStr =
            domain == PhosphorRules::ActionDomain::Context ? QStringLiteral("context") : QStringLiteral("window");
        entry[QStringLiteral("domain")] = domainStr;
        // The picker draws a divider between top-level categories whose group
        // differs — context-domain categories above, window-domain below.
        entry[QStringLiteral("categoryGroup")] = domainStr;
        out.append(entry);
    }
    return out;
}

QVariantMap defaultPayloadFor(const QString& typeWire)
{
    // Walk the action's parameter descriptor and seed each entry with a
    // kind-appropriate default. The QML side previously open-coded this in a
    // `_defaultParamValue` helper inside ActionListEditor — moving it here
    // makes the descriptor + its defaults the single source of truth, so a
    // newly-appended action and a type-switched action both land on the same
    // shape (which prevents the type-switch leaving the SpinBox at 0 with
    // canSave gating the rest of the editor).
    QVariantMap payload;
    payload[QStringLiteral("type")] = typeWire;

    const QVariantList params = paramsForActionTypeImpl(typeWire);
    for (const QVariant& v : params) {
        const QVariantMap p = v.toMap();
        const QString key = p.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            continue;
        }
        const QString kind = p.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("enum")) {
            // Enum options carry `{value, label}` pairs — wire form is `value`.
            // Tolerate the legacy bare-string shape so a future descriptor that
            // skips the wrap doesn't silently default to "".
            const QVariantList options = p.value(QStringLiteral("options")).toList();
            if (options.isEmpty()) {
                payload[key] = QString();
                continue;
            }
            const QVariant& first = options.first();
            if (first.canConvert<QVariantMap>()) {
                payload[key] = first.toMap().value(QStringLiteral("value")).toString();
            } else {
                payload[key] = first.toString();
            }
        } else if (kind == QLatin1String("number") || kind == QLatin1String("percent")) {
            // Seed order: `defaultDisplay` (if the descriptor declared a
            // safe-but-not-`min` starting value) → `min` → 0.0. `min` and
            // `defaultDisplay` are both expressed in *display* units (see
            // `ParamSchema` doc in RuleAction.h). For `percent` the wire
            // value is `display * scale` — so the seed must run through
            // that same conversion or the rule lands with a wire value
            // far outside the validator's range. `defaultDisplay` lets a
            // descriptor like SetOpacity start at 100% (no visible change)
            // instead of seeding `min = 0%` (a saveable-but-invisible rule).
            const QVariant defaultDisplay = p.value(QStringLiteral("defaultDisplay"));
            const QVariant min = p.value(QStringLiteral("min"));
            const QVariant displaySource = defaultDisplay.isValid() ? defaultDisplay : min;
            if (!displaySource.isValid()) {
                payload[key] = QVariant(0.0);
            } else if (kind == QLatin1String("percent")) {
                // Percent kind requires `scale` per the ParamSchema doc
                // (`stored = display * scale`). A descriptor declaring
                // `percent` without `scale` is a programmer error in the
                // registry — fall back to `0.0` (scale-invariant safe
                // default) rather than seeding the un-scaled display
                // value, which would reintroduce the exact bug the
                // scale multiplication was added to fix.
                const QVariant scale = p.value(QStringLiteral("scale"));
                payload[key] = scale.isValid() ? QVariant(displaySource.toDouble() * scale.toDouble()) : QVariant(0.0);
            } else {
                payload[key] = displaySource;
            }
        } else if (kind == QLatin1String("bool")) {
            // Bool kind seeds to a real boolean so the wire value is valid from
            // the first save. `defaultDisplay` is the schema's numeric field, so
            // a descriptor expresses its default toggle as 0.0 (off) / 1.0 (on);
            // we reinterpret it as a bool via `!= 0.0` (QVariant::toBool). When
            // absent, seed `false`. Without this branch a bool-kind descriptor
            // would fall into the `payload[key] = QString()` default and fail the
            // validator on save.
            const QVariant defaultDisplay = p.value(QStringLiteral("defaultDisplay"));
            payload[key] = defaultDisplay.isValid() ? QVariant(defaultDisplay.toBool()) : QVariant(false);
        } else if (kind == QLatin1String("color")) {
            // Colour kind has no numeric `defaultDisplay` (that field is a
            // double). The border-colour actions seed the accent sentinel so a
            // fresh rule follows the system accent until the user picks a colour.
            // The overlay-colour actions have NO accent concept (their consumer
            // resolves no token — validator is plain hex), so seed a concrete
            // hex (the Plasma default blue, matching the colour picker's own
            // empty-value fallback) that passes `hasHexColor`.
            const bool isAccentColor = typeWire == QString(PhosphorRules::ActionType::SetBorderColorActive)
                || typeWire == QString(PhosphorRules::ActionType::SetBorderColorInactive)
                || typeWire == QString(PhosphorRules::ActionType::SetTintColor);
            payload[key] =
                isAccentColor ? QString(PhosphorRules::BorderColorToken::Accent) : QStringLiteral("#FF3DAEE9");
        } else if (kind == QLatin1String("zoneOrdinals")) {
            // Seed a valid single-zone default ([1]) so a fresh SnapToZone rule
            // passes the validator (non-empty array of positive ordinals) before
            // the user edits the zone list.
            payload[key] = QVariantList{1};
        } else if (kind == QLatin1String("decorationChain")) {
            // Seed an empty array: unlike zoneOrdinals, an empty chain IS a
            // valid payload (the "no decoration" sentinel), so a fresh
            // OverrideDecorationChain rule is savable immediately and the
            // user stacks packs from there.
            payload[key] = QVariantList{};
        } else if (kind == QLatin1String("virtualDesktop")) {
            // Seed desktop 1 so a fresh RouteToDesktop rule passes the validator
            // (a 1-based ordinal) before the user picks a desktop.
            payload[key] = 1;
        } else {
            // Picker kinds (snappingLayout, tilingAlgorithm, animationEvent,
            // shaderEffect, curveEditor, screenId) and plain strings all start
            // empty (zoneOrdinals and virtualDesktop are seeded above because their
            // validators reject an empty value). The user has to choose a value
            // before the rule is savable, and `canSave` surfaces the gap explicitly.
            // Seeding a placeholder here would mask the "user has to pick" state.
            payload[key] = QString();
        }
    }
    return payload;
}

} // namespace PlasmaZones::RuleAuthoring
