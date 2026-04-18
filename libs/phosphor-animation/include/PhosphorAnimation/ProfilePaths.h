// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

/**
 * @brief Named constants for well-known animation event paths.
 *
 * The ProfileTree uses **dot-path strings** (not an enum) so plugins and
 * shell extensions can introduce new event categories without requiring
 * a library change. This header supplies the default taxonomy used by
 * PlasmaZones + any PhosphorAnimation consumer that wants to hit the
 * same well-known paths.
 *
 * ## Taxonomy
 *
 *   global
 *     ├── window             ┐ lifecycle + geometry for individual windows
 *     │   ├── window.open
 *     │   ├── window.close
 *     │   ├── window.minimize
 *     │   ├── window.unminimize
 *     │   ├── window.maximize
 *     │   ├── window.unmaximize
 *     │   ├── window.move
 *     │   ├── window.resize
 *     │   └── window.focus
 *     │
 *     ├── zone               ┐ zone / tiling animations (PlasmaZones-native)
 *     │   ├── zone.snapIn
 *     │   ├── zone.snapOut
 *     │   ├── zone.snapResize
 *     │   ├── zone.highlight
 *     │   ├── zone.layoutSwitchIn
 *     │   └── zone.layoutSwitchOut
 *     │
 *     ├── workspace          ┐ virtual desktop / activity switching
 *     │   ├── workspace.switchIn
 *     │   ├── workspace.switchOut
 *     │   └── workspace.overview
 *     │
 *     ├── osd                ┐ on-screen display, feedback
 *     │   ├── osd.show
 *     │   ├── osd.hide
 *     │   └── osd.dim
 *     │
 *     ├── panel              ┐ docks, bars, notifications
 *     │   ├── panel.slideIn
 *     │   ├── panel.slideOut
 *     │   └── panel.popup
 *     │
 *     ├── cursor             ┐ pointer / input feedback
 *     │   ├── cursor.hover
 *     │   ├── cursor.click
 *     │   └── cursor.drag
 *     │
 *     └── shader             ┐ shader-driven composite transitions
 *         ├── shader.open
 *         ├── shader.close
 *         └── shader.switch
 *
 * ## Inheritance
 *
 * ProfileTree::resolve() walks segments right-to-left: a request for
 * `window.open` checks `window.open`, then `window`, then `global`, and
 * returns the first explicit override (or a library default).
 *
 * ## Extension
 *
 * Plugins add their own paths freely — e.g., a shell extension for
 * notifications might use `widget.toast.slideIn`, falling back through
 * `widget.toast`, `widget`, `global`. No library change required.
 */
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
PHOSPHORANIMATION_EXPORT extern const QString OsdDim;

// panel.*
PHOSPHORANIMATION_EXPORT extern const QString Panel;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideOut;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopup;

// cursor.*
PHOSPHORANIMATION_EXPORT extern const QString Cursor;
PHOSPHORANIMATION_EXPORT extern const QString CursorHover;
PHOSPHORANIMATION_EXPORT extern const QString CursorClick;
PHOSPHORANIMATION_EXPORT extern const QString CursorDrag;

// shader.*
PHOSPHORANIMATION_EXPORT extern const QString Shader;
PHOSPHORANIMATION_EXPORT extern const QString ShaderOpen;
PHOSPHORANIMATION_EXPORT extern const QString ShaderClose;
PHOSPHORANIMATION_EXPORT extern const QString ShaderSwitch;

/**
 * @brief Full list of built-in paths, in taxonomy order.
 *
 * Useful for settings UIs that want to enumerate every recognized event
 * for per-event override editing. Extension paths added at runtime via
 * ProfileTree::setOverride() are NOT included here — enumerate the tree
 * itself for those.
 */
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/**
 * @brief Walk @p path up one level.
 *
 * Examples:
 *   - `"window.open"`  → `"window"`
 *   - `"window"`       → `"global"`
 *   - `"global"`       → `""` (empty string — no parent)
 *   - `""`             → `""`
 *
 * Used by ProfileTree::resolve() to implement inheritance; exposed here
 * because shell extensions sometimes need to construct inheritance
 * chains themselves (e.g., for settings-UI path pickers).
 */
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
