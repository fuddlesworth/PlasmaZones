// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruleauthoring.h"

#include "rulemodel.h"

#include "../phosphor_i18n.h"

#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <QLatin1StringView>
#include <QList>
#include <QSet>

#include <algorithm>
#include <array>

namespace PlasmaZones::RuleAuthoring {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::Field;
using PhosphorRules::Operator;
using PhosphorRules::RuleAction;

/// One picker category: a translated label + a stable sort order. The field
/// and action pickers group their (otherwise long, flat) entry lists into
/// fly-out submenus keyed by this.
struct PickerCategory
{
    QString label;
    int order;
};

/// Group a match Field into a picker category. The `Field` enum interleaves
/// state and context (e.g. IsMaximized sits after Activity), so the picker
/// groups by THIS classification, never by enum / emit order. The categories
/// are deliberately fine-grained: a single flat "State" bucket of ~19 entries
/// is hard to scan, so the window-kind, taskbar/switcher-hint, and
/// PlasmaZones-tiling concepts each get their own top-level fly-out. Items
/// within a category are sorted alphabetically by CategoryMenuButton; only the
/// returned order int controls the relative position of the categories. The
/// order ints below are assigned so the category labels sort alphabetically
/// (Context, Identity, Size, State, Taskbar & switcher, Tiling, Type).
PickerCategory fieldCategory(Field f)
{
    switch (f) {
    // Who the window is — identifiers a rule matches against.
    case Field::AppId:
    case Field::WindowClass:
    case Field::DesktopFile:
    case Field::WindowRole:
    case Field::Pid:
    case Field::Title:
    case Field::CaptionNormal:
        return {PhosphorI18n::tr("Identity"), 1};
    // What kind of window it is (its role/type), not a toggled runtime state.
    case Field::WindowType:
    case Field::IsTransient:
    case Field::IsModal:
    case Field::IsNotification:
        return {PhosphorI18n::tr("Type"), 6};
    // Live window-manager state and chrome flags.
    case Field::IsMaximized:
    case Field::IsMinimized:
    case Field::IsFullscreen:
    case Field::IsFocused:
    case Field::KeepAbove:
    case Field::KeepBelow:
    case Field::IsSticky:
    case Field::HasDecoration:
    case Field::IsResizable:
    case Field::IsMovable:
    case Field::IsMaximizable:
        return {PhosphorI18n::tr("State"), 3};
    // NETWM "skip" hints — whether the window opts out of the taskbar, pager,
    // or Alt+Tab switcher.
    case Field::SkipTaskbar:
    case Field::SkipPager:
    case Field::SkipSwitcher:
        return {PhosphorI18n::tr("Taskbar & switcher"), 4};
    // PlasmaZones-owned placement state.
    case Field::IsFloating:
    case Field::IsSnapped:
    case Field::IsTiled:
    case Field::Zone:
        return {PhosphorI18n::tr("Tiling"), 5};
    case Field::Width:
    case Field::Height:
    case Field::PositionX:
    case Field::PositionY:
        return {PhosphorI18n::tr("Size"), 2};
    case Field::ScreenId:
    case Field::VirtualDesktop:
    case Field::Activity:
    case Field::Mode:
    case Field::TiledWindowCount:
    case Field::ScreenOrientation:
    case Field::ActiveLayout:
        return {PhosphorI18n::tr("Context"), 0};
    }
    return {PhosphorI18n::tr("Other"), 99};
}

/// Short, user-facing help for one match Field — surfaced as the hover
/// tooltip on the leaf editor's info icon. Kept concise (one line); the
/// switch is exhaustive so every field, including the picker-hidden ones,
/// has a description.
QString fieldDescription(Field f)
{
    switch (f) {
    case Field::AppId:
        return PhosphorI18n::tr("The application's ID (Wayland app_id / desktop entry), e.g. org.kde.konsole.");
    case Field::WindowClass:
        return PhosphorI18n::tr("The window's class (WM_CLASS resource class), e.g. konsole.");
    case Field::DesktopFile:
        return PhosphorI18n::tr("The application's desktop entry file name.");
    case Field::WindowRole:
        return PhosphorI18n::tr("The window's X11 role (WM_WINDOW_ROLE). Empty for Wayland-native windows.");
    case Field::Pid:
        return PhosphorI18n::tr("The window's process ID.");
    case Field::Title:
        return PhosphorI18n::tr("The window's title-bar text.");
    case Field::WindowType:
        return PhosphorI18n::tr("The window's type (Normal, Dialog, Utility, Notification, …).");
    case Field::IsSticky:
        return PhosphorI18n::tr("Whether the window is shown on all virtual desktops.");
    case Field::IsFullscreen:
        return PhosphorI18n::tr("Whether the window is fullscreen.");
    case Field::IsMinimized:
        return PhosphorI18n::tr("Whether the window is minimized.");
    case Field::IsMaximized:
        return PhosphorI18n::tr("Whether the window is maximized.");
    case Field::IsFocused:
        return PhosphorI18n::tr("Whether the window currently has keyboard focus.");
    case Field::IsTransient:
        return PhosphorI18n::tr("Whether the window is a transient (a dialog or popup owned by another window).");
    case Field::IsNotification:
        return PhosphorI18n::tr("Whether the window is a notification or on-screen display.");
    case Field::Width:
        return PhosphorI18n::tr("The window's width in pixels.");
    case Field::Height:
        return PhosphorI18n::tr("The window's height in pixels.");
    case Field::KeepAbove:
        return PhosphorI18n::tr("Whether the window is set to stay above other windows (always on top).");
    case Field::KeepBelow:
        return PhosphorI18n::tr("Whether the window is set to stay below other windows.");
    case Field::SkipTaskbar:
        return PhosphorI18n::tr("Whether the window is hidden from the taskbar.");
    case Field::SkipPager:
        return PhosphorI18n::tr("Whether the window is hidden from the pager.");
    case Field::SkipSwitcher:
        return PhosphorI18n::tr("Whether the window is hidden from the window switcher (Alt+Tab).");
    case Field::IsModal:
        return PhosphorI18n::tr("Whether the window is a modal dialog.");
    case Field::HasDecoration:
        return PhosphorI18n::tr("Whether the window has a server-side title-bar and border.");
    case Field::IsResizable:
        return PhosphorI18n::tr("Whether the window can be resized.");
    case Field::IsMovable:
        return PhosphorI18n::tr("Whether the window can be moved.");
    case Field::IsMaximizable:
        return PhosphorI18n::tr("Whether the window can be maximized.");
    case Field::PositionX:
        return PhosphorI18n::tr("The window's left-edge X position in pixels.");
    case Field::PositionY:
        return PhosphorI18n::tr("The window's top-edge Y position in pixels.");
    case Field::CaptionNormal:
        return PhosphorI18n::tr("The window's title without the application-name suffix the window manager adds.");
    case Field::IsFloating:
        return PhosphorI18n::tr("Whether the window has been floated out of tiling (snap or autotile).");
    case Field::IsSnapped:
        return PhosphorI18n::tr(
            "Whether the window is snapped into a zone (manual-zone mode, where tiled windows are not snapped).");
    case Field::IsTiled:
        return PhosphorI18n::tr("Whether the window is managed by the autotile engine.");
    case Field::Zone:
        return PhosphorI18n::tr("The zone the window is snapped into (manual-zone mode only).");
    case Field::ScreenId:
        return PhosphorI18n::tr("The monitor the window is on.");
    case Field::VirtualDesktop:
        return PhosphorI18n::tr("The virtual desktop the window is on.");
    case Field::Activity:
        return PhosphorI18n::tr("The KDE Activity the window is on.");
    case Field::Mode:
        return PhosphorI18n::tr("The engine mode the window is placed by (snapping or tiling).");
    case Field::TiledWindowCount:
        return PhosphorI18n::tr(
            "How many windows are tiled on this monitor and desktop. Lets a rule switch the tiling algorithm as "
            "windows open and close, for example a centered single-window layout that gives way once a second window "
            "opens.");
    case Field::ScreenOrientation:
        return PhosphorI18n::tr(
            "Whether the monitor is in portrait or landscape orientation. Lets a rule pick a different layout or "
            "algorithm on a rotated screen.");
    case Field::ActiveLayout:
        return PhosphorI18n::tr(
            "The layout currently active on the monitor. Lets a rule change gaps, the overlay or the lock state for "
            "the screen showing a given layout. It cannot change which layout is assigned (that would be circular).");
    }
    return QString();
}

/// Group an action type into a picker category. Derives from the
/// descriptor's `category` field — adding a new action to an existing
/// category requires zero changes here.
PickerCategory actionCategory(const QString& type)
{
    const auto desc = PhosphorRules::ActionRegistry::instance().descriptor(type);
    if (!desc.has_value()) {
        return {PhosphorI18n::tr("Other"), 99};
    }
    const QString& cat = desc->category;
    // Two groups, alphabetised within each: the context-domain categories
    // (resolved per screen/desktop/activity/mode) come first (orders 0-2), then
    // the window-domain categories (orders 3-5). Keep these orders in lockstep
    // with each category's action domains in RuleAction.cpp.
    if (cat == QLatin1String("gap")) {
        return {PhosphorI18n::tr("Gaps"), 0};
    }
    if (cat == QLatin1String("layoutEngine")) {
        return {PhosphorI18n::tr("Layout & engine"), 1};
    }
    if (cat == QLatin1String("overlay")) {
        return {PhosphorI18n::tr("Overlay"), 2};
    }
    if (cat == QLatin1String("animation")) {
        return {PhosphorI18n::tr("Animation"), 3};
    }
    if (cat == QLatin1String("appearance") || cat == QLatin1String("borderAppearance")) {
        return {PhosphorI18n::tr("Appearance"), 4};
    }
    if (cat == QLatin1String("windowManagement")) {
        return {PhosphorI18n::tr("Window"), 5};
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

QString operatorLabelImpl(Operator op)
{
    switch (op) {
    case Operator::Equals:
        return PhosphorI18n::tr("is");
    case Operator::Contains:
        return PhosphorI18n::tr("contains");
    case Operator::StartsWith:
        return PhosphorI18n::tr("starts with");
    case Operator::EndsWith:
        return PhosphorI18n::tr("ends with");
    case Operator::Regex:
        return PhosphorI18n::tr("matches regex");
    case Operator::AppIdMatches:
        return PhosphorI18n::tr("matches app-id");
    case Operator::GreaterThan:
        return PhosphorI18n::tr("greater than");
    case Operator::LessThan:
        return PhosphorI18n::tr("less than");
    }
    // Wire-string fallback (same convention as paramLabel /
    // actionTypeFallbackLabel): a future operator missing a label entry
    // shows its raw token in the picker instead of a blank row.
    return PhosphorRules::operatorToString(op);
}

/// Single source of truth for WindowType → { int value, wire token, display label },
/// shared by matchFields() (the editor dropdown options) and windowTypeLabel() (the
/// collapsed rule-list summary). Order mirrors WindowTypeEnum.h — Unknown first as
/// the safe default, then Normal as the most common authoring choice.
struct WindowTypeOption
{
    int value;
    QString wire;
    QString label;
};
QList<WindowTypeOption> windowTypeOptions()
{
    struct Entry
    {
        PhosphorProtocol::WindowType type;
        QString label;
    };
    // CTAD deduces the array size from the brace-list, so a new enum value can't
    // silently drop the trailing entry by mismatching a hardcoded size.
    const std::array entries = std::to_array<Entry>({
        {PhosphorProtocol::WindowType::Unknown, PhosphorI18n::tr("Unknown")},
        {PhosphorProtocol::WindowType::Normal, PhosphorI18n::tr("Normal window")},
        {PhosphorProtocol::WindowType::Dialog, PhosphorI18n::tr("Dialog")},
        {PhosphorProtocol::WindowType::Utility, PhosphorI18n::tr("Utility")},
        {PhosphorProtocol::WindowType::Toolbar, PhosphorI18n::tr("Toolbar")},
        {PhosphorProtocol::WindowType::Splash, PhosphorI18n::tr("Splash screen")},
        {PhosphorProtocol::WindowType::Menu, PhosphorI18n::tr("Menu")},
        {PhosphorProtocol::WindowType::Tooltip, PhosphorI18n::tr("Tooltip")},
        {PhosphorProtocol::WindowType::Notification, PhosphorI18n::tr("Notification")},
        {PhosphorProtocol::WindowType::Dock, PhosphorI18n::tr("Dock / panel")},
        {PhosphorProtocol::WindowType::Desktop, PhosphorI18n::tr("Desktop")},
        {PhosphorProtocol::WindowType::OnScreenDisplay, PhosphorI18n::tr("On-screen display")},
        {PhosphorProtocol::WindowType::Popup, PhosphorI18n::tr("Popup")},
    });
    QList<WindowTypeOption> out;
    out.reserve(static_cast<int>(entries.size()));
    for (const auto& e : entries) {
        out.append({static_cast<int>(e.type), PhosphorProtocol::windowTypeToString(e.type), e.label});
    }
    return out;
}

/// Single source for the closed Mode / ScreenOrientation token vocabularies (the
/// stored value IS the wire token), shared by matchFields() (the editor dropdown
/// options) and modeLabel() / orientationLabel() (the collapsed rule-list summary),
/// so the picker and the summary can never drift.
struct ClosedTokenOption
{
    QString wire;
    QString label;
};
QList<ClosedTokenOption> modeOptions()
{
    return {{QStringLiteral("snapping"), PhosphorI18n::tr("Snapping")},
            {QStringLiteral("tiling"), PhosphorI18n::tr("Tiling")}};
}
QList<ClosedTokenOption> orientationOptions()
{
    return {{QStringLiteral("landscape"), PhosphorI18n::tr("Landscape")},
            {QStringLiteral("portrait"), PhosphorI18n::tr("Portrait")}};
}
QString closedTokenLabel(const QList<ClosedTokenOption>& opts, const QString& token)
{
    for (const ClosedTokenOption& o : opts) {
        if (o.wire == token) {
            return o.label;
        }
    }
    // Unknown token (hand-edited rule): round-trip verbatim.
    return token;
}

} // namespace

QString windowTypeLabel(int windowTypeValue)
{
    for (const WindowTypeOption& opt : windowTypeOptions()) {
        if (opt.value == windowTypeValue) {
            return opt.label;
        }
    }
    // Unknown value (hand-edited rule): show the raw int rather than a blank.
    return QString::number(windowTypeValue);
}

QString modeLabel(const QString& modeToken)
{
    return closedTokenLabel(modeOptions(), modeToken);
}

QString orientationLabel(const QString& orientationToken)
{
    return closedTokenLabel(orientationOptions(), orientationToken);
}

QString enumOptionLabel(const QString& type, const QString& key, const QString& wireValue)
{
    namespace ActionParam = PhosphorRules::ActionParam;
    if ((type == ActionType::SetEngineMode || type == ActionType::DisableEngine) && key == ActionParam::Mode) {
        if (wireValue == QLatin1String("snapping")) {
            return PhosphorI18n::tr("Snapping");
        }
        if (wireValue == QLatin1String("autotile")) {
            return PhosphorI18n::tr("Autotile");
        }
        if (wireValue == QLatin1String("scrolling")) {
            return PhosphorI18n::tr("Scrolling");
        }
    }
    if (type == ActionType::OverrideOverlayStyle && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::OverlayStyleToken::Rectangles) {
            return PhosphorI18n::tr("Zone rectangles");
        }
        if (wireValue == PhosphorRules::OverlayStyleToken::Preview) {
            return PhosphorI18n::tr("Layout preview");
        }
    }
    if (type == ActionType::SetInsertPosition && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::InsertPositionToken::End) {
            return PhosphorI18n::tr("End of stack");
        }
        if (wireValue == PhosphorRules::InsertPositionToken::AfterFocused) {
            return PhosphorI18n::tr("After focused window");
        }
        if (wireValue == PhosphorRules::InsertPositionToken::AsMaster) {
            return PhosphorI18n::tr("As master");
        }
    }
    if (type == ActionType::SetOverflowBehavior && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::OverflowBehaviorToken::Float) {
            return PhosphorI18n::tr("Float overflow windows");
        }
        if (wireValue == PhosphorRules::OverflowBehaviorToken::Unlimited) {
            return PhosphorI18n::tr("Unlimited (no cap)");
        }
    }
    if (type == ActionType::SetDragBehavior && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::DragBehaviorToken::Float) {
            return PhosphorI18n::tr("Float on drag");
        }
        if (wireValue == PhosphorRules::DragBehaviorToken::Reorder) {
            return PhosphorI18n::tr("Reorder in stack");
        }
    }
    return wireValue;
}

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
    if (type == ActionType::SetUsePerSideOuterGap) {
        return on ? PhosphorI18n::tr("Per-side outer gaps") : PhosphorI18n::tr("Uniform outer gap");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return on ? PhosphorI18n::tr("Show zone numbers") : PhosphorI18n::tr("Hide zone numbers");
    }
    return QString();
}

