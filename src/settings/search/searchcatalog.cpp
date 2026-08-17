// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchcatalog.h"
#include "searchcatalog_p.h"

#include "phosphor_i18n.h"

#include <PhosphorControl/SearchController.h>
#include <PhosphorControl/SearchEntry.h>

#include <QString>
#include <QStringList>

using PhosphorControl::SearchEntry;

namespace PlasmaZones {

// addSetting / addSection live in searchcatalog_p.h, shared with the
// animation-events TU (searchcatalog_animations.cpp).
using SearchCatalogDetail::addSection;
using SearchCatalogDetail::addSetting;

namespace {

void addAction(PhosphorControl::SearchController* search, const QString& actionId, const QString& title,
               const QString& subtitle, const QString& icon, const QStringList& keywords)
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Action;
    e.actionId = actionId;
    e.title = title;
    e.subtitle = subtitle;
    e.icon = icon;
    e.keywords = keywords;
    search->addEntry(e);
}

} // namespace

void seedSearchCatalog(PhosphorControl::SearchController* search)
{
    if (search == nullptr) {
        return;
    }

    // ── App actions ──────────────────────────────────────────────────────
    // Commands, not destinations. Dispatched by id in Main.qml's
    // onActionTriggered — keep the two sides' id vocabulary in sync.
    addAction(search, QStringLiteral("show-shortcut-overlay"), PhosphorI18n::tr("Keyboard Shortcuts"),
              PhosphorI18n::tr("Show the shortcut reference for this window"), QStringLiteral("input-keyboard"),
              {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("shortcuts"), PhosphorI18n::tr("hotkey"),
               PhosphorI18n::tr("keybinding"), PhosphorI18n::tr("keys"), PhosphorI18n::tr("cheatsheet"),
               PhosphorI18n::tr("help")});

    // ── Per-page synonyms ────────────────────────────────────────────────
    // Page entries are auto-derived from the registry; these add search terms a
    // user is likely to type that don't appear in the page title. Literal
    // PhosphorI18n::tr calls (not a wrapper) so `update-ts` extracts them.
    // The condensed simple pages need their own synonym lists: in simple
    // mode their advanced twins are filtered out of the index along with
    // those pages' keywords, so without these a search for "trigger" or
    // "easing" matches no page at all.
    search->setPageKeywords(QStringLiteral("snapping-simple"),
                            {PhosphorI18n::tr("snap"), PhosphorI18n::tr("zones"), PhosphorI18n::tr("trigger"),
                             PhosphorI18n::tr("magnet"), PhosphorI18n::tr("drag")});
    search->setPageKeywords(QStringLiteral("tiling-simple"),
                            {PhosphorI18n::tr("tile"), PhosphorI18n::tr("algorithm"), PhosphorI18n::tr("master"),
                             PhosphorI18n::tr("bsp"), PhosphorI18n::tr("grid"), PhosphorI18n::tr("autotile")});
    search->setPageKeywords(QStringLiteral("scrolling-simple"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("column"),
                             PhosphorI18n::tr("strip"),
                             // Proper noun, deliberately untranslated (see the
                             // scrolling-window keywords).
                             QStringLiteral("niri")});
    search->setPageKeywords(QStringLiteral("animations-simple"),
                            {PhosphorI18n::tr("animation"), PhosphorI18n::tr("motion"), PhosphorI18n::tr("easing"),
                             PhosphorI18n::tr("duration"), PhosphorI18n::tr("effect")});
    search->setPageKeywords(QStringLiteral("overview"),
                            {PhosphorI18n::tr("monitor"), PhosphorI18n::tr("display"), PhosphorI18n::tr("mode"),
                             PhosphorI18n::tr("active layout")});
    search->setPageKeywords(QStringLiteral("general"),
                            {PhosphorI18n::tr("rendering"), PhosphorI18n::tr("backend"), PhosphorI18n::tr("opengl"),
                             PhosphorI18n::tr("vulkan"), PhosphorI18n::tr("gpu"), PhosphorI18n::tr("graphics card"),
                             PhosphorI18n::tr("osd"), PhosphorI18n::tr("backup"), PhosphorI18n::tr("export"),
                             PhosphorI18n::tr("import"), PhosphorI18n::tr("reset")});
    search->setPageKeywords(QStringLiteral("virtualscreens"),
                            {PhosphorI18n::tr("split"), PhosphorI18n::tr("subdivide"), PhosphorI18n::tr("region"),
                             PhosphorI18n::tr("monitor")});
    // The three per-mode library pages (the old tabbed Layouts page, split).
    // Each hosts the shared LayoutBrowserPage, whose LayoutManageCard
    // (import / open folder) carries the manageLayouts anchor on every view.
    // Each list carries its own mode's words: every other leaf of a mode
    // section does, so without them a "snapping" / "tiling" / "scrolling"
    // query reached the mode's config pages but never its library.
    search->setPageKeywords(QStringLiteral("snapping-layouts"),
                            {PhosphorI18n::tr("layout"), PhosphorI18n::tr("zone"), PhosphorI18n::tr("grid"),
                             PhosphorI18n::tr("preset"), PhosphorI18n::tr("aspect ratio"), PhosphorI18n::tr("snap"),
                             PhosphorI18n::tr("snapping")});
    addSection(search, QStringLiteral("snapping-layouts"), QStringLiteral("manageLayouts"),
               PhosphorI18n::tr("User layouts"));
    search->setPageKeywords(QStringLiteral("tiling-library"),
                            {PhosphorI18n::tr("algorithm"), PhosphorI18n::tr("script"), PhosphorI18n::tr("luau"),
                             PhosphorI18n::tr("autotile"), PhosphorI18n::tr("capability"), PhosphorI18n::tr("tile"),
                             PhosphorI18n::tr("tiling"), PhosphorI18n::tr("library")});
    addSection(search, QStringLiteral("tiling-library"), QStringLiteral("manageLayouts"),
               PhosphorI18n::tr("User algorithms"));
    search->setPageKeywords(QStringLiteral("scrolling-templates"),
                            {PhosphorI18n::tr("template"), PhosphorI18n::tr("column"), PhosphorI18n::tr("width"),
                             PhosphorI18n::tr("strip"), PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling")});
    // "User templates", matching the card title in LayoutManageCard.qml — the
    // catalog's own rule keeps section titles identical to the QML they open.
    addSection(search, QStringLiteral("scrolling-templates"), QStringLiteral("manageLayouts"),
               PhosphorI18n::tr("User templates"));

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
    search->setPageKeywords(QStringLiteral("snapping-ordering"),
                            {PhosphorI18n::tr("snap"), PhosphorI18n::tr("snapping"), PhosphorI18n::tr("layout"),
                             PhosphorI18n::tr("priority"), PhosphorI18n::tr("order"), PhosphorI18n::tr("precedence")});
    search->setPageKeywords(QStringLiteral("snapping-shortcuts"),
                            {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("hotkey"), PhosphorI18n::tr("keybind"),
                             PhosphorI18n::tr("keyboard"), PhosphorI18n::tr("key")});
    search->setPageKeywords(QStringLiteral("snapping-shaders"),
                            {PhosphorI18n::tr("shader"), PhosphorI18n::tr("effect"), PhosphorI18n::tr("glow")});

    // Tiling & scrolling
    search->setPageKeywords(QStringLiteral("tiling-behavior"),
                            {PhosphorI18n::tr("tile"), PhosphorI18n::tr("tiling"), PhosphorI18n::tr("auto"),
                             PhosphorI18n::tr("gap"), PhosphorI18n::tr("spacing")});
    search->setPageKeywords(QStringLiteral("tiling-algorithm"),
                            {PhosphorI18n::tr("algorithm"), PhosphorI18n::tr("bsp"), PhosphorI18n::tr("binary"),
                             PhosphorI18n::tr("spiral"), PhosphorI18n::tr("master"), PhosphorI18n::tr("stack")});
    search->setPageKeywords(QStringLiteral("scrolling-columns"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("column"),
                             PhosphorI18n::tr("width"), PhosphorI18n::tr("preset"), PhosphorI18n::tr("tab"),
                             PhosphorI18n::tr("template")});
    // "tab" is deliberately on BOTH scrolling pages: the Columns page decides
    // which columns open tabbed, this one decides how a tabbed column is
    // marked, and a user searching "tab" wants to be offered both.
    search->setPageKeywords(QStringLiteral("scrolling-tabs"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("tab"),
                             PhosphorI18n::tr("indicator"), PhosphorI18n::tr("color"), PhosphorI18n::tr("urgent"),
                             // Proper noun, deliberately untranslated (see the
                             // scrolling-window keywords).
                             QStringLiteral("niri")});
    search->setPageKeywords(QStringLiteral("scrolling-zoneselector"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("strip"),
                             PhosphorI18n::tr("selector"), PhosphorI18n::tr("picker"), PhosphorI18n::tr("popup"),
                             PhosphorI18n::tr("drag")});
    search->setPageKeywords(QStringLiteral("scrolling-window"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("window"),
                             PhosphorI18n::tr("strip"), PhosphorI18n::tr("focus"), PhosphorI18n::tr("center"),
                             PhosphorI18n::tr("wheel"), PhosphorI18n::tr("drag"), PhosphorI18n::tr("insert"),
                             PhosphorI18n::tr("trigger"), PhosphorI18n::tr("modifier"),
                             // Proper noun (the upstream compositor), deliberately not translated —
                             // the one exception to this section's tr-for-extraction rule.
                             QStringLiteral("niri")});
    search->setPageKeywords(QStringLiteral("tiling-ordering"),
                            {PhosphorI18n::tr("tile"), PhosphorI18n::tr("tiling"), PhosphorI18n::tr("algorithm"),
                             PhosphorI18n::tr("priority"), PhosphorI18n::tr("order"), PhosphorI18n::tr("precedence")});
    search->setPageKeywords(QStringLiteral("tiling-shortcuts"),
                            {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("hotkey"), PhosphorI18n::tr("keybind"),
                             PhosphorI18n::tr("keyboard"), PhosphorI18n::tr("key")});
    search->setPageKeywords(QStringLiteral("scrolling-ordering"),
                            {PhosphorI18n::tr("scroll"), PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("priority"),
                             PhosphorI18n::tr("order"), PhosphorI18n::tr("precedence"), PhosphorI18n::tr("template")});
    search->setPageKeywords(QStringLiteral("scrolling-shortcuts"),
                            {PhosphorI18n::tr("shortcut"), PhosphorI18n::tr("hotkey"), PhosphorI18n::tr("keybind"),
                             PhosphorI18n::tr("keyboard"), PhosphorI18n::tr("key"), PhosphorI18n::tr("template")});

    // Animations
    search->setPageKeywords(QStringLiteral("animations-general"),
                            {PhosphorI18n::tr("animation"), PhosphorI18n::tr("duration"), PhosphorI18n::tr("easing"),
                             PhosphorI18n::tr("curve"), PhosphorI18n::tr("spring"), PhosphorI18n::tr("speed")});
    search->setPageKeywords(QStringLiteral("animations-windows"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("animation"), PhosphorI18n::tr("appearance"),
                             PhosphorI18n::tr("open"), PhosphorI18n::tr("close"), PhosphorI18n::tr("minimize"),
                             PhosphorI18n::tr("focus")});
    search->setPageKeywords(QStringLiteral("animations-window-motion"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("movement"), PhosphorI18n::tr("motion"),
                             PhosphorI18n::tr("snap"), PhosphorI18n::tr("maximize")});
    search->setPageKeywords(QStringLiteral("animations-window-dragging"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("drag"), PhosphorI18n::tr("dragging"),
                             PhosphorI18n::tr("move"), PhosphorI18n::tr("wobble"), PhosphorI18n::tr("physics")});
    search->setPageKeywords(
        QStringLiteral("animations-osds"),
        {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification"), PhosphorI18n::tr("on-screen display")});
    search->setPageKeywords(QStringLiteral("animations-overlays"),
                            {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("popup"), PhosphorI18n::tr("animation")});
    search->setPageKeywords(QStringLiteral("animations-desktops"),
                            {PhosphorI18n::tr("desktop"), PhosphorI18n::tr("virtual desktop"),
                             PhosphorI18n::tr("workspace"), PhosphorI18n::tr("switch"), PhosphorI18n::tr("peek"),
                             PhosphorI18n::tr("show desktop")});
    search->setPageKeywords(QStringLiteral("animations-scrolling"),
                            {PhosphorI18n::tr("scrolling"), PhosphorI18n::tr("strip"), PhosphorI18n::tr("column"),
                             PhosphorI18n::tr("animation"), PhosphorI18n::tr("shader"), PhosphorI18n::tr("blur"),
                             PhosphorI18n::tr("motion blur")});
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

    // Decorations
    search->setPageKeywords(QStringLiteral("decorations-windows"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("border"),
                             PhosphorI18n::tr("surface"), PhosphorI18n::tr("appearance")});
    search->setPageKeywords(
        QStringLiteral("decorations-osds"),
        {PhosphorI18n::tr("osd"), PhosphorI18n::tr("on-screen display"), PhosphorI18n::tr("decoration")});
    search->setPageKeywords(QStringLiteral("decorations-popups"),
                            {PhosphorI18n::tr("popup"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("tooltip")});
    search->setPageKeywords(QStringLiteral("decorations-shell"),
                            {PhosphorI18n::tr("shell"), PhosphorI18n::tr("panel"), PhosphorI18n::tr("plasma"),
                             PhosphorI18n::tr("taskbar"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("applet"),
                             PhosphorI18n::tr("applet popup"), PhosphorI18n::tr("launcher"), PhosphorI18n::tr("tray"),
                             PhosphorI18n::tr("system tray"), PhosphorI18n::tr("dock"), PhosphorI18n::tr("widget")});
    search->setPageKeywords(QStringLiteral("decorations-sets"),
                            {PhosphorI18n::tr("decoration set"), PhosphorI18n::tr("set"), PhosphorI18n::tr("preset"),
                             PhosphorI18n::tr("profile"), PhosphorI18n::tr("pack")});
    search->setPageKeywords(QStringLiteral("decorations-shaders"),
                            {PhosphorI18n::tr("shader"), PhosphorI18n::tr("surface"), PhosphorI18n::tr("pack"),
                             PhosphorI18n::tr("border"), PhosphorI18n::tr("glass"), PhosphorI18n::tr("glow"),
                             PhosphorI18n::tr("blur")});

    // Top-level + tools
    search->setPageKeywords(QStringLiteral("window-appearance"),
                            {PhosphorI18n::tr("window"), PhosphorI18n::tr("border"), PhosphorI18n::tr("color"),
                             PhosphorI18n::tr("title bar"), PhosphorI18n::tr("decoration"),
                             PhosphorI18n::tr("appearance"), PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"),
                             PhosphorI18n::tr("spacing"), PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"),
                             PhosphorI18n::tr("blur"), PhosphorI18n::tr("performance")});
    search->setPageKeywords(QStringLiteral("rules"),
                            {PhosphorI18n::tr("rule"), PhosphorI18n::tr("exclude"), PhosphorI18n::tr("float"),
                             PhosphorI18n::tr("monitor"), PhosphorI18n::tr("priority"), PhosphorI18n::tr("activity")});
    search->setPageKeywords(QStringLiteral("profiles"),
                            {PhosphorI18n::tr("profile"), PhosphorI18n::tr("profiles"), PhosphorI18n::tr("preset"),
                             PhosphorI18n::tr("switch"), PhosphorI18n::tr("import"), PhosphorI18n::tr("export"),
                             PhosphorI18n::tr("inherit"), PhosphorI18n::tr("create"), PhosphorI18n::tr("save")});
    search->setPageKeywords(QStringLiteral("editor"),
                            {PhosphorI18n::tr("editor"), PhosphorI18n::tr("layout"), PhosphorI18n::tr("design"),
                             PhosphorI18n::tr("zones")});
    search->setPageKeywords(QStringLiteral("about"),
                            {PhosphorI18n::tr("about"), PhosphorI18n::tr("version"), PhosphorI18n::tr("license"),
                             PhosphorI18n::tr("credits")});

    // ── Addressable anchors ──────────────────────────────────────────────
    // Each entry pairs with a searchAnchor tag in QML so a result deep-links to
    // the exact card/row and pulse-highlights it. Subtitles are auto-derived from
    // the page hierarchy by SearchController, so none are passed here.
    addSection(search, QStringLiteral("general"), QStringLiteral("onScreenDisplay"),
               PhosphorI18n::tr("On-Screen Display"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("osdOnLayoutSwitch"),
               PhosphorI18n::tr("Layout switch OSD"), {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("osdOnDesktopSwitch"),
               PhosphorI18n::tr("Desktop switch OSD"),
               {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification"), PhosphorI18n::tr("virtual desktop")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("navigationOsd"),
               PhosphorI18n::tr("Keyboard navigation OSD"),
               {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification"), PhosphorI18n::tr("shortcut")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("osdStyle"), PhosphorI18n::tr("OSD style"),
               {PhosphorI18n::tr("osd"), PhosphorI18n::tr("notification")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("overlayDisplayMode"),
               PhosphorI18n::tr("Overlay style"),
               {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("preview"), PhosphorI18n::tr("thumbnail")});
    addSection(search, QStringLiteral("general"), QStringLiteral("rendering"), PhosphorI18n::tr("Rendering"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("gpuDevice"), PhosphorI18n::tr("Rendering device"),
               {PhosphorI18n::tr("gpu"), PhosphorI18n::tr("graphics card"), PhosphorI18n::tr("device")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("renderingBackend"),
               PhosphorI18n::tr("Rendering backend"),
               {PhosphorI18n::tr("opengl"), PhosphorI18n::tr("vulkan"), PhosphorI18n::tr("graphics")});
    addSection(search, QStringLiteral("general"), QStringLiteral("shaderEffects"), PhosphorI18n::tr("Shader Effects"),
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("frameRate"), PhosphorI18n::tr("Frame rate"),
               {PhosphorI18n::tr("fps"), PhosphorI18n::tr("refresh"), PhosphorI18n::tr("animation")},
               /*advancedOnly=*/true);
    // The spectrum-bars row and the whole Audio Analysis card are HIDDEN
    // (not merely disabled) when cava is absent or the toggle is off, so
    // these entries can resolve to an invisible anchor. That is an accepted
    // degradation: the reveal machinery detects the invisible target and
    // reverts any cards it speculatively expanded (SettingsFlickable's
    // pending-cards revert), and the catalog is seeded once at startup while
    // cava availability is a runtime probe, so gating the entries here would
    // need dynamic re-seeding for marginal benefit.
    addSection(search, QStringLiteral("general"), QStringLiteral("audioSpectrum"), PhosphorI18n::tr("Audio Spectrum"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioSpectrumEnabled"),
               PhosphorI18n::tr("Audio spectrum"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("music"), PhosphorI18n::tr("visualizer"),
                PhosphorI18n::tr("sound")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("spectrumBars"), PhosphorI18n::tr("Spectrum bars"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("bands"), PhosphorI18n::tr("frequency")});
    addSection(search, QStringLiteral("general"), QStringLiteral("audioAnalysis"), PhosphorI18n::tr("Audio Analysis"),
               /*advancedOnly=*/true);
    addSetting(
        search, QStringLiteral("general"), QStringLiteral("audioNoiseReduction"), PhosphorI18n::tr("Noise reduction"),
        {PhosphorI18n::tr("cava"), PhosphorI18n::tr("smoothing"), PhosphorI18n::tr("smooth")}, /*advancedOnly=*/true);
    addSetting(
        search, QStringLiteral("general"), QStringLiteral("audioExtraSmoothing"), PhosphorI18n::tr("Extra smoothing"),
        {PhosphorI18n::tr("cava"), PhosphorI18n::tr("smoothing"), PhosphorI18n::tr("smooth")}, /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioAutosens"), PhosphorI18n::tr("Automatic gain"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("autosens"), PhosphorI18n::tr("sensitivity"),
                PhosphorI18n::tr("gain")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioSensitivity"), PhosphorI18n::tr("Sensitivity"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("gain")}, /*advancedOnly=*/true);
    addSetting(
        search, QStringLiteral("general"), QStringLiteral("audioLowerCutoff"), PhosphorI18n::tr("Lowest frequency"),
        {PhosphorI18n::tr("cava"), PhosphorI18n::tr("cutoff"), PhosphorI18n::tr("frequency"), PhosphorI18n::tr("bass")},
        /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioHigherCutoff"),
               PhosphorI18n::tr("Highest frequency"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("cutoff"), PhosphorI18n::tr("frequency"),
                PhosphorI18n::tr("treble")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioChannelMode"), PhosphorI18n::tr("Channels"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("stereo"), PhosphorI18n::tr("mono")}, /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioReverse"), PhosphorI18n::tr("Reverse bar order"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("flip"), PhosphorI18n::tr("mirror")}, /*advancedOnly=*/true);
    addSetting(
        search, QStringLiteral("general"), QStringLiteral("audioMonstercat"), PhosphorI18n::tr("Monstercat filter"),
        {PhosphorI18n::tr("cava"), PhosphorI18n::tr("filter"), PhosphorI18n::tr("smooth")}, /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioWaves"), PhosphorI18n::tr("Wave filter"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("filter"), PhosphorI18n::tr("wave")}, /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioInputMethod"), PhosphorI18n::tr("Audio backend"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("pipewire"), PhosphorI18n::tr("pulseaudio"),
                PhosphorI18n::tr("capture")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioInputSource"), PhosphorI18n::tr("Audio source"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("device"), PhosphorI18n::tr("monitor"),
                PhosphorI18n::tr("capture")},
               /*advancedOnly=*/true);
    addSection(search, QStringLiteral("general"), QStringLiteral("layoutAssignment"),
               PhosphorI18n::tr("Layout assignment"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("suppressDefaultLayoutAssignment"),
               PhosphorI18n::tr("Don't assign a layout by default"),
               {PhosphorI18n::tr("default"), PhosphorI18n::tr("assign"), PhosphorI18n::tr("snapping"),
                PhosphorI18n::tr("tiling")});
    addSection(search, QStringLiteral("general"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window filtering"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("excludeTransient"),
               PhosphorI18n::tr("Exclude transient windows"),
               {PhosphorI18n::tr("dialog"), PhosphorI18n::tr("popup"), PhosphorI18n::tr("tooltip")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("resetDefaults"), PhosphorI18n::tr("Reset"),
               {PhosphorI18n::tr("reset to defaults"), PhosphorI18n::tr("defaults"), PhosphorI18n::tr("restore")});

    // ── Section anchors ──────────────────────────────────────────────────
    // Jump to a card on its page; paired with searchAnchor tags on those
    // SettingsCards.
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("triggers"),
               PhosphorI18n::tr("Triggers"));
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("zoneSpan"),
               PhosphorI18n::tr("Zone span"));
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("display"),
               PhosphorI18n::tr("Display"));

    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("snapAssist"),
               PhosphorI18n::tr("Snap Assist"));
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"));

    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("triggers"), PhosphorI18n::tr("Triggers"));
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"));

    // ── Setting + section anchors: appearance / algorithm / behaviour rows ──
    // Snapping › Overlay (appearance)
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("colors"),
               PhosphorI18n::tr("Colors"));
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("opacity"),
               PhosphorI18n::tr("Opacity"));
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("border"),
               PhosphorI18n::tr("Border"));
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("zoneLabels"),
               PhosphorI18n::tr("Zone labels"));
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("effects"),
               PhosphorI18n::tr("Effects"));
    // The theme/scheme keywords ride the colour rows themselves now that the
    // all-or-nothing "System accent color" switch is gone: each row's Reset
    // is the follow-the-scheme affordance.
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("highlightColor"),
               PhosphorI18n::tr("Highlight color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("active"), PhosphorI18n::tr("hover"),
                PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("accent")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveColor"),
               PhosphorI18n::tr("Inactive color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused"), PhosphorI18n::tr("theme"),
                PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderColor"),
               PhosphorI18n::tr("Border color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("outline"), PhosphorI18n::tr("theme"),
                PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("activeOpacity"),
               PhosphorI18n::tr("Active opacity"), {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveOpacity"),
               PhosphorI18n::tr("Inactive opacity"), {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), {PhosphorI18n::tr("thickness"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderRadius"),
               PhosphorI18n::tr("Corner radius"), {PhosphorI18n::tr("rounding"), PhosphorI18n::tr("border")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("labelColor"),
               PhosphorI18n::tr("Label color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("text"), PhosphorI18n::tr("font"),
                PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("font"), PhosphorI18n::tr("Font"),
               {PhosphorI18n::tr("typeface"), PhosphorI18n::tr("family"), PhosphorI18n::tr("style")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("labelScale"),
               PhosphorI18n::tr("Label scale"),
               {PhosphorI18n::tr("size"), PhosphorI18n::tr("text"), PhosphorI18n::tr("multiplier")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("zoneNumbers"),
               PhosphorI18n::tr("Zone numbers"),
               {PhosphorI18n::tr("index"), PhosphorI18n::tr("digit"), PhosphorI18n::tr("label")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("flashOnLayoutSwitch"),
               PhosphorI18n::tr("Flash on layout switch"), {PhosphorI18n::tr("blink"), PhosphorI18n::tr("animation")});

    // Window Appearance (config-backed Windows.* / Gaps.* page)
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("borders"), PhosphorI18n::tr("Borders"));
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("decorations"),
               PhosphorI18n::tr("Decorations"));
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), {PhosphorI18n::tr("thickness"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("cornerRadius"),
               PhosphorI18n::tr("Corner radius"), {PhosphorI18n::tr("rounding"), PhosphorI18n::tr("border")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("borderScope"),
               PhosphorI18n::tr("Apply borders to"),
               {PhosphorI18n::tr("scope"), PhosphorI18n::tr("which windows"), PhosphorI18n::tr("border")},
               /*advancedOnly=*/true);
    // The accent/scheme keywords ride the colour rows themselves now that the
    // separate "Use system accent color" switches are gone: each row's Reset
    // is the follow-the-scheme affordance, so a search for accent or theme
    // should land on the row.
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("activeBorderColor"),
               PhosphorI18n::tr("Active border color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("focused"), PhosphorI18n::tr("outline"),
                PhosphorI18n::tr("accent"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("inactiveBorderColor"),
               PhosphorI18n::tr("Inactive border color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused"), PhosphorI18n::tr("outline"),
                PhosphorI18n::tr("accent"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("opacityTint"),
               PhosphorI18n::tr("Opacity and tint"));
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("opacityTintScope"),
               PhosphorI18n::tr("Apply opacity and tint to"),
               {PhosphorI18n::tr("scope"), PhosphorI18n::tr("which windows"), PhosphorI18n::tr("transparency")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("windowOpacity"),
               PhosphorI18n::tr("Opacity"),
               {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("translucent"), PhosphorI18n::tr("fade")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("tintStrength"),
               PhosphorI18n::tr("Tint strength"),
               {PhosphorI18n::tr("wash"), PhosphorI18n::tr("blend"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("tintColor"), PhosphorI18n::tr("Tint color"),
               {PhosphorI18n::tr("wash"), PhosphorI18n::tr("colour"), PhosphorI18n::tr("accent"),
                PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("hideTitleBars"),
               PhosphorI18n::tr("Hide title bars"),
               {PhosphorI18n::tr("titlebar"), PhosphorI18n::tr("decoration"), PhosphorI18n::tr("header")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("hideTitleBarsScope"),
               PhosphorI18n::tr("Hide title bars on"),
               {PhosphorI18n::tr("scope"), PhosphorI18n::tr("which windows"), PhosphorI18n::tr("titlebar")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("focusFadeDuration"),
               PhosphorI18n::tr("Focus fade duration"),
               {PhosphorI18n::tr("fade"), PhosphorI18n::tr("unfocused"), PhosphorI18n::tr("dim"),
                PhosphorI18n::tr("cross-fade")},
               /*advancedOnly=*/true);

    // Decoration performance (Decorations.Performance) — the Performance card on
    // the same page. Keyworded for what someone actually types when their fans
    // spin up after engaging a pack: power, battery, gpu, heat.
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("decorationPerformance"),
               PhosphorI18n::tr("Performance"), /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("decorationAnimateFocusedOnly"),
               PhosphorI18n::tr("Animate only the active window"),
               {PhosphorI18n::tr("performance"), PhosphorI18n::tr("power"), PhosphorI18n::tr("battery"),
                PhosphorI18n::tr("gpu"), PhosphorI18n::tr("heat"), PhosphorI18n::tr("focus")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("decorationPauseWhenIdle"),
               PhosphorI18n::tr("Pause while you are away"),
               {PhosphorI18n::tr("performance"), PhosphorI18n::tr("power"), PhosphorI18n::tr("battery"),
                PhosphorI18n::tr("gpu"), PhosphorI18n::tr("heat"), PhosphorI18n::tr("idle")},
               /*advancedOnly=*/true);
    addSetting(
        search, QStringLiteral("window-appearance"), QStringLiteral("decorationIdleTimeout"),
        PhosphorI18n::tr("Idle after"),
        {PhosphorI18n::tr("idle"), PhosphorI18n::tr("timeout"), PhosphorI18n::tr("power"), PhosphorI18n::tr("battery")},
        /*advancedOnly=*/true);
    // The Blur card on the same page — the per-frame cost lever, where the
    // Performance card gates when the chain animates.
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("decorationBlur"), PhosphorI18n::tr("Blur"));
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("decorationBlurQuality"),
               PhosphorI18n::tr("Blur quality"),
               {PhosphorI18n::tr("blur"), PhosphorI18n::tr("quality"), PhosphorI18n::tr("resolution"),
                PhosphorI18n::tr("performance"), PhosphorI18n::tr("gpu"), PhosphorI18n::tr("glass")});

    // Window filtering (Decorations.WindowFiltering) — the shared WindowFilterCard
    // on the Window Appearance page. Same anchors the card emits, mirroring the
    // General and Animations filtering entries.
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window filtering"), /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("excludeTransient"),
               PhosphorI18n::tr("Exclude transient windows"),
               {PhosphorI18n::tr("dialogs"), PhosphorI18n::tr("popups"), PhosphorI18n::tr("menus"),
                PhosphorI18n::tr("border")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow"), PhosphorI18n::tr("size")},
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short"), PhosphorI18n::tr("size")},
               /*advancedOnly=*/true);

    // Gaps (shared inner/outer gap model) — folded onto the Window Appearance
    // page, which edits the same config-backed model.
    addSection(search, QStringLiteral("window-appearance"), QStringLiteral("gaps"), PhosphorI18n::tr("Gaps"));
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("primaryGap"), PhosphorI18n::tr("Inner gap"),
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("inner")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("outerGap"), PhosphorI18n::tr("Outer gap"),
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("outer"),
                PhosphorI18n::tr("edge")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("perSideOuterGaps"),
               PhosphorI18n::tr("Per-side outer gaps"),
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin"), PhosphorI18n::tr("edge"),
                PhosphorI18n::tr("side")});
    // Smart gaps is tiling-only and relocated to the Tiling → Window page.
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("smartGaps"), PhosphorI18n::tr("Smart gaps"),
               {PhosphorI18n::tr("gap"), PhosphorI18n::tr("gaps"), PhosphorI18n::tr("spacing"),
                PhosphorI18n::tr("smart"), PhosphorI18n::tr("single")});

    // Snapping › Overlay (behaviour rows)
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("activateOnEveryDrag"),
               PhosphorI18n::tr("Activate on every drag"), {PhosphorI18n::tr("overlay"), PhosphorI18n::tr("trigger")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("holdToActivate"),
               PhosphorI18n::tr("Hold to activate"), {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("deactivate")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("triggersToggleMode"),
               PhosphorI18n::tr("Toggle mode"), {PhosphorI18n::tr("tap"), PhosphorI18n::tr("activation")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("spanModifier"),
               PhosphorI18n::tr("Span modifier"), {PhosphorI18n::tr("zone span"), PhosphorI18n::tr("paint")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("zoneSpanToggleMode"),
               PhosphorI18n::tr("Zone span toggle mode"), {PhosphorI18n::tr("span"), PhosphorI18n::tr("tap")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("edgeThreshold"),
               PhosphorI18n::tr("Edge threshold"), {PhosphorI18n::tr("distance"), PhosphorI18n::tr("multi-zone")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("showZonesOnAllMonitors"),
               PhosphorI18n::tr("Show zones on all monitors"),
               {PhosphorI18n::tr("display"), PhosphorI18n::tr("screens")});
    addSetting(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("filterByAspectRatio"),
               PhosphorI18n::tr("Filter by aspect ratio"), {PhosphorI18n::tr("layouts"), PhosphorI18n::tr("monitor")});

    // Snapping › Window (behaviour rows)
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("alwaysShowAfterSnapping"),
               PhosphorI18n::tr("Always show after snapping"),
               {PhosphorI18n::tr("snap assist"), PhosphorI18n::tr("picker")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("holdToEnable"),
               PhosphorI18n::tr("Hold to enable"), {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("snap assist")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("reSnapOnResolutionChange"),
               PhosphorI18n::tr("Re-snap on resolution change"),
               {PhosphorI18n::tr("resolution"), PhosphorI18n::tr("display")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("openNewWindowsInLastUsedZone"),
               PhosphorI18n::tr("Open new windows in the last-used zone"),
               {PhosphorI18n::tr("new window"), PhosphorI18n::tr("last zone")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("autoAssignNewWindowsAllLayouts"),
               PhosphorI18n::tr("Auto-assign new windows for all layouts"),
               {PhosphorI18n::tr("auto-assign"), PhosphorI18n::tr("layouts")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreSizeOnUnsnap"),
               PhosphorI18n::tr("Restore size on unsnap"),
               {PhosphorI18n::tr("unsnap"), PhosphorI18n::tr("original size")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreWindowsToPreviousZone"),
               PhosphorI18n::tr("Restore windows to their previous zone"),
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("login")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("restoreUnsnappedWindowsPosition"),
               PhosphorI18n::tr("Restore unsnapped windows to their previous position"),
               {PhosphorI18n::tr("floated"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("unfloatToZoneFallback"),
               PhosphorI18n::tr("Unfloat to a zone when there is no previous zone"),
               {PhosphorI18n::tr("unfloat"), PhosphorI18n::tr("fallback")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), {PhosphorI18n::tr("all desktops"), PhosphorI18n::tr("sticky")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});

    // Tiling › Algorithm
    addSection(search, QStringLiteral("tiling-algorithm"), QStringLiteral("algorithm"), PhosphorI18n::tr("Algorithm"));
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("maxWindows"),
               PhosphorI18n::tr("Max windows"),
               {PhosphorI18n::tr("windows"), PhosphorI18n::tr("maximum"), PhosphorI18n::tr("count"),
                PhosphorI18n::tr("limit")});
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("masterRatio"),
               PhosphorI18n::tr("Master ratio"),
               {PhosphorI18n::tr("master"), PhosphorI18n::tr("center"), PhosphorI18n::tr("ratio"),
                PhosphorI18n::tr("split"), PhosphorI18n::tr("proportion")});
    addSetting(
        search, QStringLiteral("tiling-algorithm"), QStringLiteral("ratioStepSize"),
        PhosphorI18n::tr("Ratio step size"),
        {PhosphorI18n::tr("ratio"), PhosphorI18n::tr("step"), PhosphorI18n::tr("increment"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("tiling-algorithm"), QStringLiteral("masterCount"),
               PhosphorI18n::tr("Master count"),
               {PhosphorI18n::tr("master"), PhosphorI18n::tr("center"), PhosphorI18n::tr("count"),
                PhosphorI18n::tr("number")});

    // Placement › Scrolling. Anchors follow each page's row order. The
    // kind-gated size rows are registered unconditionally: those rows stay
    // visible while disabled (see ScrollingColumnsPage), so a deep link lands
    // on something the user can see and read.
    addSection(search, QStringLiteral("scrolling-columns"), QStringLiteral("newColumns"),
               PhosphorI18n::tr("New columns"));
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultColumnWidthKind"),
               PhosphorI18n::tr("Default width"),
               {PhosphorI18n::tr("width"), PhosphorI18n::tr("column"), PhosphorI18n::tr("proportion"),
                PhosphorI18n::tr("pixels")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultColumnWidthProportion"),
               PhosphorI18n::tr("Proportion of the screen"),
               {PhosphorI18n::tr("width"), PhosphorI18n::tr("proportion"), PhosphorI18n::tr("percent")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultColumnWidthFixed"),
               PhosphorI18n::tr("Fixed width"), {PhosphorI18n::tr("width"), PhosphorI18n::tr("pixels")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultColumnWidthPresetIndex"),
               PhosphorI18n::tr("Preset width"),
               {PhosphorI18n::tr("preset"), PhosphorI18n::tr("width"), PhosphorI18n::tr("index"),
                PhosphorI18n::tr("template")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultColumnDisplay"),
               PhosphorI18n::tr("Open new columns as"),
               {PhosphorI18n::tr("tabbed"), PhosphorI18n::tr("tabs"), PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultWindowHeightKind"),
               PhosphorI18n::tr("Default height"),
               {PhosphorI18n::tr("height"), PhosphorI18n::tr("window"), PhosphorI18n::tr("auto")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultWindowHeightFixed"),
               PhosphorI18n::tr("Fixed height"), {PhosphorI18n::tr("height"), PhosphorI18n::tr("pixels")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("defaultWindowHeightPresetIndex"),
               PhosphorI18n::tr("Preset height"),
               {PhosphorI18n::tr("preset"), PhosphorI18n::tr("height"), PhosphorI18n::tr("index"),
                PhosphorI18n::tr("template")});
    addSection(search, QStringLiteral("scrolling-columns"), QStringLiteral("scrollingPresets"),
               PhosphorI18n::tr("Width and height presets"));
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("presetColumnWidths"),
               PhosphorI18n::tr("Column widths"),
               {PhosphorI18n::tr("preset"), PhosphorI18n::tr("width"), PhosphorI18n::tr("cycle"),
                PhosphorI18n::tr("template")});
    addSetting(search, QStringLiteral("scrolling-columns"), QStringLiteral("presetWindowHeights"),
               PhosphorI18n::tr("Window heights"),
               {PhosphorI18n::tr("preset"), PhosphorI18n::tr("height"), PhosphorI18n::tr("cycle"),
                PhosphorI18n::tr("template")});

    // ── Scrolling → Tabs ──
    // Three sections mirroring the page's three cards, so a search hit lands
    // on the card that owns the row rather than at the top of the page.
    addSection(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicator"),
               PhosphorI18n::tr("Tab indicator"));
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorEnabled"),
               PhosphorI18n::tr("Show the tab indicator"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("strip"), PhosphorI18n::tr("indicator")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorHideWhenSingleTab"),
               PhosphorI18n::tr("Hide it for a single window"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("hide"), PhosphorI18n::tr("single")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorStyle"), PhosphorI18n::tr("Style"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("chips"), PhosphorI18n::tr("bar")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorPosition"),
               PhosphorI18n::tr("Position"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("left"), PhosphorI18n::tr("right"), PhosphorI18n::tr("top"),
                PhosphorI18n::tr("bottom")});

    addSection(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorSizing"),
               PhosphorI18n::tr("Size and spacing"));
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorPlaceWithinColumn"),
               PhosphorI18n::tr("Make room inside the column"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("inside"), PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorGap"), PhosphorI18n::tr("Gap"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("gap")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorWidth"),
               PhosphorI18n::tr("Thickness"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("thickness"), PhosphorI18n::tr("width")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorLength"),
               PhosphorI18n::tr("Length"), {PhosphorI18n::tr("tab"), PhosphorI18n::tr("length")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorGapsBetweenTabs"),
               PhosphorI18n::tr("Gap between tabs"), {PhosphorI18n::tr("tab"), PhosphorI18n::tr("gap")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorFullyRounded"),
               PhosphorI18n::tr("Fully rounded tabs"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("rounded"), PhosphorI18n::tr("pill")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorCornerRadius"),
               PhosphorI18n::tr("Corner radius"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("corner"), PhosphorI18n::tr("radius")});

    addSection(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorColors"),
               PhosphorI18n::tr("Colors"));
    // The three tab colours are theme-fallback rows like the zone colours, so
    // they carry the same theme/scheme vocabulary (plus the British spelling
    // every other colour row has).
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorActiveColor"),
               PhosphorI18n::tr("Active tab"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"),
                PhosphorI18n::tr("active"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorInactiveColor"),
               PhosphorI18n::tr("Inactive tabs"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"),
                PhosphorI18n::tr("inactive"), PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("scrolling-tabs"), QStringLiteral("tabIndicatorUrgentColor"),
               PhosphorI18n::tr("Urgent tab"),
               {PhosphorI18n::tr("tab"), PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"),
                PhosphorI18n::tr("urgent"), PhosphorI18n::tr("attention"), PhosphorI18n::tr("theme"),
                PhosphorI18n::tr("scheme")});

    // Triggers card first, matching visual order on the page, then the drop
    // indicator. The ANCHOR string is what must match the QML searchAnchor
    // VERBATIM — that is what the deep-link reveal resolves; the title is
    // only the search-result label, and a handful of catalog titles
    // deliberately disambiguate rows that share a QML title (the three
    // "Apply to" scopes, for instance). Keeping titles identical to the QML
    // — here ScrollingDragInsertCard.qml and ScrollingDropIndicatorCard.qml
    // — is still the default, so the result reads like the row it opens. No
    // advancedOnly flag: the whole scrolling-window page is AdvancedOnly,
    // same as the unflagged tiling-behavior twins.
    addSection(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingTriggers"),
               PhosphorI18n::tr("Triggers"));
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingAlwaysReinsertOnDrag"),
               PhosphorI18n::tr("Always re-insert on drag"), {PhosphorI18n::tr("strip"), PhosphorI18n::tr("insert")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingHoldToReinsert"),
               PhosphorI18n::tr("Hold to re-insert into strip"),
               {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("strip")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingTriggersToggleMode"),
               PhosphorI18n::tr("Toggle mode"), {PhosphorI18n::tr("tap"), PhosphorI18n::tr("strip preview")});

    // Drop indicator card, directly after Triggers on the page. The master
    // switch lives in the card HEADER (no body row of its own), so this is
    // the section-only shape the Borders and Opacity-and-tint cards use —
    // the section title is what a "drop indicator" search matches.
    addSection(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicator"),
               PhosphorI18n::tr("Drop indicator"));
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicatorColor"),
               PhosphorI18n::tr("Fill color"),
               {PhosphorI18n::tr("drop"), PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"),
                PhosphorI18n::tr("indicator"), PhosphorI18n::tr("fill"), PhosphorI18n::tr("theme"),
                PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicatorOpacity"),
               PhosphorI18n::tr("Fill opacity"),
               {PhosphorI18n::tr("drop"), PhosphorI18n::tr("opacity"), PhosphorI18n::tr("transparency"),
                PhosphorI18n::tr("indicator")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicatorBorderColor"),
               PhosphorI18n::tr("Border color"),
               {PhosphorI18n::tr("drop"), PhosphorI18n::tr("color"), PhosphorI18n::tr("colour"),
                PhosphorI18n::tr("border"), PhosphorI18n::tr("indicator"), PhosphorI18n::tr("theme"),
                PhosphorI18n::tr("scheme")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicatorBorderWidth"),
               PhosphorI18n::tr("Border width"),
               {PhosphorI18n::tr("drop"), PhosphorI18n::tr("border"), PhosphorI18n::tr("width"),
                PhosphorI18n::tr("thickness")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingDropIndicatorBorderRadius"),
               PhosphorI18n::tr("Corner radius"),
               {PhosphorI18n::tr("drop"), PhosphorI18n::tr("radius"), PhosphorI18n::tr("corner"),
                PhosphorI18n::tr("rounding")});

    addSection(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingWindowHandling"),
               PhosphorI18n::tr("Window handling"));
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingNewWindowPlacement"),
               PhosphorI18n::tr("New window placement"),
               {PhosphorI18n::tr("insert"), PhosphorI18n::tr("position"), PhosphorI18n::tr("column"),
                PhosphorI18n::tr("open")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingRespectMinimumSize"),
               PhosphorI18n::tr("Respect minimum size"),
               {PhosphorI18n::tr("minimum"), PhosphorI18n::tr("size"), PhosphorI18n::tr("resize")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingRestoreStripsOnLogin"),
               PhosphorI18n::tr("Restore columns on login"),
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("login"), PhosphorI18n::tr("session"),
                PhosphorI18n::tr("column")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingRestoreFloatedOnLogin"),
               PhosphorI18n::tr("Restore floated windows to their previous position"),
               {PhosphorI18n::tr("restore"), PhosphorI18n::tr("float"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingStickyWindows"),
               PhosphorI18n::tr("Sticky windows"),
               {PhosphorI18n::tr("sticky"), PhosphorI18n::tr("all"), PhosphorI18n::tr("desktops")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingColumnWidthStep"),
               PhosphorI18n::tr("Width adjustment step"),
               {PhosphorI18n::tr("step"), PhosphorI18n::tr("width"), PhosphorI18n::tr("shortcut")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingWindowHeightStep"),
               PhosphorI18n::tr("Height adjustment step"),
               {PhosphorI18n::tr("step"), PhosphorI18n::tr("height"), PhosphorI18n::tr("shortcut")});
    // The Strip direction card, shared with scrolling-simple like the Focus
    // card below, so the pair registers against both hosting page ids.
    addSection(search, QStringLiteral("scrolling-window"), QStringLiteral("stripDirection"),
               PhosphorI18n::tr("Strip direction"));
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("stripAxis"), PhosphorI18n::tr("Direction"),
               {PhosphorI18n::tr("strip"), PhosphorI18n::tr("axis"), PhosphorI18n::tr("vertical"),
                PhosphorI18n::tr("horizontal"), PhosphorI18n::tr("portrait")});
    // The Focus and view card, shared with scrolling-simple below: it absorbed
    // the former View page's viewport rows, so those anchors resolve against
    // both pages that host the card.
    addSection(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingFocus"),
               PhosphorI18n::tr("Focus and view"));
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("centerFocusedColumn"),
               PhosphorI18n::tr("Center the focused column"),
               {PhosphorI18n::tr("center"), PhosphorI18n::tr("focus"), PhosphorI18n::tr("column"),
                PhosphorI18n::tr("scroll")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("alwaysCenterSingleColumn"),
               PhosphorI18n::tr("Center a lone column"),
               {PhosphorI18n::tr("center"), PhosphorI18n::tr("single"), PhosphorI18n::tr("column")});
    addSetting(
        search, QStringLiteral("scrolling-window"), QStringLiteral("cropStraddlers"),
        PhosphorI18n::tr("Crop columns at the screen edge"),
        {PhosphorI18n::tr("crop"), PhosphorI18n::tr("clip"), PhosphorI18n::tr("edge"), PhosphorI18n::tr("cut off")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingFocusNewWindows"),
               PhosphorI18n::tr("Focus new windows"),
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new"), PhosphorI18n::tr("open")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("scrollingFocusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"),
               {PhosphorI18n::tr("focus"), PhosphorI18n::tr("mouse"), PhosphorI18n::tr("hover")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("wheelFocusEnabled"),
               PhosphorI18n::tr("Scroll columns with the mouse wheel"),
               {PhosphorI18n::tr("wheel"), PhosphorI18n::tr("mouse"), PhosphorI18n::tr("scroll")});
    addSetting(search, QStringLiteral("scrolling-window"), QStringLiteral("wheelFocusInverted"),
               PhosphorI18n::tr("Invert wheel direction"),
               {PhosphorI18n::tr("invert"), PhosphorI18n::tr("wheel"), PhosphorI18n::tr("direction")});

    // Tiling › Window (behaviour rows)
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("alwaysReinsertOnDrag"),
               PhosphorI18n::tr("Always re-insert on drag"), {PhosphorI18n::tr("stack"), PhosphorI18n::tr("insert")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("holdToReinsert"),
               PhosphorI18n::tr("Hold to re-insert into stack"),
               {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("stack")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("triggersToggleMode"),
               PhosphorI18n::tr("Toggle mode"), {PhosphorI18n::tr("tap"), PhosphorI18n::tr("stack preview")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("newWindowPlacement"),
               PhosphorI18n::tr("New window placement"), {PhosphorI18n::tr("order"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("respectMinimumSize"),
               PhosphorI18n::tr("Respect minimum size"), {PhosphorI18n::tr("minimum"), PhosphorI18n::tr("gaps")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("restoreUntiledWindowsPosition"),
               PhosphorI18n::tr("Restore untiled windows to their previous position"),
               {PhosphorI18n::tr("floated"), PhosphorI18n::tr("position")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("stickyWindows"),
               PhosphorI18n::tr("Sticky windows"), {PhosphorI18n::tr("all desktops"), PhosphorI18n::tr("sticky")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("dragBehavior"),
               PhosphorI18n::tr("Drag behavior"), {PhosphorI18n::tr("float"), PhosphorI18n::tr("reorder")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("overflowBehavior"),
               PhosphorI18n::tr("Overflow behavior"), {PhosphorI18n::tr("max windows"), PhosphorI18n::tr("unlimited")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("focusNewWindows"),
               PhosphorI18n::tr("Focus new windows"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("new window")});
    addSetting(search, QStringLiteral("tiling-behavior"), QStringLiteral("focusFollowsMouse"),
               PhosphorI18n::tr("Focus follows mouse"), {PhosphorI18n::tr("focus"), PhosphorI18n::tr("pointer")});
    // Condensed simple-mode page anchors, split into their own TU by
    // concern -- this catalog TU had crossed the file-size ceiling (the
    // same split the animation-event anchors took).
    seedSimplePageAnchors(search);

    // Animations › General
    addSection(search, QStringLiteral("animations-general"), QStringLiteral("globalAnimationDefaults"),
               PhosphorI18n::tr("Global animation defaults"));
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("multipleWindows"),
               PhosphorI18n::tr("Multiple windows"),
               {PhosphorI18n::tr("sequence"), PhosphorI18n::tr("simultaneous"), PhosphorI18n::tr("one by one")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("staggerDelay"),
               PhosphorI18n::tr("Stagger delay"),
               {PhosphorI18n::tr("pause"), PhosphorI18n::tr("interval"), PhosphorI18n::tr("delay")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumDistance"),
               PhosphorI18n::tr("Minimum distance"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("skip"), PhosphorI18n::tr("geometry")});
    addSection(search, QStringLiteral("animations-general"), QStringLiteral("windowFiltering"),
               PhosphorI18n::tr("Window filtering"));
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("excludeTransient"),
               PhosphorI18n::tr("Exclude transient windows"),
               {PhosphorI18n::tr("dialogs"), PhosphorI18n::tr("popups"), PhosphorI18n::tr("tooltips"),
                PhosphorI18n::tr("menus")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("excludeNotificationsAndOsds"),
               PhosphorI18n::tr("Exclude notifications and OSDs"),
               {PhosphorI18n::tr("on-screen display"), PhosphorI18n::tr("volume"), PhosphorI18n::tr("brightness")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("animations-general"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"),
               {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short"), PhosphorI18n::tr("size")});

    // ── List / browser page anchors ──────────────────────────────────────
    // General › Configuration (backup / restore / data)
    addSection(search, QStringLiteral("general"), QStringLiteral("configuration"), PhosphorI18n::tr("Configuration"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("backup"), PhosphorI18n::tr("Backup"),
               {PhosphorI18n::tr("export"), PhosphorI18n::tr("save"), PhosphorI18n::tr("data")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("restore"), PhosphorI18n::tr("Restore"),
               {PhosphorI18n::tr("import"), PhosphorI18n::tr("load"), PhosphorI18n::tr("data")});

    // Snapping › Zone Selector
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("zoneSelectorEnabled"),
               PhosphorI18n::tr("Zone selector popup"),
               {PhosphorI18n::tr("enable"), PhosphorI18n::tr("toggle"), PhosphorI18n::tr("picker")});
    addSection(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("positionTrigger"),
               PhosphorI18n::tr("Position and trigger"));
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("triggerDistance"),
               PhosphorI18n::tr("Trigger distance"),
               {PhosphorI18n::tr("edge"), PhosphorI18n::tr("distance"), PhosphorI18n::tr("proximity")});
    addSection(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("layoutArrangement"),
               PhosphorI18n::tr("Layout arrangement"));
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("arrangement"),
               PhosphorI18n::tr("Arrangement"),
               {PhosphorI18n::tr("grid"), PhosphorI18n::tr("horizontal"), PhosphorI18n::tr("vertical")});
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("gridColumns"),
               PhosphorI18n::tr("Grid columns"),
               {PhosphorI18n::tr("columns"), PhosphorI18n::tr("per row"), PhosphorI18n::tr("count")});
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("maxVisibleRows"),
               PhosphorI18n::tr("Max visible rows"),
               {PhosphorI18n::tr("rows"), PhosphorI18n::tr("scroll"), PhosphorI18n::tr("visible")});
    addSection(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("previewSize"),
               PhosphorI18n::tr("Preview size"));

    // Scrolling › Strip Selector. The positionTrigger / triggerDistance /
    // previewSize anchors are DELIBERATELY declared for both selector pages:
    // the cards are shared components hosted by two genuinely distinct
    // settings surfaces, so a search hit navigates to whichever page the user
    // needs. No arrangement/grid/rows entries — the strip popup is a single
    // horizontal row by design.
    addSetting(search, QStringLiteral("scrolling-zoneselector"), QStringLiteral("scrollingZoneSelectorEnabled"),
               PhosphorI18n::tr("Strip selector popup"),
               {PhosphorI18n::tr("enable"), PhosphorI18n::tr("toggle"), PhosphorI18n::tr("picker")});
    addSection(search, QStringLiteral("scrolling-zoneselector"), QStringLiteral("positionTrigger"),
               PhosphorI18n::tr("Position and trigger"));
    addSetting(search, QStringLiteral("scrolling-zoneselector"), QStringLiteral("triggerDistance"),
               PhosphorI18n::tr("Trigger distance"),
               {PhosphorI18n::tr("edge"), PhosphorI18n::tr("distance"), PhosphorI18n::tr("proximity")});
    addSection(search, QStringLiteral("scrolling-zoneselector"), QStringLiteral("previewSize"),
               PhosphorI18n::tr("Preview size"));

    // Ordering (shared OrderingPage) + Quick shortcuts (shared QuickLayoutSlotsCard)
    addSection(search, QStringLiteral("snapping-ordering"), QStringLiteral("ordering"),
               PhosphorI18n::tr("Snapping layout priority"));
    addSection(search, QStringLiteral("tiling-ordering"), QStringLiteral("ordering"),
               PhosphorI18n::tr("Tiling algorithm priority"));
    addSection(search, QStringLiteral("scrolling-ordering"), QStringLiteral("ordering"),
               PhosphorI18n::tr("Scrolling template priority"));
    addSection(search, QStringLiteral("snapping-shortcuts"), QStringLiteral("quickShortcuts"),
               PhosphorI18n::tr("Snapping Quick Shortcuts"));
    addSection(search, QStringLiteral("tiling-shortcuts"), QStringLiteral("quickShortcuts"),
               PhosphorI18n::tr("Tiling Quick Shortcuts"));
    addSection(search, QStringLiteral("scrolling-shortcuts"), QStringLiteral("quickShortcuts"),
               PhosphorI18n::tr("Scrolling Quick Shortcuts"));

    // Shaders (shared ShaderBrowserPage) + animation presets / motion sets /
    // decoration sets. Every page that hosts a ShaderBrowserPage carries its
    // "userShaders" card, so each one registers the anchor.
    addSection(search, QStringLiteral("snapping-shaders"), QStringLiteral("userShaders"),
               PhosphorI18n::tr("User shaders"));
    addSection(search, QStringLiteral("animations-shaders"), QStringLiteral("userShaders"),
               PhosphorI18n::tr("User shaders"));
    addSection(search, QStringLiteral("decorations-shaders"), QStringLiteral("userShaders"),
               PhosphorI18n::tr("User shaders"));
    addSection(search, QStringLiteral("animations-presets"), QStringLiteral("easingPresets"),
               PhosphorI18n::tr("Easing presets"));
    addSection(search, QStringLiteral("animations-presets"), QStringLiteral("springPresets"),
               PhosphorI18n::tr("Spring presets"));
    addSection(search, QStringLiteral("animations-motionsets"), QStringLiteral("saveMotionSet"),
               PhosphorI18n::tr("Save current state"));
    addSection(search, QStringLiteral("animations-motionsets"), QStringLiteral("importMotionSets"),
               PhosphorI18n::tr("User sets"));
    addSection(search, QStringLiteral("animations-motionsets"), QStringLiteral("savedMotionSets"),
               PhosphorI18n::tr("Saved sets"));
    addSection(search, QStringLiteral("decorations-sets"), QStringLiteral("saveDecorationSet"),
               PhosphorI18n::tr("Save current state"));
    addSection(search, QStringLiteral("decorations-sets"), QStringLiteral("importDecorationSets"),
               PhosphorI18n::tr("User sets"));
    addSection(search, QStringLiteral("decorations-sets"), QStringLiteral("savedDecorationSets"),
               PhosphorI18n::tr("Saved sets"));

    // The per-event animation anchors live in their own TU
    // (searchcatalog_animations.cpp) — the single catalog file had crossed
    // the size ceiling.
    seedAnimationEventAnchors(search);

    addSection(search, QStringLiteral("profiles"), QStringLiteral("saveCurrent"),
               PhosphorI18n::tr("Save current settings"));
    addSection(search, QStringLiteral("profiles"), QStringLiteral("importProfile"),
               PhosphorI18n::tr("Import a profile"));
    addSection(search, QStringLiteral("profiles"), QStringLiteral("profilesList"), PhosphorI18n::tr("Profiles"));
}

} // namespace PlasmaZones
