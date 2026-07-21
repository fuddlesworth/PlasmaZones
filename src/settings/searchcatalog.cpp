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

// `advancedOnly` mirrors the `advancedOnly:` declaration on the row or card
// this anchor points at, for anchors whose HOST PAGE shows in both modes. The
// registry's page tier cannot express a per-row tier, so without it a
// simple-mode search offers a result that reveals a collapsed row. The two
// declarations are cross-checked by tests/unit/settings/
// test_search_catalog_tiers.cpp, so they cannot drift apart silently.
void addSetting(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                const QString& title, const QStringList& keywords = {}, bool advancedOnly = false)
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Setting;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.keywords = keywords;
    e.advancedOnly = advancedOnly;
    search->addEntry(e);
}

void addSection(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                const QString& title, bool advancedOnly = false)
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Section;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.advancedOnly = advancedOnly;
    search->addEntry(e);
}

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
    search->setPageKeywords(QStringLiteral("animations-simple"),
                            {PhosphorI18n::tr("animation"), PhosphorI18n::tr("motion"), PhosphorI18n::tr("easing"),
                             PhosphorI18n::tr("duration"), PhosphorI18n::tr("effect")});
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
    // The LayoutManageCard (import / open folder) carries this anchor on both
    // the layouts and the algorithms view of the page.
    addSection(search, QStringLiteral("layouts"), QStringLiteral("manageLayouts"), PhosphorI18n::tr("User layouts"));

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
                             PhosphorI18n::tr("spacing"), PhosphorI18n::tr("padding"), PhosphorI18n::tr("margin")});
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
    addSection(search, QStringLiteral("general"), QStringLiteral("rendering"), PhosphorI18n::tr("Rendering"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("renderingBackend"),
               PhosphorI18n::tr("Rendering backend"),
               {PhosphorI18n::tr("opengl"), PhosphorI18n::tr("vulkan"), PhosphorI18n::tr("graphics")});
    addSection(search, QStringLiteral("general"), QStringLiteral("shaderEffects"), PhosphorI18n::tr("Shader Effects"),
               /*advancedOnly=*/true);
    addSetting(search, QStringLiteral("general"), QStringLiteral("frameRate"), PhosphorI18n::tr("Frame rate"),
               {PhosphorI18n::tr("fps"), PhosphorI18n::tr("refresh"), PhosphorI18n::tr("animation")},
               /*advancedOnly=*/true);
    addSection(search, QStringLiteral("general"), QStringLiteral("audioSpectrum"), PhosphorI18n::tr("Audio Spectrum"));
    addSetting(search, QStringLiteral("general"), QStringLiteral("audioSpectrumEnabled"),
               PhosphorI18n::tr("Audio spectrum"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("music"), PhosphorI18n::tr("visualizer"),
                PhosphorI18n::tr("sound")});
    addSetting(search, QStringLiteral("general"), QStringLiteral("spectrumBars"), PhosphorI18n::tr("Spectrum bars"),
               {PhosphorI18n::tr("cava"), PhosphorI18n::tr("bands"), PhosphorI18n::tr("frequency")});
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
               PhosphorI18n::tr("Zone Span"));
    addSection(search, QStringLiteral("snapping-overlay-behavior"), QStringLiteral("display"),
               PhosphorI18n::tr("Display"));

    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("snapAssist"),
               PhosphorI18n::tr("Snap Assist"));
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window Handling"));
    addSection(search, QStringLiteral("snapping-window-behavior"), QStringLiteral("focus"), PhosphorI18n::tr("Focus"));

    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("triggers"), PhosphorI18n::tr("Triggers"));
    addSection(search, QStringLiteral("tiling-behavior"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window Handling"));
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
               PhosphorI18n::tr("Zone Labels"));
    addSection(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("effects"),
               PhosphorI18n::tr("Effects"));
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("systemAccentColor"),
               PhosphorI18n::tr("System accent color"),
               {PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("highlightColor"),
               PhosphorI18n::tr("Highlight color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("active"), PhosphorI18n::tr("hover")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveColor"),
               PhosphorI18n::tr("Inactive color"), {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderColor"),
               PhosphorI18n::tr("Border color"), {PhosphorI18n::tr("colour"), PhosphorI18n::tr("outline")});
    addSetting(
        search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("importColors"),
        PhosphorI18n::tr("Import colors"),
        {PhosphorI18n::tr("pywal"), PhosphorI18n::tr("json"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("load")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("activeOpacity"),
               PhosphorI18n::tr("Active opacity"), {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("inactiveOpacity"),
               PhosphorI18n::tr("Inactive opacity"), {PhosphorI18n::tr("transparency"), PhosphorI18n::tr("alpha")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderWidth"),
               PhosphorI18n::tr("Border width"), {PhosphorI18n::tr("thickness"), PhosphorI18n::tr("size")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("borderRadius"),
               PhosphorI18n::tr("Border radius"), {PhosphorI18n::tr("rounding"), PhosphorI18n::tr("corner")});
    addSetting(search, QStringLiteral("snapping-overlay-appearance"), QStringLiteral("labelColor"),
               PhosphorI18n::tr("Label color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("text"), PhosphorI18n::tr("font")});
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
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("useSystemAccentColor"),
               PhosphorI18n::tr("Use system accent color"),
               {PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("activeBorderColor"),
               PhosphorI18n::tr("Active border color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("focused"), PhosphorI18n::tr("outline")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("inactiveBorderColor"),
               PhosphorI18n::tr("Inactive border color"),
               {PhosphorI18n::tr("colour"), PhosphorI18n::tr("unfocused"), PhosphorI18n::tr("outline")});
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
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("useSystemAccentTint"),
               PhosphorI18n::tr("Use system accent color for the tint"),
               {PhosphorI18n::tr("theme"), PhosphorI18n::tr("scheme"), PhosphorI18n::tr("colour")});
    addSetting(search, QStringLiteral("window-appearance"), QStringLiteral("tintColor"), PhosphorI18n::tr("Tint color"),
               {PhosphorI18n::tr("wash"), PhosphorI18n::tr("colour"), PhosphorI18n::tr("accent")});
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
               PhosphorI18n::tr("Toggle mode"), {PhosphorI18n::tr("span"), PhosphorI18n::tr("tap")});
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
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("simpleExcludeNotificationsAndOsds"),
               PhosphorI18n::tr("Exclude notifications and OSDs"),
               {PhosphorI18n::tr("on-screen display"), PhosphorI18n::tr("volume"), PhosphorI18n::tr("brightness")});

    // The condensed simple pages host the SAME shared cards their advanced
    // twins do, so every anchor those cards register needs a row against the
    // simple page id too — SearchController drops entries whose host page the
    // active tier hides, which would otherwise make all of these unsearchable
    // in simple mode. Anchors are page-scoped, so the duplicate ids collide
    // with nothing.
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("zoneSpan"), PhosphorI18n::tr("Zone Span"));
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("spanModifier"),
               PhosphorI18n::tr("Span modifier"), {PhosphorI18n::tr("modifier"), PhosphorI18n::tr("multi-zone")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("zoneSpanToggleMode"),
               PhosphorI18n::tr("Toggle mode"), {PhosphorI18n::tr("zone span")});
    addSetting(search, QStringLiteral("snapping-simple"), QStringLiteral("edgeThreshold"),
               PhosphorI18n::tr("Edge threshold"), {PhosphorI18n::tr("distance"), PhosphorI18n::tr("multi-zone")});
    addSection(search, QStringLiteral("snapping-simple"), QStringLiteral("windowHandling"),
               PhosphorI18n::tr("Window Handling"));
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
               PhosphorI18n::tr("Window Handling"));
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
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("minimumWindowWidth"),
               PhosphorI18n::tr("Minimum window width"), {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("narrow")});
    addSetting(search, QStringLiteral("animations-simple"), QStringLiteral("minimumWindowHeight"),
               PhosphorI18n::tr("Minimum window height"), {PhosphorI18n::tr("threshold"), PhosphorI18n::tr("short")});

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
               PhosphorI18n::tr("Position & Trigger"));
    addSetting(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("triggerDistance"),
               PhosphorI18n::tr("Trigger distance"),
               {PhosphorI18n::tr("edge"), PhosphorI18n::tr("distance"), PhosphorI18n::tr("proximity")});
    addSection(search, QStringLiteral("snapping-zoneselector"), QStringLiteral("layoutArrangement"),
               PhosphorI18n::tr("Layout Arrangement"));
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
               PhosphorI18n::tr("Preview Size"));

    // Ordering (shared OrderingPage) + Quick shortcuts (shared QuickLayoutSlotsCard)
    addSection(search, QStringLiteral("snapping-ordering"), QStringLiteral("ordering"),
               PhosphorI18n::tr("Snapping Layout Priority"));
    addSection(search, QStringLiteral("tiling-ordering"), QStringLiteral("ordering"),
               PhosphorI18n::tr("Tiling Algorithm Priority"));
    addSection(search, QStringLiteral("snapping-shortcuts"), QStringLiteral("quickShortcuts"),
               PhosphorI18n::tr("Snapping Quick Shortcuts"));
    addSection(search, QStringLiteral("tiling-shortcuts"), QStringLiteral("quickShortcuts"),
               PhosphorI18n::tr("Tiling Quick Shortcuts"));

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
               PhosphorI18n::tr("Easing Presets"));
    addSection(search, QStringLiteral("animations-presets"), QStringLiteral("springPresets"),
               PhosphorI18n::tr("Spring Presets"));
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

    // Animation events (reveal-tagged on the event list's outer delegates)
    // Windows (appearance) page under Transitions.
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.open"),
               PhosphorI18n::tr("Opened"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.close"),
               PhosphorI18n::tr("Closed"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.minimize"),
               PhosphorI18n::tr("Minimized"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.focus"),
               PhosphorI18n::tr("Focused"));
    // Window Dragging page under Motion — the held-drag leaf lives on its
    // own page (see AnimationsWindowDraggingPage.qml).
    addSetting(search, QStringLiteral("animations-window-dragging"), QStringLiteral("window.movement.move"),
               PhosphorI18n::tr("Dragged"));
    // Windows (movement) page under Motion.
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.maximize"),
               PhosphorI18n::tr("Maximized"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.snapIn"),
               PhosphorI18n::tr("Snapped Into Zone"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.snapOut"),
               PhosphorI18n::tr("Snapped Out of Zone"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.layoutSwitch"),
               PhosphorI18n::tr("Layout Switched"));
    // OSDs page.
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.show"), PhosphorI18n::tr("Shown"));
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.hide"), PhosphorI18n::tr("Hidden"));
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.pop"), PhosphorI18n::tr("Emphasized"));
    // Overlays page.
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.zoneSelector.show"),
               PhosphorI18n::tr("Zone Selector Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.zoneSelector.hide"),
               PhosphorI18n::tr("Zone Selector Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.layoutPicker.show"),
               PhosphorI18n::tr("Layout Picker Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.layoutPicker.hide"),
               PhosphorI18n::tr("Layout Picker Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.snapAssist.show"),
               PhosphorI18n::tr("Snap Assist Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.snapAssist.hide"),
               PhosphorI18n::tr("Snap Assist Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.cheatsheet.show"),
               PhosphorI18n::tr("Shortcut Cheatsheet Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.cheatsheet.hide"),
               PhosphorI18n::tr("Shortcut Cheatsheet Hidden"));
    // Desktop page.
    addSetting(search, QStringLiteral("animations-desktops"), QStringLiteral("desktop.switch"),
               PhosphorI18n::tr("Desktop Switched"));
    // Keywords on this one row, unlike its siblings: the label shares no token
    // with what users actually type for it. "show desktop" is the name of the
    // KWin action, and "peek" is what the settings page calls the state, but
    // neither appears in "Peeked at Desktop" as a searchable stem. The page
    // carries the same keywords, so without these the terms land on the page
    // rather than deep-linking to the row.
    addSetting(search, QStringLiteral("animations-desktops"), QStringLiteral("desktop.peek"),
               PhosphorI18n::tr("Peeked at Desktop"), {PhosphorI18n::tr("peek"), PhosphorI18n::tr("show desktop")});
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.slideIn"),
               PhosphorI18n::tr("Slide In"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.slideOut"),
               PhosphorI18n::tr("Slide Out"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.fadeIn"),
               PhosphorI18n::tr("Fade In"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.fadeOut"),
               PhosphorI18n::tr("Fade Out"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.hover"), PhosphorI18n::tr("Hover"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.press"), PhosphorI18n::tr("Press"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.toggleOn"),
               PhosphorI18n::tr("Toggle On"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.toggleOff"),
               PhosphorI18n::tr("Toggle Off"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgeShow"),
               PhosphorI18n::tr("Show (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgeHide"),
               PhosphorI18n::tr("Hide (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgePulse"),
               PhosphorI18n::tr("Pulse (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.tint"), PhosphorI18n::tr("Tint"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.dim"), PhosphorI18n::tr("Dim"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.fadeIn"),
               PhosphorI18n::tr("Fade In"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.fadeOut"),
               PhosphorI18n::tr("Fade Out"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.reorder"),
               PhosphorI18n::tr("Reorder"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.accordionExpand"),
               PhosphorI18n::tr("Expand (accordion)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.accordionCollapse"),
               PhosphorI18n::tr("Collapse (accordion)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.progress"),
               PhosphorI18n::tr("Progress"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight"),
               PhosphorI18n::tr("Zone Highlight"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight.pop"),
               PhosphorI18n::tr("Zone Highlight: Pop"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight.border"),
               PhosphorI18n::tr("Zone Highlight: Border"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneOverlayFlash"),
               PhosphorI18n::tr("Zone Overlay: Layout-Switch Flash"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("cursor.hover"),
               PhosphorI18n::tr("Cursor Hover"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("cursor.click"),
               PhosphorI18n::tr("Cursor Click"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapIn"),
               PhosphorI18n::tr("Snap Into Zone (Fill Preview)"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapOut"),
               PhosphorI18n::tr("Snap Out of Zone"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapResize"),
               PhosphorI18n::tr("Snap Resize (Drag Preview)"));

    addSection(search, QStringLiteral("profiles"), QStringLiteral("saveCurrent"),
               PhosphorI18n::tr("Save current settings"));
    addSection(search, QStringLiteral("profiles"), QStringLiteral("importProfile"),
               PhosphorI18n::tr("Import a profile"));
    addSection(search, QStringLiteral("profiles"), QStringLiteral("profilesList"), PhosphorI18n::tr("Profiles"));
}

} // namespace PlasmaZones
