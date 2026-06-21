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
               PhosphorI18n::tr("Reset to defaults"), general);

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
}

} // namespace PlasmaZones
