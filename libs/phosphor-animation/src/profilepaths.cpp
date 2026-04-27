// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfilePaths.h>

namespace PhosphorAnimation {
namespace ProfilePaths {

// Root
const QString Global = QStringLiteral("global");

// window.*
const QString Window = QStringLiteral("window");
const QString WindowOpen = QStringLiteral("window.open");
const QString WindowClose = QStringLiteral("window.close");
const QString WindowMinimize = QStringLiteral("window.minimize");
const QString WindowUnminimize = QStringLiteral("window.unminimize");
const QString WindowMaximize = QStringLiteral("window.maximize");
const QString WindowUnmaximize = QStringLiteral("window.unmaximize");
const QString WindowMove = QStringLiteral("window.move");
const QString WindowResize = QStringLiteral("window.resize");
const QString WindowFocus = QStringLiteral("window.focus");

// zone.*
const QString Zone = QStringLiteral("zone");
const QString ZoneSnapIn = QStringLiteral("zone.snapIn");
const QString ZoneSnapOut = QStringLiteral("zone.snapOut");
const QString ZoneSnapResize = QStringLiteral("zone.snapResize");
const QString ZoneHighlight = QStringLiteral("zone.highlight");
const QString ZoneLayoutSwitchIn = QStringLiteral("zone.layoutSwitchIn");
const QString ZoneLayoutSwitchOut = QStringLiteral("zone.layoutSwitchOut");

// workspace.*
const QString Workspace = QStringLiteral("workspace");
const QString WorkspaceSwitchIn = QStringLiteral("workspace.switchIn");
const QString WorkspaceSwitchOut = QStringLiteral("workspace.switchOut");
const QString WorkspaceOverview = QStringLiteral("workspace.overview");

// osd.*
const QString Osd = QStringLiteral("osd");
const QString OsdShow = QStringLiteral("osd.show");
const QString OsdHide = QStringLiteral("osd.hide");
const QString OsdDim = QStringLiteral("osd.dim");

// panel.*
const QString Panel = QStringLiteral("panel");
const QString PanelSlideIn = QStringLiteral("panel.slideIn");
const QString PanelSlideOut = QStringLiteral("panel.slideOut");
const QString PanelPopup = QStringLiteral("panel.popup");

// cursor.*
const QString Cursor = QStringLiteral("cursor");
const QString CursorHover = QStringLiteral("cursor.hover");
const QString CursorClick = QStringLiteral("cursor.click");
const QString CursorDrag = QStringLiteral("cursor.drag");

// shader.*
const QString Shader = QStringLiteral("shader");
const QString ShaderOpen = QStringLiteral("shader.open");
const QString ShaderClose = QStringLiteral("shader.close");
const QString ShaderSwitch = QStringLiteral("shader.switch");

// widget.*
const QString Widget = QStringLiteral("widget");
const QString WidgetHover = QStringLiteral("widget.hover");
const QString WidgetPress = QStringLiteral("widget.press");
const QString WidgetToggle = QStringLiteral("widget.toggle");
const QString WidgetBadge = QStringLiteral("widget.badge");
const QString WidgetTint = QStringLiteral("widget.tint");
const QString WidgetDim = QStringLiteral("widget.dim");
const QString WidgetFade = QStringLiteral("widget.fade");
const QString WidgetReorder = QStringLiteral("widget.reorder");
const QString WidgetAccordion = QStringLiteral("widget.accordion");
const QString WidgetProgress = QStringLiteral("widget.progress");

QStringList allBuiltInPaths()
{
    // Ordered to match taxonomy tree walk — category root, then leaves.
    // Useful as-is for settings UI that renders an indented list.
    return {
        Global,
        Window,
        WindowOpen,
        WindowClose,
        WindowMinimize,
        WindowUnminimize,
        WindowMaximize,
        WindowUnmaximize,
        WindowMove,
        WindowResize,
        WindowFocus,
        Zone,
        ZoneSnapIn,
        ZoneSnapOut,
        ZoneSnapResize,
        ZoneHighlight,
        ZoneLayoutSwitchIn,
        ZoneLayoutSwitchOut,
        Workspace,
        WorkspaceSwitchIn,
        WorkspaceSwitchOut,
        WorkspaceOverview,
        Osd,
        OsdShow,
        OsdHide,
        OsdDim,
        Panel,
        PanelSlideIn,
        PanelSlideOut,
        PanelPopup,
        Cursor,
        CursorHover,
        CursorClick,
        CursorDrag,
        Shader,
        ShaderOpen,
        ShaderClose,
        ShaderSwitch,
        Widget,
        WidgetHover,
        WidgetPress,
        WidgetToggle,
        WidgetBadge,
        WidgetTint,
        WidgetDim,
        WidgetFade,
        WidgetReorder,
        WidgetAccordion,
        WidgetProgress,
    };
}

QString parentPath(const QString& path)
{
    if (path.isEmpty() || path == Global) {
        return QString();
    }
    const int dotIdx = path.lastIndexOf(QLatin1Char('.'));
    if (dotIdx < 0) {
        // Category root ("window", "zone", …) → Global.
        return Global;
    }
    return path.left(dotIdx);
}

} // namespace ProfilePaths
} // namespace PhosphorAnimation
