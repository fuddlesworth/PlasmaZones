// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The condensed simple-mode pages' search anchors, split out of
// searchcatalog.cpp by concern (the single catalog TU had crossed the
// file-size ceiling; same split as searchcatalog_animations.cpp). Their
// rows duplicate settings the advanced pages also expose, deliberately:
// a search in simple mode must land on a page that mode can actually
// show, and the advanced twins are filtered out of the rail there.
// Anchors match the ids the simple pages register. Called only by
// seedSearchCatalog.

#include "searchcatalog_p.h"

#include "phosphor_i18n.h"

namespace PlasmaZones {

using SearchCatalogDetail::addSection;
using SearchCatalogDetail::addSetting;

void seedSimplePageAnchors(PhosphorControl::SearchController* search)
{
    // Condensed simple-mode pages. Their rows duplicate settings the
    // advanced pages above also expose, deliberately: a search in simple
    // mode must land on a page that mode can actually show, and the
    // advanced twins are filtered out of the rail there. Anchors match the
    // ids the simple pages register.
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("snappingSimple"),
               PhosphorI18n::tr("Snapping"));
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("simpleActivateOnEveryDrag"),
               PhosphorI18n::tr("Show zones on every drag"),
               {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("drag"), PhosphorI18n::tr("trigger")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("simpleHoldToActivate"),
               PhosphorI18n::tr("Hold to show zones"), {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("trigger")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("simpleSnapAssist"),
               PhosphorI18n::tr("Snap Assist"), {PhosphorI18n::tr("picker"), PhosphorI18n::tr("fill zones")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("simpleAlwaysShowAfterSnapping"),
               PhosphorI18n::tr("Always show after snapping"), {PhosphorI18n::tr("snap assist")});

    addSection(search, QStringLiteral("tiling-simple"), QStringLiteral("tilingSimple"), PhosphorI18n::tr("Tiling"));
    // The algorithm picker is this page's headline control, and in simple mode
    // the advanced Algorithm page is tier-filtered out along with its own
    // "algorithm" anchor. Without this row a search for an algorithm name
    // resolves only to the page itself and never reveals the picker.
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("simpleAlgorithm"),
               PhosphorI18n::tr("Tiling algorithm"),
               {PhosphorI18n::tr("algorithm"), PhosphorI18n::tr("bsp"), PhosphorI18n::tr("spiral"),
                PhosphorI18n::tr("master"), PhosphorI18n::tr("grid"), PhosphorI18n::tr("layout")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("simpleMasterRatio"),
               PhosphorI18n::tr("Master ratio"),
               {PhosphorI18n::tr("split"), PhosphorI18n::tr("proportion"), PhosphorI18n::tr("center")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("simpleMaxWindows"),
               PhosphorI18n::tr("Max windows"), {PhosphorI18n::tr("limit"), PhosphorI18n::tr("count")});

    // Scrolling's own condensed rows plus the shared Window Handling and
    // Focus and view cards it re-hosts, under the same rationale as its two siblings
    // above.
    addSection(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingSimple"),
               PhosphorI18n::tr("Scrolling"));
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleDefaultColumnWidthKind"),
               PhosphorI18n::tr("Default width"),
               {PhosphorI18n::tr("width"), PhosphorI18n::tr("column"), PhosphorI18n::tr("proportion")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleDefaultColumnWidthProportion"),
               PhosphorI18n::tr("Proportion of the screen"),
               {PhosphorI18n::tr("width"), PhosphorI18n::tr("proportion"), PhosphorI18n::tr("percent")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleDefaultColumnWidthFixed"),
               PhosphorI18n::tr("Fixed width"), {PhosphorI18n::tr("width"), PhosphorI18n::tr("pixels")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleDefaultColumnWidthPresetIndex"),
               PhosphorI18n::tr("Preset width"),
               {PhosphorI18n::tr("preset"), PhosphorI18n::tr("width"), PhosphorI18n::tr("index"),
                PhosphorI18n::tr("template")});
    // The simple page's Tabs card — the three tab-indicator rows it surfaces.
    // Their own anchors (simple*) because the row ids must be unique per page
    // and the advanced Tabs leaf owns the unprefixed ones.
    addSection(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleTabs"), PhosphorI18n::tr("Tabs"));
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleTabIndicatorEnabled"),
               PhosphorI18n::tr("Show the tab indicator"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("indicator"), PhosphorI18n::tr("strip")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleTabIndicatorStyle"),
               PhosphorI18n::tr("Style"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("chips"), PhosphorI18n::tr("bar")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("simpleTabIndicatorPosition"),
               PhosphorI18n::tr("Position"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("left"), PhosphorI18n::tr("right"), PhosphorI18n::tr("top"),
                PhosphorI18n::tr("bottom")});
    addSection(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingWindowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingNewWindowPlacement"),
               PhosphorI18n::tr("New window placement"),
               {PhosphorI18n::tr("insert"), PhosphorI18n::tr("position"), PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingRespectMinimumSize"),
               PhosphorI18n::tr("Respect minimum size"), {PhosphorI18n::tr("minimum"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingRestoreStripsOnLogin"),
               PhosphorI18n::tr("Restore columns on login"),
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("login"), PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingRestoreFloatedOnLogin"),
               PhosphorI18n::tr("Restore floated windows to their previous position"),
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("float"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingStickyWindows"),
               PhosphorI18n::tr("Sticky windows"), {PhosphorI18n::tr("sticky"), PhosphorI18n::tr("desktops")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingColumnWidthStep"),
               PhosphorI18n::tr("Width adjustment step"),
               {PhosphorI18n::tr("step"), PhosphorI18n::tr("width"), PhosphorI18n::tr("shortcut")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingWindowHeightStep"),
               PhosphorI18n::tr("Height adjustment step"),
               {PhosphorI18n::tr("step"), PhosphorI18n::tr("height"), PhosphorI18n::tr("shortcut")});
    addSetting(
        search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingViewScrollStep"),
        PhosphorI18n::tr("View scroll step"),
        {PhosphorI18n::tr("step"), PhosphorI18n::tr("scroll"), PhosphorI18n::tr("view"), PhosphorI18n::tr("wheel")});
    // The shared Strip direction card's pair, mirrored here because the
    // simple page hosts the same card (anchors resolve per hosting page id).
    addSection(search, QStringLiteral("scrolling-simple"), QStringLiteral("stripDirection"),
               PhosphorI18n::tr("Strip direction"));
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("stripAxis"), PhosphorI18n::tr("Direction"),
               {PhosphorI18n::tr("strip"), PhosphorI18n::tr("axis"), PhosphorI18n::tr("vertical"),
                PhosphorI18n::tr("horizontal"), PhosphorI18n::tr("portrait")});
    addSection(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingFocus"),
               PhosphorI18n::tr("Focus and view"));
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("centerFocusedColumn"),
               PhosphorI18n::tr("Center the focused column"),
               {PhosphorI18n::tr("center"), PhosphorI18n::tr("focus"), PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("alwaysCenterSingleColumn"),
               PhosphorI18n::tr("Center a lone column"),
               {PhosphorI18n::tr("center"), PhosphorI18n::tr("single"), PhosphorI18n::tr("column")});
    addSetting(
        search, QStringLiteral("scrolling-simple"), QStringLiteral("cropStraddlers"),
        PhosphorI18n::tr("Crop columns at the screen edge"),
        {PhosphorI18n::tr("crop"), PhosphorI18n::tr("clip"), PhosphorI18n::tr("edge"), PhosphorI18n::tr("cut off")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingFocusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("open")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("scrollingFocusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("mouse")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("wheelFocusEnabled"),
               PhosphorI18n::tr("Scroll the strip with the mouse wheel"),
               {PhosphorI18n::tr("wheel"), PhosphorI18n::tr("mouse")});
    addSetting(search, QStringLiteral("scrolling-simple"), QStringLiteral("wheelFocusInverted"),
               PhosphorI18n::tr("Invert wheel direction"), {PhosphorI18n::tr("invert"), PhosphorI18n::tr("wheel")});

    // The simple animations page hosts the SHARED GlobalTimingDefaultsCard,
    // which registers the same "globalAnimationDefaults" anchor the advanced
    // General page does — so the anchor needs a row against BOTH page ids or
    // the simple-mode user is routed to a page their mode filters out.
    addSection(search, QStringLiteral("animations-simple"), QStringLiteral("globalAnimationDefaults"),
               PhosphorI18n::tr("Global animation defaults"));
    // The simple page's event cards register their eventPath as the anchor
    // (see AnimationEventCardList), so each grouped card needs its own row —
    // the advanced pages that otherwise carry these ids are tier-filtered out
    // in simple mode. The window.movement parent node is not searchable.
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("window.appearance.open"),
               PhosphorI18n::tr("Window opened & closed"),
               {PhosphorI18n::tr("open"), PhosphorI18n::tr("close"), PhosphorI18n::tr("effect")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("window.appearance.minimize"),
               PhosphorI18n::tr("Window minimized"), {PhosphorI18n::tr("minimize"), PhosphorI18n::tr("effect")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("window.movement.move"),
               PhosphorI18n::tr("Window moved"), {PhosphorI18n::tr("drag"), PhosphorI18n::tr("move")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("desktop.switch"),
               PhosphorI18n::tr("Desktop switched"), {PhosphorI18n::tr("desktop"), PhosphorI18n::tr("switch")});

    // The condensed simple pages host the SAME shared cards their advanced
    // twins do, so every anchor those cards register needs a row against the
    // simple page id too — SearchController drops entries whose host page the
    // active tier hides, which would otherwise make all of these unsearchable
    // in simple mode. Anchors are page-scoped, so the duplicate ids collide
    // with nothing.
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("zoneSpan"), PhosphorI18n::tr("Zone span"));
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("spanModifier"),
               PhosphorI18n::tr("Span modifier"), {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("multi-zone")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("zoneSpanToggleMode"),
               PhosphorI18n::tr("Zone span toggle mode"), {PhosphorI18n::tr("zone span")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("edgeThreshold"),
               PhosphorI18n::tr("Edge threshold"), {PhosphorI18n::tr("distance"), PhosphorI18n::tr("multi-zone")});
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("reSnapOnResolutionChange"),
               PhosphorI18n::tr("Re-snap on resolution change"), {PhosphorI18n::tr("resolution")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("openNewWindowsInLastUsedZone"),
               PhosphorI18n::tr("Open new windows in the last-used zone"), {PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("autoAssignNewWindowsAllLayouts"),
               PhosphorI18n::tr("Auto-assign new windows for all layouts"), {PhosphorI18n::tr("auto-assign")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("restoreSizeOnUnsnap"),
               PhosphorI18n::tr("Restore size on unsnap"), {PhosphorI18n::tr("unsnap"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("restoreWindowsToPreviousZone"),
               PhosphorI18n::tr("Restore windows to their previous zone"), {PhosphorI18n::tr("restore")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("restoreUnsnappedWindowsPosition"),
               PhosphorI18n::tr("Restore unsnapped windows to their previous position"), {PhosphorI18n::tr("restore")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("unfloatToZoneFallback"),
               PhosphorI18n::tr("Unfloat to a zone when there is no previous zone"), {PhosphorI18n::tr("unfloat")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), {PhosphorI18n::tr("all desktops")});
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"));
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});

    addSection(search, QStringLiteral("tiling-simple"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("newWindowPlacement"),
               PhosphorI18n::tr("New window placement"), {PhosphorI18n::tr("insert"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("respectMinimumSize"),
               PhosphorI18n::tr("Respect minimum size"), {PhosphorI18n::tr("minimum")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("smartGaps"), PhosphorI18n::tr("Smart gaps"),
               {PhosphorI18n::tr("gaps"), PhosphorI18n::tr("single window")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("restoreUntiledWindowsPosition"),
               PhosphorI18n::tr("Restore untiled windows to their previous position"), {PhosphorI18n::tr("restore")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), {PhosphorI18n::tr("all desktops")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("dragBehavior"),
               PhosphorI18n::tr("Drag behavior"), {PhosphorI18n::tr("float"), PhosphorI18n::tr("reorder")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("overflowBehavior"),
               PhosphorI18n::tr("Overflow behavior"), {PhosphorI18n::tr("overflow"), PhosphorI18n::tr("unlimited")});
    addSection(search, QStringLiteral("tiling-simple"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"));
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("tiling-simple"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});

    addSection(search, QStringLiteral("animations-simple"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window filtering"));
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("excludeTransient"),
               PhosphorI18n::tr("Exclude transient windows"),
               {PhosphorI18n::tr("dialogs"), PhosphorI18n::tr("popups"), PhosphorI18n::tr("tooltips")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("excludeNotificationsAndOsds"),
               PhosphorI18n::tr("Exclude notifications and OSDs"),
               {PhosphorI18n::tr("on-screen display"), PhosphorI18n::tr("volume"), PhosphorI18n::tr("brightness")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"), {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"), {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short")});
}

} // namespace PlasmaZones
