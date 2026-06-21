// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchcatalog.h"

#include "phosphor_i18n.h"

#include <PhosphorControl/SearchController.h>
#include <PhosphorControl/SearchEntry.h>

#include <QString>
#include <QStringList>

using PhosphorControl::SearchEntry;

namespace PlasmaZones {

namespace {

void addSetting(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                const QString& title, const QString& breadcrumb, const QStringList& keywords = {})
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Setting;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.subtitle = breadcrumb;
    e.keywords = keywords;
    search->addEntry(e);
}

void addSection(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                const QString& title, const QString& breadcrumb)
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Section;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.subtitle = breadcrumb;
    search->addEntry(e);
}

} // namespace

void seedSearchCatalog(PhosphorControl::SearchController* search)
{
    if (search == nullptr) {
        return;
    }

    // ── Per-page synonyms ────────────────────────────────────────────────
    // Page entries are auto-derived from the registry; these add search terms a
    // user is likely to type that don't appear in the page title. Literal
    // PhosphorI18n::tr calls (not a wrapper) so `update-ts` extracts them.
    search->setPageKeywords(QStringLiteral("overview"),
                            {PhosphorI18n::tr("monitor"), PhosphorI18n::tr("display"), PhosphorI18n::tr("mode"),
                             PhosphorI18n::tr("active layout")});
    search->setPageKeywords(QStringLiteral("general"),
                            {PhosphorI18n::tr("rendering"), PhosphorI18n::tr("backend"), PhosphorI18n::tr("opengl"),
                             PhosphorI18n::tr("vulkan"), PhosphorI18n::tr("backup"), PhosphorI18n::tr("export"),
                             PhosphorI18n::tr("import"), PhosphorI18n::tr("reset")});
    search->setPageKeywords(QStringLiteral("virtualscreens"),
                            {PhosphorI18n::tr("split"), PhosphorI18n::tr("subdivide"), PhosphorI18n::tr("region"),
                             PhosphorI18n::tr("monitor")});
    search->setPageKeywords(QStringLiteral("layouts"),
                            {PhosphorI18n::tr("layout"), PhosphorI18n::tr("zone"), PhosphorI18n::tr("grid"),
                             PhosphorI18n::tr("preset"), PhosphorI18n::tr("template"),
                             PhosphorI18n::tr("aspect ratio")});

    // Snapping
    search->setPageKeywords(QStringLiteral("snapping-overlay-behavior"),
                            {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("trigger"), PhosphorI18n::tr("edge"),
                             PhosphorI18n::tr("magnet"), PhosphorI18n::tr("snap")});
    search->setPageKeywords(QStringLiteral("snapping-overlay-appearance"),
                            {PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"), PhosphorI18n::tr("opacity"),
                             PhosphorI18n::tr("transparency"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("border")});
    search->setPageKeywords(QStringLiteral("snapping-zoneselector"),
                            {PhosphorI18n::tr("zone selector"), PhosphorI18n::tr("picker"), PhosphorI18n::tr("chooser"),
                             PhosphorI18n::tr("popup")});
    search->setPageKeywords(QStringLiteral("snapping-window-behavior"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("snap"), PhosphorI18n::tr("drag"),
                             PhosphorI18n::tr("modifier"), PhosphorI18n::tr("key")});
    search->setPageKeywords(QStringLiteral("snapping-window-appearance"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("highlight"), PhosphorI18n::tr("indicator"),
                             PhosphorI18n::tr("outline")});
    search->setPageKeywords(QStringLiteral("snapping-ordering"),
                            {PhosphorI18n::tr("priority"), PhosphorI18n::tr("order"), PhosphorI18n::tr("precedence")});
    search->setPageKeywords(QStringLiteral("snapping-shortcuts"),
                            {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("hotkey"), PhosphorI18n::tr("keybind"),
                             PhosphorI18n::tr("keyboard"), PhosphorI18n::tr("key")});
    search->setPageKeywords(QStringLiteral("snapping-shaders"),
                            {PhosphorI18n::tr("shader"), PhosphorI18n::tr("effect"), PhosphorI18n::tr("glow")});

    // Tiling
    search->setPageKeywords(QStringLiteral("tiling-behavior"),
                            {PhosphorI18n::tr("tile"), PhosphorI18n::tr("tiling"), PhosphorI18n::tr("auto"),
                             PhosphorI18n::tr("gap"), PhosphorI18n::tr("spacing")});
    search->setPageKeywords(QStringLiteral("tiling-appearance"),
                            {PhosphorI18n::tr("tile"), PhosphorI18n::tr("gap"), PhosphorI18n::tr("spacing"),
                             PhosphorI18n::tr("border"), PhosphorI18n::tr("color")});
    search->setPageKeywords(QStringLiteral("tiling-algorithm"),
                            {PhosphorI18n::tr("algorithm"), PhosphorI18n::tr("bsp"), PhosphorI18n::tr("binary"),
                             PhosphorI18n::tr("spiral"), PhosphorI18n::tr("master"), PhosphorI18n::tr("stack")});
    search->setPageKeywords(QStringLiteral("tiling-ordering"),
                            {PhosphorI18n::tr("priority"), PhosphorI18n::tr("order"), PhosphorI18n::tr("precedence")});
    search->setPageKeywords(QStringLiteral("tiling-shortcuts"),
                            {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("hotkey"), PhosphorI18n::tr("keybind"),
                             PhosphorI18n::tr("key")});

    // Animations
    search->setPageKeywords(QStringLiteral("animations-general"),
                            {PhosphorI18n::tr("animation"), PhosphorI18n::tr("duration"), PhosphorI18n::tr("easing"),
                             PhosphorI18n::tr("curve"), PhosphorI18n::tr("spring"), PhosphorI18n::tr("speed")});
    search->setPageKeywords(QStringLiteral("animations-windows"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("animation"), PhosphorI18n::tr("open"),
                             PhosphorI18n::tr("close")});
    search->setPageKeywords(
        QStringLiteral("animations-osds"),
        {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification"), PhosphorI18n::tr("on-screen display")});
    search->setPageKeywords(QStringLiteral("animations-overlays"),
                            {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("animation")});
    search->setPageKeywords(QStringLiteral("animations-side-panels"),
                            {PhosphorI18n::tr("side panel"), PhosphorI18n::tr("panel"), PhosphorI18n::tr("drawer")});
    search->setPageKeywords(QStringLiteral("animations-widgets"),
                            {PhosphorI18n::tr("widget"), PhosphorI18n::tr("animation")});
    search->setPageKeywords(
        QStringLiteral("animations-editor"),
        {PhosphorI18n::tr("editor"), PhosphorI18n::tr("layout editor"), PhosphorI18n::tr("animation")});
    search->setPageKeywords(QStringLiteral("animations-presets"),
                            {PhosphorI18n::tr("preset"), PhosphorI18n::tr("curve"), PhosphorI18n::tr("easing"),
                             PhosphorI18n::tr("profile")});
    search->setPageKeywords(QStringLiteral("animations-motionsets"),
                            {PhosphorI18n::tr("motion set"), PhosphorI18n::tr("profile"), PhosphorI18n::tr("motion")});
    search->setPageKeywords(QStringLiteral("animations-shaders"),
                            {PhosphorI18n::tr("shader"), PhosphorI18n::tr("effect")});

    // Top-level + tools
    search->setPageKeywords(QStringLiteral("window-rules"),
                            {PhosphorI18n::tr("rule"), PhosphorI18n::tr("exclude"), PhosphorI18n::tr("float"),
                             PhosphorI18n::tr("monitor"), PhosphorI18n::tr("priority"), PhosphorI18n::tr("activity")});
    search->setPageKeywords(QStringLiteral("editor"),
                            {PhosphorI18n::tr("editor"), PhosphorI18n::tr("layout"), PhosphorI18n::tr("design"),
                             PhosphorI18n::tr("zones")});
    search->setPageKeywords(QStringLiteral("about"),
                            {PhosphorI18n::tr("about"), PhosphorI18n::tr("version"), PhosphorI18n::tr("license"),
                             PhosphorI18n::tr("credits")});

    // ── Addressable anchors ──────────────────────────────────────────────
    // Paired with the GeneralPage reveal pilot (searchAnchor tags in QML), so a
    // result deep-links to the exact row and pulse-highlights it.
    const QString general = PhosphorI18n::tr("General");
    addSection(search, QStringLiteral("general"), QStringLiteral("rendering"), PhosphorI18n::tr("Rendering"), general);
    addSetting(search, QStringLiteral("general"), QStringLiteral("renderingBackend"),
               PhosphorI18n::tr("Rendering backend"), general,
               {PhosphorI18n::tr("opengl"), PhosphorI18n::tr("vulkan"), PhosphorI18n::tr("graphics")});
    addSection(search, QStringLiteral("general"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window filtering"), general);
    addSetting(search, QStringLiteral("general"), QStringLiteral("excludeTransient"),
               PhosphorI18n::tr("Exclude transient windows"), general,
               {PhosphorI18n::tr("dialog"), PhosphorI18n::tr("popup"), PhosphorI18n::tr("tooltip")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("resetDefaults"),
               PhosphorI18n::tr("Reset to Defaults"), general);

    // ── Section anchors ──────────────────────────────────────────────────
    // Jump to a card on its page; paired with searchAnchor tags on those
    // SettingsCards. (Behaviour pages first; the rest grow page-by-page.)
    const QString snapOverlay = PhosphorI18n::tr("Snapping › Overlay");
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("triggers"),
               PhosphorI18n::tr("Triggers"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("zoneSpan"),
               PhosphorI18n::tr("Zone Span"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("display"),
               PhosphorI18n::tr("Display"), snapOverlay);

    const QString snapWindow = PhosphorI18n::tr("Snapping › Window");
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("snapAssist"),
               PhosphorI18n::tr("Snap Assist"), snapWindow);
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window Handling"), snapWindow);
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"),
               snapWindow);

    const QString tilingWindow = PhosphorI18n::tr("Tiling › Window");
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("triggers"), PhosphorI18n::tr("Triggers"),
               tilingWindow);
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window Handling"), tilingWindow);
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"),
               tilingWindow);

    // ── Setting + section anchors: appearance / algorithm / behaviour rows ──
    // Snapping › Overlay (appearance)
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("colors"),
               PhosphorI18n::tr("Colors"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("opacity"),
               PhosphorI18n::tr("Opacity"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("border"),
               PhosphorI18n::tr("Border"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("zoneLabels"),
               PhosphorI18n::tr("Zone Labels"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("effects"),
               PhosphorI18n::tr("Effects"), snapOverlay);
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("shaderEffects"),
               PhosphorI18n::tr("Shader Effects"), snapOverlay);
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("systemAccentColor"),
               PhosphorI18n::tr("System accent color"), snapOverlay,
               {PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("highlightColor"),
               PhosphorI18n::tr("Highlight color"), snapOverlay,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("active"), PhosphorI18n::tr("hover")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveColor"),
               PhosphorI18n::tr("Inactive color"), snapOverlay,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderColor"),
               PhosphorI18n::tr("Border color"), snapOverlay,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("outline")});
    addSetting(
        search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("importColors"),
        PhosphorI18n::tr("Import colors"), snapOverlay,
        {PhosphorI18n::tr("pywal"), PhosphorI18n::tr("json"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("load")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("activeOpacity"),
               PhosphorI18n::tr("Active opacity"), snapOverlay,
               {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveOpacity"),
               PhosphorI18n::tr("Inactive opacity"), snapOverlay,
               {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), snapOverlay,
               {PhosphorI18n::tr("thickness"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderRadius"),
               PhosphorI18n::tr("Border radius"), snapOverlay,
               {PhosphorI18n::tr("rounding"), PhosphorI18n::tr("corner")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("labelColor"),
               PhosphorI18n::tr("Label color"), snapOverlay,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("text"), PhosphorI18n::tr("font")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("font"), PhosphorI18n::tr("Font"),
               snapOverlay, {PhosphorI18n::tr("typeface"), PhosphorI18n::tr("family"), PhosphorI18n::tr("style")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("labelScale"),
               PhosphorI18n::tr("Label scale"), snapOverlay,
               {PhosphorI18n::tr("size"), PhosphorI18n::tr("text"), PhosphorI18n::tr("multiplier")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("blurBehindZones"),
               PhosphorI18n::tr("Blur behind zones"), snapOverlay,
               {PhosphorI18n::tr("frost"), PhosphorI18n::tr("background")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("zoneNumbers"),
               PhosphorI18n::tr("Zone numbers"), snapOverlay,
               {PhosphorI18n::tr("index"), PhosphorI18n::tr("digit"), PhosphorI18n::tr("label")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("flashOnLayoutSwitch"),
               PhosphorI18n::tr("Flash on layout switch"), snapOverlay,
               {PhosphorI18n::tr("blink"), PhosphorI18n::tr("animation")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("frameRate"),
               PhosphorI18n::tr("Frame rate"), snapOverlay,
               {PhosphorI18n::tr("fps"), PhosphorI18n::tr("refresh"), PhosphorI18n::tr("animation")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("audioSpectrum"),
               PhosphorI18n::tr("Audio spectrum"), snapOverlay,
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("music"), PhosphorI18n::tr("visualizer"),
                PhosphorI18n::tr("sound")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("spectrumBars"),
               PhosphorI18n::tr("Spectrum bars"), snapOverlay,
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("bands"), PhosphorI18n::tr("frequency")});

    // Snapping › Window (appearance)
    addSection(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("colors"),
               PhosphorI18n::tr("Colors"), snapWindow);
    addSection(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("decorations"),
               PhosphorI18n::tr("Decorations"), snapWindow);
    addSection(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("borders"),
               PhosphorI18n::tr("Borders"), snapWindow);
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("useSystemAccentColor"),
               PhosphorI18n::tr("Use system accent color"), snapWindow,
               {PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("activeBorderColor"),
               PhosphorI18n::tr("Active border color"), snapWindow,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("focused"), PhosphorI18n::tr("outline")});
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("inactiveBorderColor"),
               PhosphorI18n::tr("Inactive border color"), snapWindow,
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused"), PhosphorI18n::tr("outline")});
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("hideTitleBars"),
               PhosphorI18n::tr("Hide title bars"), snapWindow,
               {PhosphorI18n::tr("titlebar"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("header")});
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), snapWindow, {PhosphorI18n::tr("thickness"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("snapping-window-appearance"), QStringLiteral("cornerRadius"),
               PhosphorI18n::tr("Corner radius"), snapWindow,
               {PhosphorI18n::tr("rounding"), PhosphorI18n::tr("border")});

    // Snapping › Overlay (behaviour rows)
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("activateOnEveryDrag"),
               PhosphorI18n::tr("Activate on every drag"), snapOverlay,
               {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("trigger")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("holdToActivate"),
               PhosphorI18n::tr("Hold to activate"), snapOverlay,
               {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("deactivate")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("triggersToggleMode"),
               PhosphorI18n::tr("Toggle mode"), snapOverlay, {PhosphorI18n::tr("tap"), PhosphorI18n::tr("activation")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("spanModifier"),
               PhosphorI18n::tr("Span modifier"), snapOverlay,
               {PhosphorI18n::tr("zone span"), PhosphorI18n::tr("paint")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("zoneSpanToggleMode"),
               PhosphorI18n::tr("Toggle mode"), snapOverlay, {PhosphorI18n::tr("span"), PhosphorI18n::tr("tap")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("edgeThreshold"),
               PhosphorI18n::tr("Edge threshold"), snapOverlay,
               {PhosphorI18n::tr("distance"), PhosphorI18n::tr("multi-zone")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("showZonesOnAllMonitors"),
               PhosphorI18n::tr("Show zones on all monitors"), snapOverlay,
               {PhosphorI18n::tr("display"), PhosphorI18n::tr("screens")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("filterByAspectRatio"),
               PhosphorI18n::tr("Filter by aspect ratio"), snapOverlay,
               {PhosphorI18n::tr("layouts"), PhosphorI18n::tr("monitor")});

    // Snapping › Window (behaviour rows)
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("alwaysShowAfterSnapping"),
               PhosphorI18n::tr("Always show after snapping"), snapWindow,
               {PhosphorI18n::tr("snap assist"), PhosphorI18n::tr("picker")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("holdToEnable"),
               PhosphorI18n::tr("Hold to enable"), snapWindow,
               {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("snap assist")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("reSnapOnResolutionChange"),
               PhosphorI18n::tr("Re-snap on resolution change"), snapWindow,
               {PhosphorI18n::tr("resolution"), PhosphorI18n::tr("display")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("openNewWindowsInLastUsedZone"),
               PhosphorI18n::tr("Open new windows in the last-used zone"), snapWindow,
               {PhosphorI18n::tr("new window"), PhosphorI18n::tr("last zone")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("autoAssignNewWindowsAllLayouts"),
               PhosphorI18n::tr("Auto-assign new windows for all layouts"), snapWindow,
               {PhosphorI18n::tr("auto-assign"), PhosphorI18n::tr("layouts")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreSizeOnUnsnap"),
               PhosphorI18n::tr("Restore size on unsnap"), snapWindow,
               {PhosphorI18n::tr("unsnap"), PhosphorI18n::tr("original size")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreWindowsToPreviousZone"),
               PhosphorI18n::tr("Restore windows to their previous zone"), snapWindow,
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("login")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreUnsnappedWindowsPosition"),
               PhosphorI18n::tr("Restore unsnapped windows to their previous position"), snapWindow,
               {PhosphorI18n::tr("floated"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("unfloatToZoneFallback"),
               PhosphorI18n::tr("Unfloat to a zone when there is no previous zone"), snapWindow,
               {PhosphorI18n::tr("unfloat"), PhosphorI18n::tr("fallback")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), snapWindow,
               {PhosphorI18n::tr("all desktops"), PhosphorI18n::tr("sticky")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), snapWindow,
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), snapWindow,
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});

    // Tiling › Appearance (incl. shared GapsSettingsCard)
    const QString tilingApp = PhosphorI18n::tr("Tiling › Appearance");
    addSection(search, QStringLiteral("tiling-appearance"), QStringLiteral("colors"), PhosphorI18n::tr("Colors"),
               tilingApp);
    addSection(search, QStringLiteral("tiling-appearance"), QStringLiteral("decorations"),
               PhosphorI18n::tr("Decorations"), tilingApp);
    addSection(search, QStringLiteral("tiling-appearance"), QStringLiteral("borders"), PhosphorI18n::tr("Borders"),
               tilingApp);
    addSection(search, QStringLiteral("tiling-appearance"), QStringLiteral("gaps"), PhosphorI18n::tr("Gaps"),
               tilingApp);
    addSetting(
        search, QStringLiteral("tiling-appearance"), QStringLiteral("useSystemAccentColor"),
        PhosphorI18n::tr("Use system accent color"), tilingApp,
        {PhosphorI18n::tr("color"), PhosphorI18n::tr("accent"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("border")});
    addSetting(
        search, QStringLiteral("tiling-appearance"), QStringLiteral("activeBorderColor"),
        PhosphorI18n::tr("Active border color"), tilingApp,
        {PhosphorI18n::tr("color"), PhosphorI18n::tr("border"), PhosphorI18n::tr("focus"), PhosphorI18n::tr("active")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("inactiveBorderColor"),
               PhosphorI18n::tr("Inactive border color"), tilingApp,
               {PhosphorI18n::tr("color"), PhosphorI18n::tr("border"), PhosphorI18n::tr("inactive"),
                PhosphorI18n::tr("unfocused")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("hideTitleBars"),
               PhosphorI18n::tr("Hide title bars"), tilingApp,
               {PhosphorI18n::tr("titlebar"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("header")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), tilingApp,
               {PhosphorI18n::tr("border"), PhosphorI18n::tr("width"), PhosphorI18n::tr("thickness")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("cornerRadius"),
               PhosphorI18n::tr("Corner radius"), tilingApp,
               {PhosphorI18n::tr("corner"), PhosphorI18n::tr("radius"), PhosphorI18n::tr("rounded")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("primaryGap"), PhosphorI18n::tr("Inner gap"),
               tilingApp,
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("inner")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("outerGap"), PhosphorI18n::tr("Outer gap"),
               tilingApp,
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("outer"),
                PhosphorI18n::tr("edge")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("perSideOuterGaps"),
               PhosphorI18n::tr("Per-side outer gaps"), tilingApp,
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("edge"),
                PhosphorI18n::tr("side")});
    addSetting(search, QStringLiteral("tiling-appearance"), QStringLiteral("smartGaps"), PhosphorI18n::tr("Smart gaps"),
               tilingApp,
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("smart"), PhosphorI18n::tr("single")});

    // Tiling › Algorithm
    const QString tilingAlgo = PhosphorI18n::tr("Tiling › Algorithm");
    addSection(search, QStringLiteral("tiling-algorithm"), QStringLiteral("algorithm"), PhosphorI18n::tr("Algorithm"),
               tilingAlgo);
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("maxWindows"),
               PhosphorI18n::tr("Max windows"), tilingAlgo,
               {PhosphorI18n::tr("windows"), PhosphorI18n::tr("maximum"), PhosphorI18n::tr("count"),
                PhosphorI18n::tr("limit")});
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("masterRatio"),
               PhosphorI18n::tr("Master ratio"), tilingAlgo,
               {PhosphorI18n::tr("master"), PhosphorI18n::tr("center"), PhosphorI18n::tr("ratio"),
                PhosphorI18n::tr("split"), PhosphorI18n::tr("proportion")});
    addSetting(
        search, QStringLiteral("tiling-algorithm"), QStringLiteral("ratioStepSize"),
        PhosphorI18n::tr("Ratio step size"), tilingAlgo,
        {PhosphorI18n::tr("ratio"), PhosphorI18n::tr("step"), PhosphorI18n::tr("increment"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("masterCount"),
               PhosphorI18n::tr("Master count"), tilingAlgo,
               {PhosphorI18n::tr("master"), PhosphorI18n::tr("center"), PhosphorI18n::tr("count"),
                PhosphorI18n::tr("number")});

    // Tiling › Window (behaviour rows)
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("alwaysReinsertOnDrag"),
               PhosphorI18n::tr("Always re-insert on drag"), tilingWindow,
               {PhosphorI18n::tr("stack"), PhosphorI18n::tr("insert")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("holdToReinsert"),
               PhosphorI18n::tr("Hold to re-insert into stack"), tilingWindow,
               {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("stack")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("triggersToggleMode"),
               PhosphorI18n::tr("Toggle mode"), tilingWindow,
               {PhosphorI18n::tr("tap"), PhosphorI18n::tr("stack preview")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("newWindowPlacement"),
               PhosphorI18n::tr("New window placement"), tilingWindow,
               {PhosphorI18n::tr("order"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("respectMinimumSize"),
               PhosphorI18n::tr("Respect minimum size"), tilingWindow,
               {PhosphorI18n::tr("minimum"), PhosphorI18n::tr("gaps")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("restoreUntiledWindowsPosition"),
               PhosphorI18n::tr("Restore untiled windows to their previous position"), tilingWindow,
               {PhosphorI18n::tr("floated"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), tilingWindow,
               {PhosphorI18n::tr("all desktops"), PhosphorI18n::tr("sticky")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("dragBehavior"),
               PhosphorI18n::tr("Drag behavior"), tilingWindow,
               {PhosphorI18n::tr("float"), PhosphorI18n::tr("reorder")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("overflowBehavior"),
               PhosphorI18n::tr("Overflow behavior"), tilingWindow,
               {PhosphorI18n::tr("max windows"), PhosphorI18n::tr("unlimited")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), tilingWindow,
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), tilingWindow,
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});

    // Animations › General
    const QString animGeneral = PhosphorI18n::tr("Animations");
    addSection(search, QStringLiteral("animations-general"), QStringLiteral("globalAnimationDefaults"),
               PhosphorI18n::tr("Global animation defaults"), animGeneral);
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("multipleWindows"),
               PhosphorI18n::tr("Multiple windows"), animGeneral,
               {PhosphorI18n::tr("sequence"), PhosphorI18n::tr("simultaneous"), PhosphorI18n::tr("one by one")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("staggerDelay"),
               PhosphorI18n::tr("Stagger delay"), animGeneral,
               {PhosphorI18n::tr("pause"), PhosphorI18n::tr("interval"), PhosphorI18n::tr("delay")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumDistance"),
               PhosphorI18n::tr("Minimum distance"), animGeneral,
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("skip"), PhosphorI18n::tr("geometry")});
    addSection(search, QStringLiteral("animations-general"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window Filtering"), animGeneral);
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("excludeTransientWindows"),
               PhosphorI18n::tr("Exclude transient windows"), animGeneral,
               {PhosphorI18n::tr("dialogs"), PhosphorI18n::tr("popups"), PhosphorI18n::tr("tooltips"),
                PhosphorI18n::tr("menus")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("excludeNotificationsAndOsds"),
               PhosphorI18n::tr("Exclude notifications and OSDs"), animGeneral,
               {PhosphorI18n::tr("on-screen display"), PhosphorI18n::tr("volume"), PhosphorI18n::tr("brightness")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"), animGeneral,
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"), animGeneral,
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short"), PhosphorI18n::tr("size")});
}

} // namespace PlasmaZones
