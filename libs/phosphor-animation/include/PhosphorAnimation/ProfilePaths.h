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
namespace ProfilePaths {

// Root
PHOSPHORANIMATION_EXPORT extern const QString Global;

// window.*
PHOSPHORANIMATION_EXPORT extern const QString Window;
PHOSPHORANIMATION_EXPORT extern const QString WindowOpen;
PHOSPHORANIMATION_EXPORT extern const QString WindowClose;
PHOSPHORANIMATION_EXPORT extern const QString WindowMinimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowUnminimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowUnmaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMove;
PHOSPHORANIMATION_EXPORT extern const QString WindowResize;
PHOSPHORANIMATION_EXPORT extern const QString WindowFocus;

// zone.*
PHOSPHORANIMATION_EXPORT extern const QString Zone;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapResize;
PHOSPHORANIMATION_EXPORT extern const QString ZoneHighlight;
PHOSPHORANIMATION_EXPORT extern const QString ZoneLayoutSwitchIn;
PHOSPHORANIMATION_EXPORT extern const QString ZoneLayoutSwitchOut;

// workspace.*
PHOSPHORANIMATION_EXPORT extern const QString Workspace;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceSwitchIn;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceSwitchOut;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceOverview;

// osd.*
PHOSPHORANIMATION_EXPORT extern const QString Osd;
PHOSPHORANIMATION_EXPORT extern const QString OsdShow;
PHOSPHORANIMATION_EXPORT extern const QString OsdHide;
PHOSPHORANIMATION_EXPORT extern const QString OsdPop;
PHOSPHORANIMATION_EXPORT extern const QString OsdDim;

// panel.*
PHOSPHORANIMATION_EXPORT extern const QString Panel;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideOut;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopup;
// Per-leg .show/.hide leaves let show/hide shader effects diverge.
// SnapAssist has .show only because the surface destroys-on-hide.
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelector;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelectorShow;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelectorHide;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPicker;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerShow;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerHide;
/// Scale-leg show for LayoutPicker — the only non-OSD surface with a scale envelope.
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerPopIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupSnapAssist;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupSnapAssistShow;

// cursor.*
// CursorDrag is reserved — no built-in renderer. A window-content shader at
// drag start just shadows window.move (last-event-wins on the same trigger).
// Reserved for a future cursor-decoration / drag-shadow surface.
PHOSPHORANIMATION_EXPORT extern const QString Cursor;
PHOSPHORANIMATION_EXPORT extern const QString CursorHover;
PHOSPHORANIMATION_EXPORT extern const QString CursorClick;
PHOSPHORANIMATION_EXPORT extern const QString CursorDrag;

// shader.*
PHOSPHORANIMATION_EXPORT extern const QString Shader;
PHOSPHORANIMATION_EXPORT extern const QString ShaderOpen;
PHOSPHORANIMATION_EXPORT extern const QString ShaderClose;
PHOSPHORANIMATION_EXPORT extern const QString ShaderSwitch;

// widget.* — per-archetype paths so library defaults preserve original motion.
PHOSPHORANIMATION_EXPORT extern const QString Widget;
PHOSPHORANIMATION_EXPORT extern const QString WidgetHover; ///< 150 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetPress; ///< 100 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggle; ///< 250 ms OutBack (spring feel)
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadge; ///< 200 ms OutBack (overshoot)
PHOSPHORANIMATION_EXPORT extern const QString WidgetTint; ///< 300 ms Linear
PHOSPHORANIMATION_EXPORT extern const QString WidgetDim; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetFade; ///< 150 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetReorder; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordion; ///< 250 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetProgress; ///< 200 ms OutCubic

/// Full list of built-in paths in taxonomy order (excludes reserved paths).
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/// True when @p path is in the taxonomy but has no built-in producer.
/// Currently reserved: cursor.drag, zone.layoutSwitchOut.
PHOSPHORANIMATION_EXPORT bool isReservedPath(const QString& path);

/// Walk @p path up one level ("window.open" -> "window" -> "global" -> "").
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
