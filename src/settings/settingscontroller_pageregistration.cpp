// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-registration + sidebar topology methods for SettingsController:
//   * buildApplicationController() — wires the PhosphorControl
//     PageRegistry with PlasmaZones' settings pages and sidebar categories
//     (the navigable leaf pages are enumerated in validPageNames()).
//   * What's-New dismissal + last-seen-version state.
//   * Static accessors for the sidebar's parent/child topology
//     (parentPageRedirects, pageGroupChildren, validPageNames) used by
//     isPageDirty() to propagate dirtiness up collapsed categories
//     and by --page CLI / D-Bus selection to redirect virtual parents
//     to a sensible leaf.
//
// Split out of settingscontroller.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController — same class, separate translation
// unit, no API change.

#include "settingscontroller.h"

#include "../config/configdefaults.h"
#include "../phosphor_i18n.h"
#include "pageadapter.h"
#include "settingsstagingdomain.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVersionNumber>

namespace PlasmaZones {

void SettingsController::buildApplicationController()
{
    // No QObject parent — m_app is owned by the unique_ptr declared after
    // the page sub-controllers in the header, so reverse-order member
    // destruction tears it down BEFORE the pages it references.
    m_app = std::make_unique<PhosphorControl::ApplicationController>();

    const QString qmlPrefix = QStringLiteral("qrc:/qt/qml/org/plasmazones/settings/qml/");

    // Two helpers — both delegate to ApplicationController::registerPage:
    //   regPage:     the page already has its own PageController subclass
    //                (the existing m_xxxPage controllers, now PhosphorControl::
    //                PageController-derived).
    //   regVirtual:  no concrete controller — either a drill-down parent /
    //                inline-collapsible category header, or a leaf whose QML
    //                page binds to a Settings property directly (no per-page
    //                controller). Uses PageAdapter as the framework-facing
    //                identity so the registry still gets a stable
    //                PageController*.
    const auto regPage = [this, &qmlPrefix](PhosphorControl::PageController* page, const QString& parentId,
                                            const QString& title, const QString& qmlFile, const QString& icon,
                                            bool collapsible = false, bool divider = false) {
        const QUrl source = qmlFile.isEmpty() ? QUrl() : QUrl(qmlPrefix + qmlFile);
        m_app->registerPage(page, parentId, title, source, icon, collapsible, divider);
    };
    const auto regVirtual = [this, &qmlPrefix](const QString& id, const QString& parentId, const QString& title,
                                               const QString& qmlFile, const QString& icon, bool collapsible = false,
                                               bool divider = false) {
        auto* adapter = new PageAdapter(id, m_app.get());
        const QUrl source = qmlFile.isEmpty() ? QUrl() : QUrl(qmlPrefix + qmlFile);
        m_app->registerPage(adapter, parentId, title, source, icon, collapsible, divider);
    };

    // Top-level entries. The rail reads top → bottom as three blocks split by
    // two dividers: (1) top/global — Overview (status dashboard) + General
    // (global settings); (2) per-feature configuration — Display, Placement,
    // Animations, Window Rules; (3) tools & meta — Editor, About.
    // `divider` flags mark only those two seams (after General, after Window
    // Rules) plus one inside the feature block (after Placement, to set the
    // placement categories apart from Animations/Window Rules).

    // ── Block 1: top / global ──
    regVirtual(QStringLiteral("overview"), QString(), PhosphorI18n::tr("Overview"),
               QStringLiteral("MonitorStatePage.qml"), QStringLiteral("monitor"));
    // General leads near the top (mirrors the Animations section leading with
    // its own "General" child). Divider after it closes the top/global block.
    regPage(m_generalPage, QString(), PhosphorI18n::tr("General"), QStringLiteral("GeneralPage.qml"),
            QStringLiteral("configure"), /*collapsible=*/false, /*divider=*/true);

    // ── Block 2: per-feature configuration ──
    // Appearance leads block 2: a surface-organized hub (see the Appearance
    // children section below). A drill-down parent (collapsible=false, like
    // Animations) — clicking it navigates into the hub rather than expanding an
    // inline dropdown in the main rail. Feature areas (Display / Placement) keep
    // behavior only.
    regVirtual(QStringLiteral("appearance"), QString(), PhosphorI18n::tr("Appearance"), QString(),
               QStringLiteral("preferences-desktop-color"));
    regVirtual(QStringLiteral("display"), QString(), PhosphorI18n::tr("Display"), QString(),
               QStringLiteral("preferences-desktop-display"), /*collapsible=*/true);
    // Placement groups the two placement modes (Snapping / Tiling) as an
    // inline-collapsible category, matching Display. Divider after it (i.e.
    // above Animations) sets the placement categories apart from the
    // Animations / Window Rules pages that follow.
    regVirtual(QStringLiteral("placement"), QString(), PhosphorI18n::tr("Placement"), QString(),
               QStringLiteral("preferences-system-windows"), /*collapsible=*/true, /*divider=*/true);
    // "animations" is a no-QML drill-down parent — register it as a virtual
    // navigation node (like display / placement / snapping / tiling), NOT as
    // m_animationsPage's own id. The AnimationsPageController is the staging
    // controller for animation edits; it carries the distinct id
    // "animations-staging" and is wired into the framework's dirty/apply
    // machinery as a headless domain below. Splitting the two means the nav
    // handle ("animations", redirected to "animations-general") and the
    // staging controller are independently addressable by QML / D-Bus.
    regVirtual(QStringLiteral("animations"), QString(), PhosphorI18n::tr("Animations"), QString(),
               QStringLiteral("media-playback-start"));
    // Headless staging domain — trackDomain() connects dirtyChanged + appends
    // to m_domains so applyAllAsync walks it, exactly as registerPage would
    // have, but without claiming a sidebar/registry id of its own.
    m_app->registerDomain(m_animationsPage);
    // Window Rules is a top-level leaf (its old "Rules" parent retired after
    // the v4 fold left a single rule surface). Divider after it closes the
    // feature block and opens the tools-and-meta block below.
    regPage(m_windowRulesPage, QString(), PhosphorI18n::tr("Window Rules"), QStringLiteral("WindowRulesPage.qml"),
            QStringLiteral("view-list-details"), /*collapsible=*/false, /*divider=*/true);

    // ── Block 3: tools & meta ──
    regPage(m_editorPage, QString(), PhosphorI18n::tr("Editor"), QStringLiteral("EditorPage.qml"),
            QStringLiteral("document-edit"));
    regVirtual(QStringLiteral("about"), QString(), PhosphorI18n::tr("About"), QStringLiteral("AboutPage.qml"),
               QStringLiteral("help-about"));

    // Placement children — the two placement modes. They keep their own
    // drill-down behaviour (collapsible=false) and their inline enable toggles
    // (keyed by pageId "snapping"/"tiling" in Main.qml's trailing delegate);
    // only their parent changed from top-level to "placement".
    regVirtual(QStringLiteral("snapping"), QStringLiteral("placement"), PhosphorI18n::tr("Snapping"), QString(),
               QStringLiteral("view-split-left-right"));
    regVirtual(QStringLiteral("tiling"), QStringLiteral("placement"), PhosphorI18n::tr("Tiling"), QString(),
               QStringLiteral("window-duplicate"));

    // Display children
    regVirtual(QStringLiteral("virtualscreens"), QStringLiteral("display"), PhosphorI18n::tr("Virtual Screens"),
               QStringLiteral("VirtualScreensPage.qml"), QStringLiteral("virtual-desktops"));
    regVirtual(QStringLiteral("layouts"), QStringLiteral("display"), PhosphorI18n::tr("Layouts"),
               QStringLiteral("LayoutsPage.qml"), QStringLiteral("view-grid"));

    // Appearance children — organised by SURFACE, not by feature. Each node page
    // is an EXISTING appearance page re-parented here; its page id is unchanged
    // (tiling-appearance, snapping-window-appearance, snapping-overlay-appearance,
    // snapping-shaders), so --page / D-Bus deep-links keep resolving — only the
    // sidebar location moved. Surface-node grouping mirrors src/config/surfacenodes.h
    // (windows.tiled / windows.snapped; daemon.overlay). The Tiling and Snapping
    // feature areas keep behavior only.
    // Windows appearance folded into the Tiling/Snapping → Window subject pages
    // as appearance sections, so the hub no longer has a Windows node.
    // Daemon Surfaces → OSDs · Zone Overlay · Zone Selector. OSDs and Zone
    // Selector are appearance-only views: OSDs hosts the OSD style moved out of
    // General; Zone Selector embeds ZoneSelectorSection in "appearance" mode
    // (its behavior stays under Snapping → Zone Selector). Both bind existing
    // controllers/Settings, so they are regVirtual.
    regVirtual(QStringLiteral("appearance-daemon"), QStringLiteral("appearance"), PhosphorI18n::tr("Daemon Surfaces"),
               QString(), QStringLiteral("preferences-desktop-display"),
               /*collapsible=*/true);
    regVirtual(QStringLiteral("osd-appearance"), QStringLiteral("appearance-daemon"), PhosphorI18n::tr("OSDs"),
               QStringLiteral("OsdAppearancePage.qml"), QStringLiteral("dialog-information"));
    regVirtual(QStringLiteral("snapping-overlay-appearance"), QStringLiteral("appearance-daemon"),
               PhosphorI18n::tr("Zone Overlay"), QStringLiteral("SnappingOverlayAppearancePage.qml"),
               QStringLiteral("preferences-desktop-color"));
    regVirtual(QStringLiteral("zoneselector-appearance"), QStringLiteral("appearance-daemon"),
               PhosphorI18n::tr("Zone Selector"), QStringLiteral("ZoneSelectorAppearancePage.qml"),
               QStringLiteral("view-choose"));
    // Pack Library — the installed shader-pack browser, a cross-surface library
    // (sibling of Surfaces, not a surface node itself).
    regPage(m_snappingShadersPage.get(), QStringLiteral("appearance"), PhosphorI18n::tr("Pack Library"),
            QStringLiteral("SnappingShadersPage.qml"), QStringLiteral("folder-open"));

    // Snapping children — organised by SUBJECT: the drag Overlay, the edge
    // Zone-Selector popup, and the snapped Window, plus a shared Configuration
    // block. Each subject's appearance moved to the Appearance hub, leaving one
    // behavior page per subject; with the per-subject "Behavior" leaf now alone,
    // the old collapsible Overlay/Window category wrappers are folded away and
    // the leaves promoted directly under Snapping, labelled by subject (Overlay /
    // Window) so the two distinct behavior pages stay disambiguated. The Overlay
    // and Window behavior pages bind snappingBehaviorPage; Zone Selector binds
    // snappingZoneSelectorPage. Per-page dirtiness keys off the ACTIVE page at
    // edit time (see setNeedsSave), so sharing a controller across pages is fine.
    regVirtual(QStringLiteral("snapping-overlay-behavior"), QStringLiteral("snapping"), PhosphorI18n::tr("Overlay"),
               QStringLiteral("SnappingOverlayBehaviorPage.qml"), QStringLiteral("preferences-desktop-color"),
               /*collapsible=*/false, /*divider=*/true);
    // Zone Selector behavior (enable + trigger). Its appearance lives in the
    // Appearance hub (Daemon Surfaces → Zone Selector).
    regVirtual(QStringLiteral("snapping-zoneselector"), QStringLiteral("snapping"), PhosphorI18n::tr("Zone Selector"),
               QStringLiteral("SnappingZoneSelectorPage.qml"), QStringLiteral("view-choose"), /*collapsible=*/false,
               /*divider=*/true);
    // Window: one page combining snapped-window behavior and appearance sections
    // (keeps the snapping-window-behavior id; the appearance controller stays a
    // bound bridge for border/gap bounds, no nav leaf of its own).
    regVirtual(QStringLiteral("snapping-window-behavior"), QStringLiteral("snapping"), PhosphorI18n::tr("Window"),
               QStringLiteral("SnappingWindowPage.qml"), QStringLiteral("preferences-system-windows"),
               /*collapsible=*/false, /*divider=*/true);

    regVirtual(QStringLiteral("snapping-config-cat"), QStringLiteral("snapping"), PhosphorI18n::tr("Configuration"),
               QString(), QStringLiteral("configure"), /*collapsible=*/true);
    regVirtual(QStringLiteral("snapping-ordering"), QStringLiteral("snapping-config-cat"), PhosphorI18n::tr("Priority"),
               QStringLiteral("SnappingOrderingPage.qml"), QStringLiteral("view-sort"));
    regVirtual(QStringLiteral("snapping-shortcuts"), QStringLiteral("snapping-config-cat"),
               PhosphorI18n::tr("Quick Shortcuts"), QStringLiteral("SnappingQuickShortcutsPage.qml"),
               QStringLiteral("bookmark"));
    // Shaders (snapping-shaders) re-parented to the Appearance hub (Pack
    // Library) above.

    // Tiling children — Behavior / Algorithm / Configuration. Tiling has a single
    // behavior surface (no drag-overlay or selector popup to disambiguate), so its
    // Behavior page is a top leaf directly under Tiling rather than wrapped in a
    // single-child Window category. Its appearance moved to the Appearance hub
    // (Windows → Tiled); page ids are unchanged.
    // Window: one page combining tiled-window behavior and appearance as
    // sections (not separate sub-pages), so a tiled-window setting is always one
    // place. Registered via the behavior controller (keeps the tiling-behavior
    // id + its dirty wiring); the appearance controller stays a Q_PROPERTY bridge
    // for border/gap bounds, with no nav leaf of its own. Dirty/apply is global
    // (one SettingsStagingDomain), so no per-controller domain wiring is needed.
    regPage(m_tilingBehaviorPage, QStringLiteral("tiling"), PhosphorI18n::tr("Window"),
            QStringLiteral("TilingWindowPage.qml"), QStringLiteral("preferences-system-windows"),
            /*collapsible=*/false, /*divider=*/true);

    // Algorithm is a top-level leaf under Tiling (no snapping peer — snapping's
    // layout equivalent lives under Display → Layouts). Divider after it sets the
    // mode-specific algorithm apart from the shared Configuration block below.
    regPage(m_tilingAlgorithmPage.get(), QStringLiteral("tiling"), PhosphorI18n::tr("Algorithm"),
            QStringLiteral("TilingAlgorithmPage.qml"), QStringLiteral("view-grid"), /*collapsible=*/false,
            /*divider=*/true);

    regVirtual(QStringLiteral("tiling-config-cat"), QStringLiteral("tiling"), PhosphorI18n::tr("Configuration"),
               QString(), QStringLiteral("configure"), /*collapsible=*/true);
    regVirtual(QStringLiteral("tiling-ordering"), QStringLiteral("tiling-config-cat"), PhosphorI18n::tr("Priority"),
               QStringLiteral("TilingOrderingPage.qml"), QStringLiteral("view-sort"));
    regVirtual(QStringLiteral("tiling-shortcuts"), QStringLiteral("tiling-config-cat"),
               PhosphorI18n::tr("Quick Shortcuts"), QStringLiteral("TilingQuickShortcutsPage.qml"),
               QStringLiteral("bookmark"));

    // Animations children — Surfaces / Library categories drill in.
    regVirtual(QStringLiteral("animations-general"), QStringLiteral("animations"), PhosphorI18n::tr("General"),
               QStringLiteral("AnimationsGeneralPage.qml"), QStringLiteral("configure"), /*collapsible=*/false,
               /*divider=*/true);
    regVirtual(QStringLiteral("animations-surfaces"), QStringLiteral("animations"), PhosphorI18n::tr("Surfaces"),
               QString(), QStringLiteral("preferences-desktop-multimedia"), /*collapsible=*/true);
    regVirtual(QStringLiteral("animations-library"), QStringLiteral("animations"), PhosphorI18n::tr("Library"),
               QString(), QStringLiteral("folder-open"), /*collapsible=*/true);

    regVirtual(QStringLiteral("animations-windows"), QStringLiteral("animations-surfaces"), PhosphorI18n::tr("Windows"),
               QStringLiteral("AnimationsWindowsPage.qml"), QStringLiteral("window-new"));
    regVirtual(QStringLiteral("animations-osds"), QStringLiteral("animations-surfaces"), PhosphorI18n::tr("OSDs"),
               QStringLiteral("AnimationsOsdsPage.qml"), QStringLiteral("dialog-information"));
    regVirtual(QStringLiteral("animations-overlays"), QStringLiteral("animations-surfaces"),
               PhosphorI18n::tr("Overlays"), QStringLiteral("AnimationsOverlaysPage.qml"),
               QStringLiteral("view-presentation"));
    regVirtual(QStringLiteral("animations-side-panels"), QStringLiteral("animations-surfaces"),
               PhosphorI18n::tr("Side Panels"), QStringLiteral("AnimationsSidePanelsPage.qml"),
               QStringLiteral("sidebar-collapse-symbolic"));
    regVirtual(QStringLiteral("animations-widgets"), QStringLiteral("animations-surfaces"), PhosphorI18n::tr("Widgets"),
               QStringLiteral("AnimationsWidgetsPage.qml"), QStringLiteral("preferences-desktop-theme"));
    regVirtual(QStringLiteral("animations-editor"), QStringLiteral("animations-surfaces"),
               PhosphorI18n::tr("Layout Editor"), QStringLiteral("AnimationsEditorPage.qml"),
               QStringLiteral("document-edit"));

    regVirtual(QStringLiteral("animations-presets"), QStringLiteral("animations-library"), PhosphorI18n::tr("Presets"),
               QStringLiteral("AnimationsPresetsPage.qml"), QStringLiteral("bookmarks"));
    regVirtual(QStringLiteral("animations-motionsets"), QStringLiteral("animations-library"),
               PhosphorI18n::tr("Motion Sets"), QStringLiteral("AnimationsMotionSetsPage.qml"),
               QStringLiteral("color-palette"));
    regVirtual(QStringLiteral("animations-shaders"), QStringLiteral("animations-library"), PhosphorI18n::tr("Shaders"),
               QStringLiteral("AnimationsShadersPage.qml"), QStringLiteral("preferences-desktop-display"));

    // Bridge SettingsController.save/load to the framework's Apply/Cancel
    // (and to the global dirty flag QML chrome binds to).
    m_app->registerDomain(new SettingsStagingDomain(this, m_app.get()));

    // Sync activePage ↔ ApplicationController.currentPageId. Both directions
    // are guarded against re-entrancy by comparing the incoming value to the
    // already-stored one before propagating.
    m_app->setCurrentPageId(m_activePage);
    // Context object for both connections below is `m_app.get()`, so Qt
    // auto-disconnects them when `m_app` is destroyed — no manual
    // null-guard on `m_app` inside the lambdas is needed.
    connect(this, &SettingsController::activePageChanged, m_app.get(), [this]() {
        if (m_app->currentPageId() != m_activePage) {
            m_app->setCurrentPageId(m_activePage);
        }
    });
    connect(m_app.get(), &PhosphorControl::ApplicationController::currentPageIdChanged, this, [this]() {
        const QString id = m_app->currentPageId();
        if (id.isEmpty() || id == m_activePage) {
            return;
        }
        const QString previousActive = m_activePage;
        setActivePage(id);
        // setActivePage rejects unknown ids (e.g. a stale CLI
        // --page=exclusions invocation after the page was folded
        // out) with a warning and returns early — m_activePage
        // stays at previousActive. The framework's m_app side has
        // ALREADY accepted the invalid id from its own setter, so
        // without a snap-back the two would diverge silently
        // (m_app->currentPageId() == "exclusions" while
        // m_activePage == "general"). Restore the authoritative
        // controller-side id whenever validation failed.
        if (m_activePage == previousActive && id != previousActive) {
            m_app->setCurrentPageId(m_activePage);
        }
    });
}

void SettingsController::setDismissedUpdateVersion(const QString& version)
{
    if (m_dismissedUpdateVersion != version) {
        m_dismissedUpdateVersion = version;
        QSettings appSettings;
        appSettings.setValue(ConfigDefaults::settingsAppDismissedUpdateVersionKey(), version);
        Q_EMIT dismissedUpdateVersionChanged();
    }
}

void SettingsController::dismissUpdate()
{
    setDismissedUpdateVersion(m_updateChecker.latestVersion());
}

// Highest version among m_whatsNewEntries, using QVersionNumber so "1.10.0"
// sorts after "1.9.0" (plain string compare gets that wrong). Entries come
// from the bundled whatsnew.json resource in no guaranteed order.
QString SettingsController::latestWhatsNewVersion() const
{
    QVersionNumber best;
    QString bestStr;
    for (const QVariant& v : m_whatsNewEntries) {
        const QString ver = v.toMap().value(QStringLiteral("version")).toString();
        const QVersionNumber parsed = QVersionNumber::fromString(ver);
        if (parsed.isNull())
            continue;
        if (bestStr.isEmpty() || best < parsed) {
            best = parsed;
            bestStr = ver;
        }
    }
    return bestStr;
}

bool SettingsController::hasUnseenWhatsNew() const
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return false;
    // Unseen iff the latest bundled entry is strictly newer than what the
    // user last marked seen. String compare after normalisation would still
    // mis-order "1.10" vs "1.9", so go through QVersionNumber.
    const QVersionNumber latestV = QVersionNumber::fromString(latest);
    const QVersionNumber seenV = QVersionNumber::fromString(m_lastSeenWhatsNewVersion);
    return seenV < latestV;
}