QVariantList matchFields()
{
    // Pid and WindowRole are intentionally omitted from the picker —
    // both are footguns in a persistent rule store:
    //   * Pid is ephemeral. A `Pid equals 12345` predicate matches one
    //     specific process instance and is dead the moment that process
    //     restarts. Surfacing it in the picker invites users to author
    //     rules that silently stop working.
    //   * WindowRole is the X11 WM_WINDOW_ROLE property, empty for every
    //     Wayland-native window — PlasmaZones is Wayland-only (per
    //     CLAUDE.md), so the picker would always read as blank.
    // The Field enum keeps both values for back-compat with already-saved
    // rules; only the authoring UI hides them.
    //
    // Iterate the Field enum directly with a deny-set rather than a
    // hand-maintained allow-list: a new Field value (e.g. a hypothetical
    // future `MimeType`) auto-surfaces in the picker unless it's
    // explicitly hidden here. Mirrors the `userAuthorable` filter shape
    // that replaced `kTypes` in actionTypes() below.
    static const QSet<Field> kHiddenFields = {Field::Pid, Field::WindowRole};
    QVariantList out;
    for (int i = 0; i < PhosphorRules::FieldCount; ++i) {
        const auto f = static_cast<Field>(i);
        if (kHiddenFields.contains(f)) {
            continue;
        }
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(f);
        // The JSON wire string for this field — QML keys off this rather than
        // reconstructing the enum↔string table itself.
        entry[QStringLiteral("wire")] = PhosphorRules::fieldToString(f);
        entry[QStringLiteral("label")] = RuleModel::fieldLabel(f);
        const PickerCategory fcat = fieldCategory(f);
        entry[QStringLiteral("category")] = fcat.label;
        entry[QStringLiteral("categoryOrder")] = fcat.order;
        // One-line help surfaced as the leaf editor's info-icon tooltip.
        entry[QStringLiteral("description")] = fieldDescription(f);
        QString kind = QStringLiteral("string");
        if (f == Field::WindowType) {
            // WindowType is stored as the int underlying the
            // PhosphorProtocol::WindowType enum on the wire. A plain "number" SpinBox
            // left users with no idea what each value meant ("2" — Dialog? Utility?),
            // so a dedicated kind renders a dropdown. Options come from the single-source
            // windowTypeOptions() table (also used by the collapsed-summary label).
            kind = QStringLiteral("windowType");
            QVariantList options;
            for (const WindowTypeOption& opt : windowTypeOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.value;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::VirtualDesktop) {
            // Numeric on the wire (a 1-based desktop number), but the QML editor
            // swaps the bare SpinBox for a desktop-picker ComboBox driven by
            // `settingsController.virtualDesktopNames`, so the user picks "2: Work"
            // rather than typing a number. Must precede the generic numeric branch.
            kind = QStringLiteral("virtualDesktop");
        } else if (PhosphorRules::fieldIsNumeric(f)) {
            kind = QStringLiteral("number");
        } else if (PhosphorRules::fieldIsBool(f)) {
            kind = QStringLiteral("bool");
        } else if (f == Field::ScreenId) {
            // QML editor swaps this for a screen-picker ComboBox driven by
            // `settingsController.screens`, so the user sees "LG Ultra HD" not
            // "LG Electronics:LG Ultra HD:115107/vs:0".
            kind = QStringLiteral("screen");
        } else if (f == Field::Activity) {
            // QML editor swaps this for an activity-picker ComboBox driven by
            // `settingsController.activities`, so the user sees the activity
            // name not its UUID.
            kind = QStringLiteral("activity");
        } else if (f == Field::Mode) {
            // Mode is string-valued on the wire (the placement-mode token), but
            // the vocabulary is closed — surface a dropdown of the friendly
            // tokens instead of a free-text box. The `options` carry
            // {value, wire, label}; unlike WindowType the value IS the wire
            // string (Mode is a string field, so the rule store keeps the token
            // verbatim).
            kind = QStringLiteral("mode");
            QVariantList options;
            for (const ClosedTokenOption& opt : modeOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.wire;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::ScreenOrientation) {
            // String-valued on the wire (the orientation token), closed vocabulary
            // — a dropdown of the friendly tokens, same shape as Mode. The value IS
            // the wire token ("portrait" / "landscape"). Options come from the
            // single-source orientationOptions() table (also used by the summary).
            kind = QStringLiteral("orientation");
            QVariantList options;
            for (const ClosedTokenOption& opt : orientationOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.wire;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::ActiveLayout) {
            // The value is a layout id (snap UUID or "autotile:<algo>"). The QML
            // editor swaps this for a layout-picker ComboBox driven by
            // `settingsController.layouts` (like the screen / activity pickers), so
            // the user picks a friendly name while the wire value stays the id.
            kind = QStringLiteral("layout");
        }
        entry[QStringLiteral("valueKind")] = kind;
        out.append(entry);
    }
    return out;
}

QVariantList operatorsForField(int fieldValue)
{
    // Bounded cast: QML hands us a raw int, and an out-of-range value must
    // not reach the Field classifiers (matchFields() bounds the same way).
    if (fieldValue < 0 || fieldValue >= PhosphorRules::FieldCount) {
        return {};
    }
    const Field field = static_cast<Field>(fieldValue);
    QList<Operator> ops;
    if (field == Field::Mode || field == Field::ScreenOrientation || field == Field::ActiveLayout) {
        // These are string-valued but their vocabulary is a closed single-select
        // dropdown (placement mode, portrait/landscape, a concrete layout id) — only
        // an exact-token Equals is meaningful. A substring / regex against a closed
        // token set (or a layout UUID) is a footgun the picker cannot author
        // sensibly. Mirrors the WindowType enum treatment.
        ops = {Operator::Equals};
    } else if (PhosphorRules::fieldIsString(field)) {
        ops = {Operator::Equals, Operator::Contains, Operator::StartsWith, Operator::EndsWith, Operator::Regex};
        if (field == Field::AppId) {
            ops.append(Operator::AppIdMatches);
        }
    } else if (PhosphorRules::fieldIsNumeric(field)) {
        ops = {Operator::Equals, Operator::GreaterThan, Operator::LessThan};
    } else if (PhosphorRules::fieldIsBool(field) || field == Field::WindowType) {
        ops = {Operator::Equals};
    }
    QVariantList out;
    for (Operator op : ops) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(op);
        // The JSON wire string for this operator — same contract as matchFields.
        entry[QStringLiteral("wire")] = PhosphorRules::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabelImpl(op);
        out.append(entry);
    }
    return out;
}

QVariantList allOperators()
{
    // Iterate the whole Operator enum via OperatorCount rather than a
    // hand-maintained list — a new operator auto-surfaces here (and so widens
    // the leaf editor's operator-column sizing) the moment it's added.
    QVariantList out;
    for (int i = 0; i < PhosphorRules::OperatorCount; ++i) {
        const auto op = static_cast<Operator>(i);
        QVariantMap entry;
        entry[QStringLiteral("value")] = i;
        entry[QStringLiteral("wire")] = PhosphorRules::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabelImpl(op);
        out.append(entry);
    }
    return out;
}

QString matchValueHint(const QString& op)
{
    // Keyed on the operator wire token: only the operators whose value editor is
    // a plain text box AND whose accepted syntax / matching semantics aren't
    // obvious get a hint. equals / contains / starts-with / ends-with are
    // self-explanatory; the picker / spin-box operators have no free-text field
    // to annotate. The match-side counterpart to the action-side paramHint.
    if (op == PhosphorRules::operatorToString(Operator::Regex)) {
        return PhosphorI18n::tr("Regular expression, e.g. ^(firefox|chromium)$");
    }
    if (op == PhosphorRules::operatorToString(Operator::AppIdMatches)) {
        return PhosphorI18n::tr("Matches by reverse-DNS segments, so “firefox” also matches “org.mozilla.firefox”.");
    }
    return {};
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
            const bool isBorderColor = typeWire == QString(PhosphorRules::ActionType::SetBorderColorActive)
                || typeWire == QString(PhosphorRules::ActionType::SetBorderColorInactive);
            payload[key] =
                isBorderColor ? QString(PhosphorRules::BorderColorToken::Accent) : QStringLiteral("#FF3DAEE9");
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
