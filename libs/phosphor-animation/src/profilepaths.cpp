// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfilePaths.h>

namespace PhosphorAnimation {
namespace ProfilePaths {

// Root
const QString Global = QStringLiteral("global");

// Event-class tokens (see ProfilePaths.h). QStringLiteral rather than a bare
// literal because these are namespace-scope QStrings: the literal form builds
// its QString data at compile time, so construction allocates nothing at
// static-init.
const QString EventClassGeometry = QStringLiteral("geometry");
const QString EventClassAppearance = QStringLiteral("appearance");
// NOTE: this token is the same STRING as the `Desktop` path root below. The
// collision is deliberate (both are the user-facing word) and it is the only
// one among these constants, but it means eventClassForPath(Desktop) returns
// its own argument. No store may key on "a path or a class token" in one map.
const QString EventClassDesktop = QStringLiteral("desktop");
const QString EventClassMove = QStringLiteral("move");
const QString EventClassStrip = QStringLiteral("strip");
const QString EventClassTab = QStringLiteral("tab");

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
// ever routed it. A stale config override on either path is pruned from the
// SHADER tree by shaderSupportedEventPaths() on both read and write. The
// motion tree has no such prune, so a motion override there simply lingers,
// unreachable from the UI and named by no resolver.
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

// scrolling.* — the scrolling strip's VIEW, not any window on it. Its own
// root rather than a window.movement.* leaf because the subject is the view:
// one leg moves every column at once, and the compositor drives it with a
// single per-output spring. The tab-indicator overlay does not mirror it: the
// compositor adds the same offset to that surface in the same paint pass, so
// one spring drives both. A second spring on the daemon side was tried and
// cannot work, because the overlay composites a frame or more behind the pass
// that moves the columns.
const QString Scrolling = QStringLiteral("scrolling");
const QString ScrollingView = QStringLiteral("scrolling.view");
// The tab swap inside a tabbed column. Grouped under scrolling because tabbed
// columns exist only there, but it is NOT the strip's one-scene contract and
// carries its own opt-in class instead — see eventClassForPath. The subject is
// a single window taking the rect another just vacated, which is the same
// old-content crossfade the snap morph runs, only with the old content coming
// from a DIFFERENT window.
const QString ScrollingTabSwitch = QStringLiteral("scrolling.tabSwitch");

// shell.* — the desktop shell's own surfaces, currently the Plasma applet
// popups (launcher, tray flyouts, a widget's expanded view). Their legs are
// appearance legs like a window's, and the kwin-effect installs them from the
// same windowAdded / windowClosed hooks, but they hang off their own root so
// nothing a user chose for their WINDOWS reaches a surface plasmashell owns.
// The shader tree makes that structural (shaderPathIsolationRoot): the whole
// subtree resolves inside itself, so a shell surface animates only once a pack
// is engaged on one of these nodes.
const QString Shell = QStringLiteral("shell");
const QString ShellAppletPopup = QStringLiteral("shell.appletPopup");
const QString ShellAppletPopupShow = QStringLiteral("shell.appletPopup.show");
const QString ShellAppletPopupHide = QStringLiteral("shell.appletPopup.hide");

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
        Scrolling,
        ScrollingView,
        ScrollingTabSwitch,
        Shell,
        ShellAppletPopup,
        ShellAppletPopupShow,
        ShellAppletPopupHide,
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

QStringList allEventClassTokens()
{
    // Built once: callers iterate this behind picker filters and validators,
    // and the vocabulary cannot change at runtime.
    static const QStringList tokens{EventClassGeometry, EventClassAppearance, EventClassDesktop,
                                    EventClassMove,     EventClassStrip,      EventClassTab};
    return tokens;
}

bool eventPathResolvesPerWindow(const QString& path)
{
    // An explicit list rather than a prefix rule, because the boundary does not
    // follow the taxonomy's shape: `scrolling.tabSwitch` is per-window while its
    // sibling `scrolling.view` is not, and the `window` category nodes are not
    // per-window either since the compositor only ever resolves leaves.
    //
    // Each entry names the call site that supplies the window, so a reader can
    // check the claim rather than trust it:
    //   window.appearance.*  — tryBeginShaderForEvent from window_lifecycle
    //                          (open/close/focus) and daemon_apply (minimize)
    //   window.movement.maximize / .move
    //                        — tryBeginShaderForEvent from window_connections
    //   window.movement.snapIn / .snapOut / .layoutSwitch
    //                        — applyWindowGeometry's resolve in drag_snap, and
    //                          every other applyWindowGeometry caller that takes
    //                          the default profilePath (it defaults to snapIn),
    //                          which includes the tiling and scrolling reflows
    //   scrolling.tabSwitch  — tryBeginShaderForEvent from the tiling handler's
    //                          tab swap, which passes the arriving window
    static const QStringList kPerWindowPaths{
        WindowOpen, WindowClose,  WindowMinimize, WindowFocus,        WindowMaximize,
        WindowMove, WindowSnapIn, WindowSnapOut,  WindowLayoutSwitch, ScrollingTabSwitch,
    };
    return kPerWindowPaths.contains(path);
}

QString eventClassForPath(const QString& path)
{
    // The dotted sub-tree prefixes, built once. This runs per row per repaint
    // behind the settings picker's filter, and each of these used to allocate
    // a temporary QString on every call.
    static const QString movementPrefix = WindowMovement + QLatin1Char('.');
    static const QString appearancePrefix = WindowAppearance + QLatin1Char('.');
    static const QString osdPrefix = Osd + QLatin1Char('.');
    static const QString popupPrefix = Popup + QLatin1Char('.');
    static const QString desktopPrefix = Desktop + QLatin1Char('.');
    static const QString scrollingPrefix = Scrolling + QLatin1Char('.');
    static const QString shellPrefix = Shell + QLatin1Char('.');

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
    if (path == WindowMovement || path.startsWith(movementPrefix)) {
        return EventClassGeometry;
    }
    // Appearance legs animate a single surface in or out: the whole
    // window.appearance sub-tree (open/close/minimize/focus) plus every OSD and
    // popup surface — the osd/popup roots and all their show/hide/pop
    // descendants. `startsWith` keeps future sub-surfaces classified without
    // touching this list.
    if (path == WindowAppearance || path.startsWith(appearancePrefix)) {
        return EventClassAppearance;
    }
    if (path == Osd || path == Popup || path.startsWith(osdPrefix) || path.startsWith(popupPrefix)) {
        return EventClassAppearance;
    }
    // The shell family is appearance too, and deliberately NOT a class of its
    // own: a shell surface is a single surface materialising or dissolving,
    // exactly what an appearance pack draws, and it binds the same single
    // sampler. What sets these paths apart is WHOSE surface it is, which the
    // shader tree answers by isolating the subtree rather than by narrowing
    // the pack vocabulary. A new class here would only make every existing
    // appearance pack unselectable for no gain.
    if (path == Shell || path.startsWith(shellPrefix)) {
        return EventClassAppearance;
    }
    // Desktop family — the full-screen two-texture switch contract. The
    // `desktop` root and every `desktop.*` leaf carry the desktop class so a
    // desktop-transition shader validates on the root or the leaf, and the
    // single-surface shaders are dimmed (see shaderEffectAppliesToEventPath,
    // which makes this class opt-in rather than universal-permissive).
    if (path == Desktop || path.startsWith(desktopPrefix)) {
        return EventClassDesktop;
    }
    // Scrolling family — the strip's one-scene post-process contract. The
    // view spring retargets continuously under wheel scrolling (no discrete
    // from/to legs), so neither the crossfade classes nor the two-texture
    // desktop class fit: a strip pack decorates the live capture driven by
    // offset/velocity (iStripMotion) and must opt in via
    // `appliesTo: ["strip"]`. Root and every leaf carry the class, mirroring
    // desktop.
    // The tab swap is the one scrolling leaf that is NOT a strip pass, and it
    // is not an appearance leg either. It cross-fades a snapshot of the
    // OUTGOING tab (uOldWindow) into the live content of the incoming one, so
    // it is two-texture like the desktop switch, on a window quad rather than
    // a screen. A universal single-surface pack would fade the arriving tab in
    // over whatever lies behind the column, which is the wallpaper, so its own
    // class keeps it opt-in the way desktop and strip are. Checked BEFORE the
    // sub-tree match below so the leaf wins over its scrolling ancestor, the
    // same ordering window.movement.move uses against its own family.
    if (path == ScrollingTabSwitch) {
        return EventClassTab;
    }
    if (path == Scrolling || path.startsWith(scrollingPrefix)) {
        return EventClassStrip;
    }
    // `window` root (mixed: spans both classes), `global`, and the
    // editor/panel/widget/cursor/shader families have no single class — the
    // predicate treats empty as "don't dim" so an ambiguous row never
    // suppresses a compatible effect.
    return QString();
}

QString defaultShaderEffectIdForPath(const QString& path)
{
    // Window geometry events default to the geometry-morph shader so a window
    // animates via shader cross-fade when it snaps/tiles/reflows/maximizes.
    // This is the same geometry leg set `eventClassForPath` classes as
    // EventClassGeometry — keep the two lists in sync if a new geometry leg
    // is added. Maximize is in the set because the engines route their own
    // maximizes through it (a scroll column maximized to the edges, a monocle
    // tile): before it carried a default those legs rode snapIn, and moving
    // them here without a default would have un-animated every fresh config.
    // The cost is that a built-in default counts as pack ownership for the
    // stock-effect suppression (syncStockEffectSuppression), so KWin's own
    // maximize effect is unloaded by default. `window.movement.move` is
    // EXCLUDED: the interactive drag is a held transition a crossfade pack
    // cannot drive (see eventClassForPath), so it carries no built-in default
    // and its move-class packs (wobble) stay opt-in.
    if (path == WindowSnapIn || path == WindowSnapOut || path == WindowLayoutSwitch || path == WindowMaximize) {
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
    //
    // `scrolling.view` is opt-in for the same reason from the other
    // direction: its pass runs on EVERY wheel scroll, so a built-in default
    // would put a full-screen post-process on the most frequent interaction
    // in the mode. Scrolling stays a plain translation until the user picks a
    // strip pack on Animations → Motion → Scrolling.
    //
    // `scrolling.tabSwitch` joins them, and its case is the closest call of
    // the three. Without a pack the swap is a hard cut, which IS worse than
    // the plain behaviour the other two fall back to — but the pass costs a
    // window-sized capture and a skipped frame per switch, and a transition
    // that installs itself on a fresh config is the kind of thing a user
    // should choose rather than discover. Every tab pack (Tab Fade first) is
    // one pick away on the same page as the strip packs.
    //
    // The `shell.*` legs carry no default either, and theirs is not a
    // judgement call: the shell subtree is isolated in the shader tree
    // precisely so a surface plasmashell owns animates nothing until the user
    // says otherwise, and a built-in default here would reach around that
    // isolation and put a transition on every tray flyout of a fresh install.
    // Every other event defaults to no shader.
    return QString();
}

} // namespace ProfilePaths
} // namespace PhosphorAnimation