void SettingsController::markWhatsNewSeen()
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return;
    if (m_lastSeenWhatsNewVersion != latest) {
        m_lastSeenWhatsNewVersion = latest;
        QSettings appSettings;
        appSettings.setValue(ConfigDefaults::settingsAppLastSeenWhatsNewVersionKey(), latest);
        Q_EMIT lastSeenWhatsNewVersionChanged();
    }
}

const QHash<QString, QString>& SettingsController::parentPageRedirects()
{
    // Parent sidebar categories have no QML component — resolve them to their
    // first child so D-Bus / CLI / Q_INVOKABLE callers get a sensible result.
    // Includes both top-level parents AND mid-level virtual parents
    // (`animations-surfaces`, `animations-library`) so any of those names
    // passed via `--page` or D-Bus lands on a real leaf instead of triggering
    // the generic "Unknown settings page" warning.
    static const QHash<QString, QString> redirects{
        {QStringLiteral("display"), QStringLiteral("virtualscreens")},
        // Appearance hub virtual parents → first reachable leaf. The first
        // child chain is appearance → Windows → Tiled (tiling-appearance);
        // Daemon Surfaces' first leaf is OSDs (osd-appearance).
        {QStringLiteral("appearance"), QStringLiteral("osd-appearance")},
        {QStringLiteral("appearance-daemon"), QStringLiteral("osd-appearance")},
        // Window appearance merged into the Tiling/Snapping → Window subject pages;
        // old deep-links land on the combined pages.
        {QStringLiteral("tiling-appearance"), QStringLiteral("tiling-behavior")},
        {QStringLiteral("snapping-window-appearance"), QStringLiteral("snapping-window-behavior")},
        // "placement" is the inline-collapsible parent of snapping/tiling; it
        // has no page of its own, so a --page=placement / D-Bus call lands on
        // the first leaf of its first child (snapping → snapping-overlay-behavior).
        {QStringLiteral("placement"), QStringLiteral("snapping-overlay-behavior")},
        {QStringLiteral("snapping"), QStringLiteral("snapping-overlay-behavior")},
        // Tiling's first child leaf is Behavior.
        {QStringLiteral("tiling"), QStringLiteral("tiling-behavior")},
        {QStringLiteral("animations"), QStringLiteral("animations-general")},
        {QStringLiteral("animations-surfaces"), QStringLiteral("animations-windows")},
        {QStringLiteral("animations-library"), QStringLiteral("animations-presets")},
        // The "rules" parent virtual retired when Window Rules promoted
        // to a top-level entry; no redirect needed because there is no
        // longer a parent id to land on.
        // The remaining *-cat virtual headers (registered as collapsible
        // category entries in buildApplicationController above) are real entries
        // in the framework PageRegistry — without an explicit redirect, anything
        // that drives setCurrentPageId("snapping-config-cat") would land on a
        // page id that isn't in validPageNames() and m_activePage would diverge
        // from m_app->currentPageId(). Map each to its first leaf so the
        // active-page state stays coherent regardless of how the id was reached.
        // (The Overlay/Window *-cat wrappers were folded away when their single
        // Behavior leaf was promoted directly under snapping/tiling.)
        {QStringLiteral("snapping-config-cat"), QStringLiteral("snapping-ordering")},
        {QStringLiteral("tiling-config-cat"), QStringLiteral("tiling-ordering")},
    };
    return redirects;
}

