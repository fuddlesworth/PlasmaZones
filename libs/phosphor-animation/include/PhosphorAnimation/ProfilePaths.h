// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

/// Dot-path constants for well-known animation events.
/// ProfileTree::resolve() walks segments right-to-left for inheritance.
/// Plugins add paths freely (e.g. "widget.toast.slideIn") without library changes.
///
/// Naming convention (apply to new paths):
///   show / hide                 — ephemeral surfaces (osd, popup, badge)
///   open / close                — persistent surfaces with a stateful open/closed
///   <verb>In / <verb>Out        — directional motion (slideIn, snapIn, switchIn,
///                                 fadeIn, layoutSwitchIn …)
///   expand / collapse           — size reveal of inline content (accordion)
///   on / off                    — bistable controls (toggle)
///   <event>.<variant>           — speed/intensity variants (pulse.fast, tint.fast)
namespace ProfilePaths {

// Root
PHOSPHORANIMATION_EXPORT extern const QString Global;

// window.*
PHOSPHORANIMATION_EXPORT extern const QString Window;
PHOSPHORANIMATION_EXPORT extern const QString WindowOpen;
PHOSPHORANIMATION_EXPORT extern const QString WindowClose;
PHOSPHORANIMATION_EXPORT extern const QString WindowMinimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMove;
PHOSPHORANIMATION_EXPORT extern const QString WindowResize;
PHOSPHORANIMATION_EXPORT extern const QString WindowFocus;

// zone.*
PHOSPHORANIMATION_EXPORT extern const QString Zone;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapResize;
PHOSPHORANIMATION_EXPORT extern const QString ZoneHighlight;
PHOSPHORANIMATION_EXPORT extern const QString ZoneHighlightPop;
PHOSPHORANIMATION_EXPORT extern const QString ZoneHighlightBorder;
PHOSPHORANIMATION_EXPORT extern const QString ZoneLayoutSwitchIn;
// `zone.layoutSwitchOut` is intentionally absent — the layout-switch
// flash (`ZoneOverlay.qml`) is a one-shot fade that has no out-leg
// surface. If a future consumer needs an out-leg shape, add the
// constant in lockstep with the consumer.

// osd.*
PHOSPHORANIMATION_EXPORT extern const QString Osd;
PHOSPHORANIMATION_EXPORT extern const QString OsdShow;
PHOSPHORANIMATION_EXPORT extern const QString OsdPop;
PHOSPHORANIMATION_EXPORT extern const QString OsdHide;

// popup.* — transient overlays invoked by user action.
// Per-leg .show/.hide leaves let show/hide shader effects diverge.
PHOSPHORANIMATION_EXPORT extern const QString Popup;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelector;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelectorShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelectorHide;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPicker;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPickerShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPickerHide;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssist;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssistShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssistHide;

// panel.* — persistent in-app side surfaces (settings nav rail, editor
// property panel). Absorbs the former sidebar.* root — sidebars are panels.
PHOSPHORANIMATION_EXPORT extern const QString Panel;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideOut;
PHOSPHORANIMATION_EXPORT extern const QString PanelFadeIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelFadeOut;

// cursor.*
PHOSPHORANIMATION_EXPORT extern const QString Cursor;
PHOSPHORANIMATION_EXPORT extern const QString CursorHover;
PHOSPHORANIMATION_EXPORT extern const QString CursorClick;

// shader.*
PHOSPHORANIMATION_EXPORT extern const QString Shader;
PHOSPHORANIMATION_EXPORT extern const QString ShaderOpen;
PHOSPHORANIMATION_EXPORT extern const QString ShaderClose;
PHOSPHORANIMATION_EXPORT extern const QString ShaderSwitch;

// widget.* — per-archetype paths so library defaults preserve original motion.
PHOSPHORANIMATION_EXPORT extern const QString Widget;
PHOSPHORANIMATION_EXPORT extern const QString WidgetHover; ///< 150 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetPress; ///< 100 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetDim; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetTint; ///< 300 ms Linear (family root)
PHOSPHORANIMATION_EXPORT extern const QString WidgetTintFast; ///< 120 ms (variant)
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggleOn; ///< 250 ms OutBack (spring feel)
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggleOff; ///< 250 ms OutBack
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgeShow; ///< 200 ms OutBack
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgeHide; ///< 150 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgePulse; ///< 400 ms count-change pulse
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordionExpand; ///< 250 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordionCollapse; ///< 180 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetFadeIn; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetFadeOut; ///< 400 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetReorder; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetProgress; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulse; ///< 1000 ms sinusoidal (family root)
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulseFast; ///< 500 ms
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulseSlow; ///< 1500 ms

/// Full list of built-in paths in taxonomy order.
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/// Walk @p path up one level ("window.open" -> "window" -> "global" -> "").
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
