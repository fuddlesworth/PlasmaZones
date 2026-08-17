// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The per-PARAMETER half of the action-authoring surface: the translated label
// and input hint for each `(action type, param key)` pair, the schema the QML
// editor's per-param Loader dispatches on, and the default-seeded payload a
// freshly-added or type-switched action starts from.
//
// Own translation unit rather than ruleauthoring_actions.cpp: that file owns
// the picker side (category dispatch, type labels, the assembled type list) and
// the two ladders together push it past the size ceiling. The seam is the same
// one the action-description ladder was split along.

#include "ruleauthoring.h"
#include "ruleauthoring_p.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/RuleAction.h>

#include <QLatin1StringView>

namespace PlasmaZones::RuleAuthoring {

namespace {

namespace ActionType = PhosphorRules::ActionType;

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
    if (type == ActionType::SetScrollingTemplate && key == ActionParam::LayoutId) {
        return PhosphorI18n::tr("Template layout");
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
    // Scrolling-engine params (all single-value, keyed ActionParam::Value). The
    // two width params share a label because the action label already says which
    // one is the context default and which is the per-window open override.
    if ((type == ActionType::SetScrollDefaultColumnWidth || type == ActionType::OpenColumnWidth)
        && key == ActionParam::Value) {
        return PhosphorI18n::tr("Column width (%)");
    }
    if (type == ActionType::SetCenterFocusedColumn && key == ActionParam::Value) {
        return PhosphorI18n::tr("Centering");
    }
    if (type == ActionType::SetScrollDefaultColumnDisplay && key == ActionParam::Value) {
        return PhosphorI18n::tr("Display");
    }
    if (type == ActionType::SetScrollInsertPosition && key == ActionParam::Value) {
        return PhosphorI18n::tr("New column position");
    }
    // The two height params share a label for the same reason the widths do.
    if ((type == ActionType::SetScrollDefaultWindowHeight || type == ActionType::OpenWindowHeight)
        && key == ActionParam::Value) {
        return PhosphorI18n::tr("Window height (%)");
    }
    if (type == ActionType::OpenTabbed && key == ActionParam::Value) {
        return PhosphorI18n::tr("Open in a tabbed column (off = a normal column)");
    }
    if (type == ActionType::OpenMaximized && key == ActionParam::Value) {
        return PhosphorI18n::tr("Open maximized (off = open at the default width)");
    }
    if (type == ActionType::SetScrollStickyWindowHandling && key == ActionParam::Value) {
        return PhosphorI18n::tr("Handling");
    }
    if (type == ActionType::SetScrollStripAxis && key == ActionParam::Value) {
        return PhosphorI18n::tr("Direction");
    }
    if (type == ActionType::OpenFocused && key == ActionParam::Value) {
        return PhosphorI18n::tr("Focus the window when it opens (off = keep the current focus)");
    }
    if (type == ActionType::OpenFullscreen && key == ActionParam::Value) {
        return PhosphorI18n::tr("Open in fullscreen (off = block the app's own fullscreen at open)");
    }
    if (type == ActionType::OpenColumnPlacement && key == ActionParam::Value) {
        return PhosphorI18n::tr("Placement");
    }
    // The six scrolling behaviour toggles. Each label names the ON outcome and
    // spells out the off one, the same shape the other bool params use: these
    // override a config value, so the off position is an instruction rather
    // than an absence. Without an entry the editor falls back to the wire key,
    // which is what Accessible.name would announce.
    if (type == ActionType::SetScrollAlwaysCenterSingleColumn && key == ActionParam::Value) {
        return PhosphorI18n::tr("Center a lone column (off = leave it where it sits)");
    }
    if (type == ActionType::SetScrollRespectMinimumSize && key == ActionParam::Value) {
        return PhosphorI18n::tr("Respect minimum window sizes (off = let columns go narrower)");
    }
    if (type == ActionType::SetScrollCropStraddlers && key == ActionParam::Value) {
        return PhosphorI18n::tr("Crop columns at the screen edge (off = keep them whole)");
    }
    if (type == ActionType::SetScrollFocusNewWindows && key == ActionParam::Value) {
        return PhosphorI18n::tr("Focus new windows (off = keep focus where it is)");
    }
    if (type == ActionType::SetScrollSmartGaps && key == ActionParam::Value) {
        return PhosphorI18n::tr("Drop the outer gaps for a lone column (off = keep them)");
    }
    if (type == ActionType::SetScrollFocusFollowsMouse && key == ActionParam::Value) {
        return PhosphorI18n::tr("Focus follows the mouse (off = focus stays until you click)");
    }
    // Tab indicator. The colour actions all share one label: the action label
    // already names which colour, so repeating it here would read twice.
    if ((type == ActionType::SetTabIndicatorActiveColor || type == ActionType::SetTabIndicatorInactiveColor
         || type == ActionType::SetTabIndicatorUrgentColor || type == ActionType::TabColorActive
         || type == ActionType::TabColorInactive || type == ActionType::TabColorUrgent)
        && key == ActionParam::Value) {
        return PhosphorI18n::tr("Color");
    }
    if (type == ActionType::SetTabIndicatorEnabled && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show the indicator over tabbed columns");
    }
    if (type == ActionType::SetTabIndicatorStyle && key == ActionParam::Value) {
        return PhosphorI18n::tr("Style");
    }
    if (type == ActionType::SetTabIndicatorPosition && key == ActionParam::Value) {
        return PhosphorI18n::tr("Position");
    }
    if (type == ActionType::SetTabIndicatorHideWhenSingleTab && key == ActionParam::Value) {
        return PhosphorI18n::tr("Hide it when the column holds one window");
    }
    if (type == ActionType::SetTabIndicatorPlaceWithinColumn && key == ActionParam::Value) {
        return PhosphorI18n::tr("Make room for it inside the column");
    }
    if (type == ActionType::SetTabIndicatorGap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Gap (px, negative draws over the window)");
    }
    if (type == ActionType::SetTabIndicatorWidth && key == ActionParam::Value) {
        return PhosphorI18n::tr("Thickness (px)");
    }
    if (type == ActionType::SetTabIndicatorLength && key == ActionParam::Value) {
        return PhosphorI18n::tr("Length (%)");
    }
    if (type == ActionType::SetTabIndicatorGapsBetweenTabs && key == ActionParam::Value) {
        return PhosphorI18n::tr("Gap between tabs (px)");
    }
    if (type == ActionType::SetTabIndicatorCornerRadius && key == ActionParam::Value) {
        return PhosphorI18n::tr("Corner radius (px, -1 is fully rounded)");
    }
    // Drop indicator. The two colour pairs share one label for the same reason
    // the tab colours do: the action label already names which colour it is.
    if ((type == ActionType::SetDropIndicatorColor || type == ActionType::SetDropIndicatorBorderColor
         || type == ActionType::DropIndicatorColor || type == ActionType::DropIndicatorBorderColor)
        && key == ActionParam::Value) {
        return PhosphorI18n::tr("Color");
    }
    if (type == ActionType::SetDropIndicatorEnabled && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show the indicator while dragging");
    }
    if (type == ActionType::SetDropIndicatorOpacity && key == ActionParam::Value) {
        return PhosphorI18n::tr("Fill opacity (%)");
    }
    if (type == ActionType::SetDropIndicatorBorderWidth && key == ActionParam::Value) {
        return PhosphorI18n::tr("Border width (px)");
    }
    if (type == ActionType::SetDropIndicatorBorderRadius && key == ActionParam::Value) {
        return PhosphorI18n::tr("Corner radius (px)");
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
        return PhosphorI18n::tr("Return the window to its previous zone when it reopens (off = don't restore)");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap && key == ActionParam::Value) {
        return PhosphorI18n::tr("Restore size on unsnap (off = keep zone size)");
    }
    if (type == ActionType::SetUnfloatFallbackToZone && key == ActionParam::Value) {
        return PhosphorI18n::tr("Place in a zone when no zone is remembered (off = stay floating)");
    }
    if (type == ActionType::SetWindowLayer && key == ActionParam::Value) {
        return PhosphorI18n::tr("Layer");
    }
    if (type == ActionType::ScrollFactor && key == ActionParam::Value) {
        // The descriptor is `percent` with scale 0.01, so the editor shows 75
        // where the wire holds 0.75. The label has to name the neutral point in
        // the units the user actually types, or "below 1" reads as an
        // unreachable value on a spin box whose floor is 5.
        return PhosphorI18n::tr("Scroll speed (below 100% is slower, above 100% is faster)");
    }
    if (type == ActionType::SetOsdEnabled && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show on-screen displays here (off = hide them)");
    }
    if (type == ActionType::SetDragSelectorEnabled && key == ActionParam::Value) {
        return PhosphorI18n::tr("Show the drag selector here (off = hide it)");
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

/// Whether @p typeWire's colour param accepts the "accent" sentinel
/// (PhosphorRules::BorderColorToken::Accent) — the trio whose validator is
/// hasHexColorOrAccent and whose consumer resolves the token. The SINGLE
/// source for this set: defaultPayloadFor seeds the sentinel from it, and
/// paramsForActionType publishes it to QML as `acceptsAccent` so the
/// editor's Reset affordance and ActionRow's type-switch value migration
/// read the same truth instead of each hardcoding the type ladder.
bool actionAcceptsAccent(const QString& typeWire)
{
    return typeWire == QString(PhosphorRules::ActionType::SetBorderColorActive)
        || typeWire == QString(PhosphorRules::ActionType::SetBorderColorInactive)
        || typeWire == QString(PhosphorRules::ActionType::SetTintColor);
}

} // namespace

QVariantList paramsForActionType(const QString& type)
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
        if (schema.kind == QLatin1String("color")) {
            p[QStringLiteral("acceptsAccent")] = actionAcceptsAccent(type);
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

    const QVariantList params = paramsForActionType(typeWire);
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
            payload[key] = actionAcceptsAccent(typeWire) ? QString(PhosphorRules::BorderColorToken::Accent)
                                                         : QStringLiteral("#FF3DAEE9");
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
            // Picker kinds (snappingLayout, scrollingTemplate, tilingAlgorithm,
            // animationEvent, shaderEffect, overlayShader, curveEditor,
            // screenId) and plain
            // strings all start empty (zoneOrdinals and virtualDesktop are
            // seeded above because their validators reject an empty value). The
            // user has to choose a value before the rule is savable, and
            // `canSave` surfaces the gap explicitly. Seeding a placeholder here
            // would mask the "user has to pick" state.
            payload[key] = QString();
        }
    }
    return payload;
}

} // namespace PlasmaZones::RuleAuthoring