const QHash<QString, QSet<QString>>& SettingsController::pageGroupChildren()
{
    // Single source of truth: parent name → set of leaf child page
    // names. Used by `isPageDirty` to propagate dirty state from a
    // leaf to any group it belongs to. Covers parents at every level:
    // top-level categories (placement / display / animations) AND the
    // mid-level virtual parents nested beneath them (snapping / tiling
    // under placement; animations-surfaces / animations-library; the
    // *-cat headers) whose children don't share their name prefix — the
    // explicit set sidesteps the asymmetry between prefix-walk and
    // direct membership lookup.
    //
    // The "animations" entry is built at static-init by unioning the
    // virtual sub-buckets (`animations-surfaces`, `animations-library`)
    // with the leaf that hangs directly off `animations` (general).
    // Without this, a future leaf added to a virtual parent only would
    // silently miss the top-level dirty propagation.
    //
    // Keep the per-group leaf lists in sync with the parentId arguments
    // in `buildApplicationController()` above — that function is the
    // registry's source of truth for the page tree; this static map
    // exists because `isPageDirty()` is a hot path and the per-call walk
    // over `m_app->pageRegistry()->allPages()` to derive the parent→leaf
    // mapping would otherwise re-scan every page on every dirty-check.
    // (The historical "_childItems" reference in Main.qml is obsolete —
    // the chrome now consumes registry topology directly via Sidebar.qml.)
    static const QSet<QString> kAnimationsSurfacesChildren{
        QStringLiteral("animations-windows"),  QStringLiteral("animations-osds"),
        QStringLiteral("animations-overlays"), QStringLiteral("animations-side-panels"),
        QStringLiteral("animations-widgets"),  QStringLiteral("animations-editor")};
    static const QSet<QString> kAnimationsLibraryChildren{QStringLiteral("animations-presets"),
                                                          QStringLiteral("animations-motionsets"),
                                                          QStringLiteral("animations-shaders")};
    static const QSet<QString> kAnimationsDirectChildren{QStringLiteral("animations-general")};
    static const QSet<QString> kAnimationsAllLeaves =
        kAnimationsDirectChildren + kAnimationsSurfacesChildren + kAnimationsLibraryChildren;
    // The Overlay/Window appearance leaves and the shader browser moved to the
    // Appearance hub (see kAppearance* sets below), leaving one behavior page per
    // snapping/tiling subject. Those single leaves were promoted directly under
    // the snapping/tiling drill-downs, so the old Overlay/Window *-cat category
    // wrappers are gone; only the Configuration *-cat headers remain. Sidebar.qml
    // renders those as collapsible section headers, and Main.qml's
    // trailingDelegate calls isPageDirty(<*-cat>) when collapsed, so they keep a
    // group entry below.
    static const QSet<QString> kSnappingConfigChildren{
        QStringLiteral("snapping-ordering"),
        QStringLiteral("snapping-shortcuts"),
    };
    static const QSet<QString> kSnappingBehaviorLeaves{
        QStringLiteral("snapping-overlay-behavior"),
        QStringLiteral("snapping-zoneselector"),
        QStringLiteral("snapping-window-behavior"),
    };
    static const QSet<QString> kSnappingAllLeaves = kSnappingBehaviorLeaves + kSnappingConfigChildren;
    static const QSet<QString> kTilingConfigChildren{
        QStringLiteral("tiling-ordering"),
        QStringLiteral("tiling-shortcuts"),
    };
    static const QSet<QString> kTilingAllLeaves =
        QSet<QString>{QStringLiteral("tiling-behavior"), QStringLiteral("tiling-algorithm")} + kTilingConfigChildren;
    // Appearance hub leaves (re-parented from the feature areas above). Grouped
    // by surface so a collapsed Appearance / Windows / Daemon-Surfaces header
    // lights its dirty badge when any contained node is dirty. Windows and Daemon
    // Surfaces hang directly off the Appearance drill-down, as does Pack Library
    // (snapping-shaders).
    static const QSet<QString> kAppearanceDaemonChildren{
        QStringLiteral("osd-appearance"),
        QStringLiteral("snapping-overlay-appearance"),
        QStringLiteral("zoneselector-appearance"),
    };
    static const QSet<QString> kAppearanceAllLeaves =
        kAppearanceDaemonChildren + QSet<QString>{QStringLiteral("snapping-shaders")};
    static const QHash<QString, QSet<QString>> groups{
        {QStringLiteral("appearance"), kAppearanceAllLeaves},
        {QStringLiteral("appearance-daemon"), kAppearanceDaemonChildren},
        {QStringLiteral("snapping"), kSnappingAllLeaves},
        {QStringLiteral("tiling"), kTilingAllLeaves},
        // "placement" is the inline-collapsible parent of snapping + tiling;
        // when collapsed its dirty badge must light if any snapping OR tiling
        // leaf is dirty, so its leaf set is the union of both modes' leaves.
        {QStringLiteral("placement"), kSnappingAllLeaves + kTilingAllLeaves},
        {QStringLiteral("snapping-config-cat"), kSnappingConfigChildren},
        {QStringLiteral("tiling-config-cat"), kTilingConfigChildren},
        {QStringLiteral("animations"), kAnimationsAllLeaves},
        {QStringLiteral("animations-surfaces"), kAnimationsSurfacesChildren},
        {QStringLiteral("animations-library"), kAnimationsLibraryChildren},
        // Top-level inline-collapsible parents must also propagate
        // dirty state from their leaves — without these entries the
        // sidebar's collapsed dirty badge stays cold even when a
        // child page is dirty. Mirrors the registry topology in
        // buildApplicationController() above.
        {QStringLiteral("display"), {QStringLiteral("virtualscreens"), QStringLiteral("layouts")}},
        // No "rules" entry — Window Rules is a top-level leaf so its
        // dirty state propagates without a parent-bucket intermediary.
    };
    return groups;
}

