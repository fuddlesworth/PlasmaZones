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
const QString EventClassDesktop = QStringLiteral("desktop");
const QString EventClassMove = QStringLiteral("move");

// window.* — split into two contract sub-trees so each has a real cascade
// parent for its "All": appearance (a surface materialising / dissolving) and
// movement (an old-rect → new-rect geometry morph). Setting window.appearance
// cascades to only the appearance leaves, window.movement to only the movement
// leaves; the `window` root still spans both.
const QString Window = QStringLiteral("window");
const QString WindowAppearance = QStringLiteral("window.appearance");
const QString WindowOpen = QStringLiteral("window.appearance.open");
const QString WindowClose = QStringLiteral("window.appearance.close");
const QString WindowMinimize = QStringLiteral("window.appearance.minimize");
const QString WindowFocus = QStringLiteral("window.appearance.focus");
// window.movement has NO resize legs. `window.movement.resize` (the
// interactive edge-drag) was dropped: it is a held gesture the crossfade
// packs cannot drive (no discrete before/after until release, and KWin
// repaints the re-laid content live anyway), and the soft-body sim
// deliberately omits KWin's resize edge-lock logic, so no pack class had a
// real story there. Discrete resizes are covered by snapIn / layoutSwitch /
// maximize. `window.movement.snapResize` was dropped with it: no callsite
// ever routed it. Stale config overrides on either path are filtered by the
// allBuiltInPaths()/shaderSupportedEventPaths() membership checks.
const QString WindowMovement = QStringLiteral("window.movement");
const QString WindowMaximize = QStringLiteral("window.movement.maximize");
const QString WindowMove = QStringLiteral("window.movement.move");
const QString WindowSnapIn = QStringLiteral("window.movement.snapIn");
const QString WindowSnapOut = QStringLiteral("window.movement.snapOut");
const QString WindowLayoutSwitch = QStringLiteral("window.movement.layoutSwitch");

// desktop.* — full-screen two-texture from/to blends driven by the
// kwin-effect's screen-level paint pass: the virtual-desktop switch, and the
// show-desktop peek (windows scene ↔ bare desktop; both peek legs resolve one
// node, with the show-back leg running the same blend with time reversed).
const QString Desktop = QStringLiteral("desktop");
const QString DesktopSwitch = QStringLiteral("desktop.switch");
const QString DesktopPeek = QStringLiteral("desktop.peek");

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
const QString PopupCheatsheet = QStringLiteral("popup.cheatsheet");
const QString PopupCheatsheetShow = QStringLiteral("popup.cheatsheet.show");
const QString PopupCheatsheetHide = QStringLiteral("popup.cheatsheet.hide");

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
        WindowAppearance,
        WindowOpen,
        WindowClose,
        WindowMinimize,
        WindowFocus,
        WindowMovement,
        WindowMaximize,
        WindowMove,
        WindowSnapIn,
        WindowSnapOut,
        WindowLayoutSwitch,
        Desktop,
        DesktopSwitch,
        DesktopPeek,
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
        PopupCheatsheet,
        PopupCheatsheetShow,
        PopupCheatsheetHide,
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
    // The interactive-drag leaf is its own opt-in class. A drag installs a
    // HELD transition: there is no old→new crossfade to play (iFromRect stays
    // invalid, progress clamps while the pointer is down), so a geometry
    // crossfade pack is a guaranteed no-op there. Only a pack consuming the
    // move-physics inputs (iMoveMesh / iMoveOffset / iMoveVelocity* /
    // iMoveTrail — position / mesh backed) can drive it, and such a pack
    // declares `move` in appliesTo (wobble). Checked BEFORE the sub-tree
    // match below so the leaf wins over its geometry-classed ancestors.
    if (path == WindowMove) {
        return EventClassMove;
    }
    // Geometry legs carry an old rect and a new rect (snap/layoutSwitch/
    // maximize) — the rest of the window.movement sub-tree, including its
    // cascade parent. Maximize IS a geometry change with a before/after
    // rect, so morph can drive it even though it isn't a built-in default.
    if (path == WindowMovement || path.startsWith(WindowMovement + QLatin1Char('.'))) {
        return EventClassGeometry;
    }
    // Appearance legs animate a single surface in or out: the whole
    // window.appearance sub-tree (open/close/minimize/focus) plus every OSD and
    // popup surface — the osd/popup roots and all their show/hide/pop
    // descendants. `startsWith` keeps future sub-surfaces classified without
    // touching this list.
    if (path == WindowAppearance || path.startsWith(WindowAppearance + QLatin1Char('.'))) {
        return EventClassAppearance;
    }
    if (path == Osd || path == Popup || path.startsWith(Osd + QLatin1Char('.'))
        || path.startsWith(Popup + QLatin1Char('.'))) {
        return EventClassAppearance;
    }
    // Desktop family — the full-screen two-texture switch contract. The
    // `desktop` root and every `desktop.*` leaf carry the desktop class so a
    // desktop-transition shader validates on the root or the leaf, and the
    // single-surface shaders are dimmed (see shaderEffectAppliesToEventPath,
    // which makes this class opt-in rather than universal-permissive).
    if (path == Desktop || path.startsWith(Desktop + QLatin1Char('.'))) {
        return EventClassDesktop;
    }
    // `window` root (mixed: spans both classes), `global`, and the
    // editor/panel/widget/cursor/shader families have no single class — the
    // predicate treats empty as "don't dim" so an ambiguous row never
    // suppresses a compatible effect.
    return QString();
}

QString defaultShaderEffectIdForPath(const QString& path)
{
    // Window snap events default to the geometry-morph shader so a window
    // animates via shader cross-fade when it snaps/tiles/reflows. This is
    // the same geometry leg set `eventClassForPath` classes as
    // EventClassGeometry, MINUS maximize (maximize is geometry-classed so
    // morph is selectable there, but it isn't a built-in default) — keep the
    // two lists in sync if a new geometry leg is added. `window.movement.move`
    // is EXCLUDED: the interactive drag is a held transition a crossfade pack
    // cannot drive (see eventClassForPath), so it carries no built-in default
    // and its move-class packs (wobble) stay opt-in.
    if (path == WindowSnapIn || path == WindowSnapOut || path == WindowLayoutSwitch) {
        return QStringLiteral("window-morph");
    }
    // Overlay surface show/hide (OSD + popups) default to the fade-and-scale
    // shader so the daemon animates them via shader instead of the C++
    // opacity/scale legs in SurfaceAnimator (which stay as the fallback when a
    // shader is unavailable or the user picks "None"). Per-surface scale feel is
    // preserved by seeding fade's `scaleAmount` daemon-side (animation_config).
    if (path == OsdShow || path == OsdHide || path == PopupZoneSelectorShow || path == PopupZoneSelectorHide
        || path == PopupLayoutPickerShow || path == PopupLayoutPickerHide || path == PopupSnapAssistShow
        || path == PopupSnapAssistHide || path == PopupCheatsheetShow || path == PopupCheatsheetHide) {
        return QStringLiteral("fade");
    }
    // The desktop transitions (switch AND peek) have NO built-in default: both
    // are full-screen, intrusive transitions that contend with KWin's own
    // effects (Slide for the switch, windowaperture/eyeonscreen for the peek —
    // the latter are even unloaded while a peek pack is assigned), so they
    // stay opt-in. A fresh config animates them only once the user picks a
    // desktop pack (e.g. Desktop Fade) on the Animations → Transitions →
    // Desktop page.
    // Every other event defaults to no shader.
    return QString();
}

} // namespace ProfilePaths
} // namespace PhosphorAnimation
