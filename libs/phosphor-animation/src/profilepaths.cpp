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
const QString WindowMaximize = QStringLiteral("window.maximize");
const QString WindowMove = QStringLiteral("window.move");
const QString WindowResize = QStringLiteral("window.resize");
const QString WindowFocus = QStringLiteral("window.focus");

// zone.*
const QString Zone = QStringLiteral("zone");
const QString ZoneSnapIn = QStringLiteral("zone.snapIn");
const QString ZoneSnapOut = QStringLiteral("zone.snapOut");
const QString ZoneSnapResize = QStringLiteral("zone.snapResize");
const QString ZoneHighlight = QStringLiteral("zone.highlight");
const QString ZoneHighlightPop = QStringLiteral("zone.highlight.pop");
const QString ZoneHighlightBorder = QStringLiteral("zone.highlight.border");
const QString ZoneLayoutSwitchIn = QStringLiteral("zone.layoutSwitchIn");
// `zone.layoutSwitchOut` intentionally undeclared — see ProfilePaths.h.

// workspace.*
const QString Workspace = QStringLiteral("workspace");
const QString WorkspaceSwitchIn = QStringLiteral("workspace.switchIn");
const QString WorkspaceSwitchOut = QStringLiteral("workspace.switchOut");
const QString WorkspaceOverviewOpen = QStringLiteral("workspace.overviewOpen");
const QString WorkspaceOverviewClose = QStringLiteral("workspace.overviewClose");

// osd.*
const QString Osd = QStringLiteral("osd");
const QString OsdShow = QStringLiteral("osd.show");
const QString OsdPop = QStringLiteral("osd.pop");
const QString OsdHide = QStringLiteral("osd.hide");

// popup.*
const QString Popup = QStringLiteral("popup");
const QString PopupZoneSelector = QStringLiteral("popup.zoneSelector");
const QString PopupZoneSelectorShow = QStringLiteral("popup.zoneSelector.show");
const QString PopupZoneSelectorHide = QStringLiteral("popup.zoneSelector.hide");
const QString PopupLayoutPicker = QStringLiteral("popup.layoutPicker");
const QString PopupLayoutPickerShow = QStringLiteral("popup.layoutPicker.show");
const QString PopupLayoutPickerHide = QStringLiteral("popup.layoutPicker.hide");
const QString PopupSnapAssist = QStringLiteral("popup.snapAssist");
const QString PopupSnapAssistShow = QStringLiteral("popup.snapAssist.show");
const QString PopupSnapAssistHide = QStringLiteral("popup.snapAssist.hide");

// panel.*
const QString Panel = QStringLiteral("panel");
const QString PanelSlideIn = QStringLiteral("panel.slideIn");
const QString PanelSlideOut = QStringLiteral("panel.slideOut");
const QString PanelFadeIn = QStringLiteral("panel.fadeIn");
const QString PanelFadeOut = QStringLiteral("panel.fadeOut");

// cursor.*
const QString Cursor = QStringLiteral("cursor");
const QString CursorHover = QStringLiteral("cursor.hover");
const QString CursorClick = QStringLiteral("cursor.click");

// shader.*
const QString Shader = QStringLiteral("shader");
const QString ShaderOpen = QStringLiteral("shader.open");
const QString ShaderClose = QStringLiteral("shader.close");
const QString ShaderSwitch = QStringLiteral("shader.switch");

// widget.*
const QString Widget = QStringLiteral("widget");
const QString WidgetHover = QStringLiteral("widget.hover");
const QString WidgetPress = QStringLiteral("widget.press");
const QString WidgetDim = QStringLiteral("widget.dim");
const QString WidgetTint = QStringLiteral("widget.tint");
const QString WidgetTintFast = QStringLiteral("widget.tint.fast");
const QString WidgetToggleOn = QStringLiteral("widget.toggleOn");
const QString WidgetToggleOff = QStringLiteral("widget.toggleOff");
const QString WidgetBadgeShow = QStringLiteral("widget.badgeShow");
const QString WidgetBadgeHide = QStringLiteral("widget.badgeHide");
const QString WidgetBadgePulse = QStringLiteral("widget.badgePulse");
const QString WidgetAccordionExpand = QStringLiteral("widget.accordionExpand");
const QString WidgetAccordionCollapse = QStringLiteral("widget.accordionCollapse");
const QString WidgetFadeIn = QStringLiteral("widget.fadeIn");
const QString WidgetFadeOut = QStringLiteral("widget.fadeOut");
const QString WidgetReorder = QStringLiteral("widget.reorder");
const QString WidgetProgress = QStringLiteral("widget.progress");
const QString WidgetPulse = QStringLiteral("widget.pulse");
const QString WidgetPulseFast = QStringLiteral("widget.pulse.fast");
const QString WidgetPulseSlow = QStringLiteral("widget.pulse.slow");

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
        WindowMaximize,
        WindowMove,
        WindowResize,
        WindowFocus,
        Zone,
        ZoneSnapIn,
        ZoneSnapOut,
        ZoneSnapResize,
        ZoneHighlight,
        ZoneHighlightPop,
        ZoneHighlightBorder,
        ZoneLayoutSwitchIn,
        Workspace,
        WorkspaceSwitchIn,
        WorkspaceSwitchOut,
        WorkspaceOverviewOpen,
        WorkspaceOverviewClose,
        Osd,
        OsdShow,
        OsdPop,
        OsdHide,
        Popup,
        PopupZoneSelector,
        PopupZoneSelectorShow,
        PopupZoneSelectorHide,
        PopupLayoutPicker,
        PopupLayoutPickerShow,
        PopupLayoutPickerHide,
        PopupSnapAssist,
        PopupSnapAssistShow,
        PopupSnapAssistHide,
        Panel,
        PanelSlideIn,
        PanelSlideOut,
        PanelFadeIn,
        PanelFadeOut,
        Cursor,
        CursorHover,
        CursorClick,
        Shader,
        ShaderOpen,
        ShaderClose,
        ShaderSwitch,
        Widget,
        WidgetHover,
        WidgetPress,
        WidgetDim,
        WidgetTint,
        WidgetTintFast,
        WidgetToggleOn,
        WidgetToggleOff,
        WidgetBadgeShow,
        WidgetBadgeHide,
        WidgetBadgePulse,
        WidgetAccordionExpand,
        WidgetAccordionCollapse,
        WidgetFadeIn,
        WidgetFadeOut,
        WidgetReorder,
        WidgetProgress,
        WidgetPulse,
        WidgetPulseFast,
        WidgetPulseSlow,
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