const QSet<QString>& SettingsController::validPageNames()
{
    // Keep in sync with the `regPage` / `regVirtual` registrations in
    // `buildApplicationController` above — every entry here must resolve
    // to a registered page, otherwise external --page invocations and
    // sidebar navigation will silently fall through to the default page.
    // (The legacy `_pageComponents` Main.qml map this comment used to
    // name was retired when Main.qml moved to PhosphorUi.SettingsAppWindow's
    // framework-driven PageHost Loader, keyed off the registry.)
    static const QSet<QString> pages{
        QStringLiteral("overview"),
        QStringLiteral("layouts"),
        QStringLiteral("snapping-overlay-behavior"),
        QStringLiteral("snapping-overlay-appearance"),
        QStringLiteral("osd-appearance"),
        QStringLiteral("zoneselector-appearance"),
        QStringLiteral("snapping-zoneselector"),
        QStringLiteral("snapping-window-behavior"),
        QStringLiteral("snapping-shaders"),
        QStringLiteral("snapping-shortcuts"),
        QStringLiteral("tiling-behavior"),
        QStringLiteral("tiling-algorithm"),
        QStringLiteral("tiling-shortcuts"),
        QStringLiteral("snapping-ordering"),
        QStringLiteral("tiling-ordering"),
        QStringLiteral("window-rules"),
        QStringLiteral("editor"),
        QStringLiteral("general"),
        QStringLiteral("about"),
        QStringLiteral("virtualscreens"),
        QStringLiteral("animations-general"),
        QStringLiteral("animations-windows"),
        QStringLiteral("animations-editor"),
        QStringLiteral("animations-osds"),
        QStringLiteral("animations-overlays"),
        QStringLiteral("animations-side-panels"),
        QStringLiteral("animations-widgets"),
        QStringLiteral("animations-presets"),
        QStringLiteral("animations-motionsets"),
        QStringLiteral("animations-shaders"),
    };
    return pages;
}

} // namespace PlasmaZones
