// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfilePaths.h>

namespace PhosphorAnimation {
namespace ProfilePaths {

// Root
const QString Global = QStringLiteral("global");

// Event-class tokens (see ProfilePaths.h). Kept as QStringLiteral so they
// compare cheaply against AnimationShaderEffect::appliesTo entries.
const QString EventClassGeometry = QStringLiteral("geometry");
const QString EventClassAppearance = QStringLiteral("appearance");

// window.*
const QString Window = QStringLiteral("window");
const QString WindowOpen = QStringLiteral("window.open");
const QString WindowClose = QStringLiteral("window.close");
const QString WindowMinimize = QStringLiteral("window.minimize");
const QString WindowMaximize = QStringLiteral("window.maximize");
const QString WindowMove = QStringLiteral("window.move");
const QString WindowResize = QStringLiteral("window.resize");
const QString WindowFocus = QStringLiteral("window.focus");
const QString WindowSnapIn = QStringLiteral("window.snapIn");
const QString WindowSnapOut = QStringLiteral("window.snapOut");
const QString WindowSnapResize = QStringLiteral("window.snapResize");
const QString WindowLayoutSwitch = QStringLiteral("window.layoutSwitch");

// editor.* — Layout-editor-only zone manipulation. NOT runtime
// window snapping (that's KWin's domain).
const QString Editor = QStringLiteral("editor");
const QString EditorSnapIn = QStringLiteral("editor.snapIn");
const QString EditorSnapOut = QStringLiteral("editor.snapOut");
const QString EditorSnapResize = QStringLiteral("editor.snapResize");

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
const QString WidgetZoneHighlight = QStringLiteral("widget.zoneHighlight");
const QString WidgetZoneHighlightPop = QStringLiteral("widget.zoneHighlight.pop");
const QString WidgetZoneHighlightBorder = QStringLiteral("widget.zoneHighlight.border");
const QString WidgetZoneOverlayFlash = QStringLiteral("widget.zoneOverlayFlash");

QStringList allBuiltInPaths()
{
    // Cache as a function-local static — the path list is compile-time
    // stable, so reconstructing it on every call is wasteful for the
    // settings UI that iterates this list multiple times. QStringList
    // is implicitly shared, so the return-by-value just increments the
    // refcount instead of copying every QString.
    static const QStringList kAllPaths = {
        Global,
        Window,
        WindowOpen,
        WindowClose,
        WindowMinimize,
        WindowMaximize,
        WindowMove,
        WindowResize,
        WindowFocus,
        WindowSnapIn,
        WindowSnapOut,
        WindowSnapResize,
        WindowLayoutSwitch,
        Editor,
        EditorSnapIn,
        EditorSnapOut,
        EditorSnapResize,
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
        WidgetZoneHighlight,
        WidgetZoneHighlightPop,
        WidgetZoneHighlightBorder,
        WidgetZoneOverlayFlash,
    };
    return kAllPaths;
}

QString parentPath(const QString& path)
{
    if (path.isEmpty() || path == Global) {
        return QString();
    }
    const int dotIdx = path.lastIndexOf(QLatin1Char('.'));
    if (dotIdx < 0) {
        // Category root ("window", "editor", "widget", …) → Global.
        return Global;
    }
    return path.left(dotIdx);
}

QString eventClassForPath(const QString& path)
{
    // Geometry legs carry an old rect and a new rect (move/resize/snap/
    // tile/layoutSwitch/maximize). Mirrors the geometry set that
    // defaultShaderEffectIdForPath seeds with window-morph, plus maximize
    // (a maximize IS a geometry change with a before/after rect — morph can
    // drive it even though it isn't a built-in default there).
    if (path == WindowMove || path == WindowResize || path == WindowSnapIn || path == WindowSnapOut
        || path == WindowSnapResize || path == WindowLayoutSwitch || path == WindowMaximize) {
        return EventClassGeometry;
    }
    // Appearance legs animate a single surface in or out. Window lifecycle
    // leaves (open/close/minimize/focus) plus every OSD and popup surface —
    // the osd/popup roots and all their show/hide/pop descendants. A
    // `startsWith` on the family roots keeps future popup sub-surfaces
    // classified without touching this list.
    if (path == WindowOpen || path == WindowClose || path == WindowMinimize || path == WindowFocus) {
        return EventClassAppearance;
    }
    if (path == Osd || path == Popup || path.startsWith(Osd + QLatin1Char('.'))
        || path.startsWith(Popup + QLatin1Char('.'))) {
        return EventClassAppearance;
    }
    // `window` root (mixed: spans both classes), `global`, and the
    // editor/panel/widget/cursor/shader families have no single class — the
    // predicate treats empty as "don't dim" so an ambiguous row never
    // suppresses a compatible effect.
    return QString();
}

QString defaultShaderEffectIdForPath(const QString& path)
{
    // Window move/resize events default to the geometry-morph shader so a
    // window animates via shader cross-fade when it snaps/tiles/reflows.
    if (path == WindowMove || path == WindowResize || path == WindowSnapIn || path == WindowSnapOut
        || path == WindowSnapResize || path == WindowLayoutSwitch) {
        return QStringLiteral("window-morph");
    }
    // Overlay surface show/hide (OSD + popups) default to the fade-and-scale
    // shader so the daemon animates them via shader instead of the C++
    // opacity/scale legs in SurfaceAnimator (which stay as the fallback when a
    // shader is unavailable or the user picks "None"). Per-surface scale feel is
    // preserved by seeding fade's `scaleAmount` daemon-side (animation_config).
    if (path == OsdShow || path == OsdHide || path == PopupZoneSelectorShow || path == PopupZoneSelectorHide
        || path == PopupLayoutPickerShow || path == PopupLayoutPickerHide || path == PopupSnapAssistShow
        || path == PopupSnapAssistHide) {
        return QStringLiteral("fade");
    }
    // Every other event defaults to no shader.
    return QString();
}

} // namespace ProfilePaths
} // namespace PhosphorAnimation
