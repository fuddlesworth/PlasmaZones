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

// window.* — runtime window-lifecycle animations driven by the
// kwin-effect's OffscreenEffect via tryBeginShaderForEvent. The
// snap/layout-switch leaves are window events triggered by zone
// interaction (the WINDOW animates when it snaps into/out of a zone
// or when a layout switch repositions it).
PHOSPHORANIMATION_EXPORT extern const QString Window;
PHOSPHORANIMATION_EXPORT extern const QString WindowOpen;
PHOSPHORANIMATION_EXPORT extern const QString WindowClose;
PHOSPHORANIMATION_EXPORT extern const QString WindowMinimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMove;
PHOSPHORANIMATION_EXPORT extern const QString WindowResize;
PHOSPHORANIMATION_EXPORT extern const QString WindowFocus;
PHOSPHORANIMATION_EXPORT extern const QString WindowSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString WindowSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString WindowSnapResize;
PHOSPHORANIMATION_EXPORT extern const QString WindowLayoutSwitch;
/// Scroll-mode strip translation — the whole viewport pans as the focused
/// column changes. Distinct from snapIn (a window dropping into a zone) so
/// the strip-scroll feel can be tuned independently.
PHOSPHORANIMATION_EXPORT extern const QString WindowScroll;

// editor.* — Layout-editor-only zone manipulation animations
// (fill-preview, drag-resize-preview). NOT triggered by runtime
// window snapping — window-snap animations are KWin's
// compositor-level domain. These paths only fire inside the
// PlasmaZones layout editor.
PHOSPHORANIMATION_EXPORT extern const QString Editor;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapResize;

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
// Zone-rect widget (used by ZoneItem.qml, LayoutPreview.qml,
// ZonePreview.qml — i.e. the reusable QML zone-rectangle that gets
// embedded in the runtime overlay, settings dialogs, layout
// thumbnails, etc.). The animation lives with the widget; the
// surface it's hosted on is incidental.
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlight;
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlightPop;
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlightBorder;
// One-shot flash on the main zone-overlay surface when the active
// layout changes mid-drag (ZoneOverlayContent.qml). A widget-level
// content effect on the overlay, not a per-zone animation.
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneOverlayFlash;

/// Full list of built-in paths in taxonomy order.
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/// Walk @p path up one level ("window.open" -> "window" -> "global" -> "").
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
