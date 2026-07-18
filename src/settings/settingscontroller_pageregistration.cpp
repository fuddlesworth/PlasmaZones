// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-registration methods for SettingsController:
//   * buildApplicationController() — wires the PhosphorControl
//     PageRegistry with PlasmaZones' settings pages and sidebar categories
//     (the navigable leaf pages are enumerated in validPageNames()).
//   * What's-New dismissal + last-seen-version state.
//
// The static sidebar parent/child topology accessors (parentPageRedirects,
// pageGroupChildren, pageOwnedConfigKeys, validPageNames) live in the sibling
// settingscontroller_pagetopology.cpp.
//
// All methods here are members of PlasmaZones::SettingsController. Same class
// as settingscontroller.cpp, separate translation unit, no API change.

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
    // Animations, Rules; (3) tools & meta — Editor, About.
    // `divider` flags mark only those two seams (after General, after Window
    // Rules) plus one inside the feature block (after Placement, to set the
    // placement categories apart from Animations/Rules).

    // ── Block 1: top / global ──
    regVirtual(QStringLiteral("overview"), QString(), PhosphorI18n::tr("Overview"),
               QStringLiteral("MonitorStatePage.qml"), QStringLiteral("monitor"));
    // General leads near the top (mirrors the Animations section leading with
    // its own "General" child). Divider after it closes the top/global block.
    regPage(m_generalPage, QString(), PhosphorI18n::tr("General"), QStringLiteral("GeneralPage.qml"),
            QStringLiteral("configure"), /*collapsible=*/false, /*divider=*/true);

    // ── Block 2: per-feature configuration ──
    regVirtual(QStringLiteral("display"), QString(), PhosphorI18n::tr("Display"), QString(),
               QStringLiteral("preferences-desktop-display"), /*collapsible=*/true);
    // Placement groups the two placement modes (Snapping / Tiling) as an
    // inline-collapsible category, matching Display. Divider after it (i.e.
    // above Animations) sets the placement categories apart from the
    // Animations / Rules pages that follow.
    regVirtual(QStringLiteral("placement"), QString(), PhosphorI18n::tr("Placement"), QString(),
               QStringLiteral("preferences-system-windows"), /*collapsible=*/true, /*divider=*/true);
    // Appearance groups the visual surfaces — the Animations tree and the
    // Decoration tree (which owns the window border / title-bar / gap page) — as an
    // inline-collapsible category (matching Display / Placement). No QML of its
    // own; redirects to its first leaf.
    regVirtual(QStringLiteral("appearance"), QString(), PhosphorI18n::tr("Appearance"), QString(),
               QStringLiteral("preferences-desktop-theme"), /*collapsible=*/true);
    // "animations" is a no-QML drill-down parent under Appearance — register it as
    // a virtual navigation node (like display / placement / snapping / tiling),
    // NOT as m_animationsPage's own id. The AnimationsPageController is the
    // staging controller for animation edits; it carries the distinct id
    // "animations-staging" and is wired into the framework's dirty/apply
    // machinery as a headless domain below. Splitting the two means the nav
    // handle ("animations", redirected to "animations-general") and the
    // staging controller are independently addressable by QML / D-Bus.
    regVirtual(QStringLiteral("animations"), QStringLiteral("appearance"), PhosphorI18n::tr("Animations"), QString(),
               QStringLiteral("media-playback-start"));
    // Headless staging domain — trackDomain() connects dirtyChanged + appends
    // to m_domains so applyAllAsync walks it, exactly as registerPage would
    // have, but without claiming a sidebar/registry id of its own.
    m_app->registerDomain(m_animationsPage);
    // Decoration — a no-QML drill-down parent UNDER Appearance, alongside
    // Windows and Animations (all visual-surface pages live in that group).
    // PER-SURFACE scope: per-surface CHAINS of decoration shader packs,
    // resolved through a DecorationProfileTree (walk-up inheritance). Each
    // surface family (window / popup) is its own alwaysEnabled root. The nav
    // handle ("decoration") redirects to its General page (the config-backed
    // window-appearance page, registered below with the decoration children).
    // The DecorationPageController is wired in
    // as a headless staging domain below; it has no per-page staged state —
    // dirty tracking rides the global decorationProfileTreeChanged NOTIFY loop.
    regVirtual(QStringLiteral("decorations"), QStringLiteral("appearance"), PhosphorI18n::tr("Decorations"), QString(),
               QStringLiteral("preferences-desktop-theme"));
    // Headless staging domain — trackDomain() connects dirtyChanged + appends
    // to m_domains so applyAllAsync walks it, exactly as registerPage would,
    // but without claiming a sidebar/registry id of its own.
    m_app->registerDomain(m_decorationPage);
    // Rules is a top-level leaf (its old "Rules" parent retired after
    // the v4 fold left a single rule surface). Divider after it closes the
    // feature block and opens the tools-and-meta block below.
    regPage(m_rulesPage, QString(), PhosphorI18n::tr("Rules"), QStringLiteral("RulesPage.qml"),
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

    // Snapping children — organised by SUBJECT, each carrying its own Behavior
    // and Appearance pages: the drag Overlay, the edge Zone-Selector popup, and
    // the snapped Window, plus a shared Configuration block. Most leaves are
    // QML-only (regVirtual) and bind to the existing per-feature controllers —
    // Overlay→Behavior and Window→Behavior bind snappingBehaviorPage; Overlay→
    // Appearance binds snappingZonesPage + snappingEffectsPage; Zone Selector
    // binds snappingZoneSelectorPage. Those controllers stay exposed as
    // Q_PROPERTY bridges on SettingsController but are no longer registered as
    // pages of their own. Window→Appearance keeps its dedicated controller
    // (border/gap bounds), so it alone stays a regPage. Per-page dirtiness keys
    // off the ACTIVE page at edit time (see setNeedsSave), so sharing a
    // controller across two pages is fine.
    regVirtual(QStringLiteral("snapping-overlay-cat"), QStringLiteral("snapping"), PhosphorI18n::tr("Overlay"),
               QString(), QStringLiteral("preferences-desktop-color"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("snapping-overlay-behavior"), QStringLiteral("snapping-overlay-cat"),
               PhosphorI18n::tr("Behavior"), QStringLiteral("SnappingOverlayBehaviorPage.qml"),
               QStringLiteral("preferences-system"));
    regVirtual(QStringLiteral("snapping-overlay-appearance"), QStringLiteral("snapping-overlay-cat"),
               PhosphorI18n::tr("Appearance"), QStringLiteral("SnappingOverlayAppearancePage.qml"),
               QStringLiteral("preferences-desktop-color"));

    // Zone Selector is a single top leaf under Snapping (not split into
    // Behavior/Appearance): its behaviour is just the enable toggle + trigger
    // distance, too small to warrant its own page. Structurally parallel to
    // Tiling's Algorithm leaf.
    regVirtual(QStringLiteral("snapping-zoneselector"), QStringLiteral("snapping"), PhosphorI18n::tr("Zone Selector"),
               QStringLiteral("SnappingZoneSelectorPage.qml"), QStringLiteral("view-choose"), /*collapsible=*/false,
               /*divider=*/true);

    // Snapping → Window holds just the per-mode Behavior page now. The window
    // border / title-bar appearance moved to the shared, top-level Window
    // Appearance page (config-backed, shared, mode-neutral).
    regVirtual(QStringLiteral("snapping-window-behavior"), QStringLiteral("snapping"), PhosphorI18n::tr("Window"),
               QStringLiteral("SnappingWindowBehaviorPage.qml"), QStringLiteral("preferences-system-windows"),
               /*collapsible=*/false, /*divider=*/true);

    regVirtual(QStringLiteral("snapping-config-cat"), QStringLiteral("snapping"), PhosphorI18n::tr("Configuration"),
               QString(), QStringLiteral("configure"), /*collapsible=*/true);
    regVirtual(QStringLiteral("snapping-ordering"), QStringLiteral("snapping-config-cat"), PhosphorI18n::tr("Priority"),
               QStringLiteral("SnappingOrderingPage.qml"), QStringLiteral("view-sort"));
    regVirtual(QStringLiteral("snapping-shortcuts"), QStringLiteral("snapping-config-cat"),
               PhosphorI18n::tr("Quick Shortcuts"), QStringLiteral("SnappingQuickShortcutsPage.qml"),
               QStringLiteral("bookmark"));
    regPage(m_snappingShadersPage.get(), QStringLiteral("snapping-config-cat"), PhosphorI18n::tr("Shaders"),
            QStringLiteral("SnappingShadersPage.qml"), QStringLiteral("preferences-desktop-display"));

    // Tiling children — organised by subject (Window / Algorithm / Configuration)
    // to match the snapping reorg. Tiling has no drag-overlay or selector popup,
    // so its only interaction surface (the drag-insert indicator) folds into
    // Window → Behavior; the layout algorithm is its own top leaf, and gaps moved
    // onto Window → Appearance. Page IDs are unchanged (tiling has a single
    // Behavior/Appearance pair, so they need no overlay/selector disambiguation) —
    // only the parents changed, keeping the per-page controller ids stable.
    // Tiling → Window holds just the per-mode Behavior page now. The window
    // border / title-bar appearance moved to the shared, top-level Window
    // Appearance page (config-backed, shared, mode-neutral).
    regPage(m_tilingBehaviorPage, QStringLiteral("tiling"), PhosphorI18n::tr("Window"),
            QStringLiteral("TilingBehaviorPage.qml"), QStringLiteral("preferences-system-windows"),
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

    // Animations children — Transitions / Motion / Library categories drill in.
    // The simple-mode surface leads: a SimpleOnly leaf (stamped below) that
    // replaces the whole per-event tree when the user is in simple mode. In
    // advanced mode it is filtered out and the General page leads instead.
    regVirtual(QStringLiteral("animations-simple"), QStringLiteral("animations"), PhosphorI18n::tr("Animations"),
               QStringLiteral("AnimationsSimplePage.qml"), QStringLiteral("media-playback-start"),
               /*collapsible=*/false, /*divider=*/true);
    regVirtual(QStringLiteral("animations-general"), QStringLiteral("animations"), PhosphorI18n::tr("General"),
               QStringLiteral("AnimationsGeneralPage.qml"), QStringLiteral("configure"), /*collapsible=*/false,
               /*divider=*/true);
    // Transitions — shader-driven appearance and reveal events (window
    // open/close/minimize, OSDs, overlays, desktop switch), grouped so each child
    // page carries ONE shader contract. This keeps the per-row shader picker
    // coherent: every effect it offers actually applies.
    regVirtual(QStringLiteral("animations-transitions"), QStringLiteral("animations"), PhosphorI18n::tr("Transitions"),
               QString(), QStringLiteral("preferences-desktop-multimedia"),
               /*collapsible=*/true);
    // Motion — movement and geometry events. Window motion (maximize / snap /
    // layout switch) carries the geometry shader contract, so its page still
    // shows a shader picker; the held drag is its own opt-in `move` class on
    // the Window Dragging child page; side panels, widgets and the editor are
    // timing/curve only.
    regVirtual(QStringLiteral("animations-motion"), QStringLiteral("animations"), PhosphorI18n::tr("Motion"), QString(),
               QStringLiteral("chronometer"), /*collapsible=*/true);
    regVirtual(QStringLiteral("animations-library"), QStringLiteral("animations"), PhosphorI18n::tr("Library"),
               QString(), QStringLiteral("folder-open"), /*collapsible=*/true);

    regVirtual(QStringLiteral("animations-windows"), QStringLiteral("animations-transitions"),
               PhosphorI18n::tr("Windows"), QStringLiteral("AnimationsWindowsPage.qml"), QStringLiteral("window-new"));
    regVirtual(QStringLiteral("animations-osds"), QStringLiteral("animations-transitions"), PhosphorI18n::tr("OSDs"),
               QStringLiteral("AnimationsOsdsPage.qml"), QStringLiteral("dialog-information"));
    regVirtual(QStringLiteral("animations-overlays"), QStringLiteral("animations-transitions"),
               PhosphorI18n::tr("Overlays"), QStringLiteral("AnimationsOverlaysPage.qml"),
               QStringLiteral("view-presentation"));
    regVirtual(QStringLiteral("animations-desktops"), QStringLiteral("animations-transitions"),
               PhosphorI18n::tr("Desktop"), QStringLiteral("AnimationsDesktopsPage.qml"),
               QStringLiteral("virtual-desktops"));

    regVirtual(QStringLiteral("animations-window-motion"), QStringLiteral("animations-motion"),
               PhosphorI18n::tr("Window Motion"), QStringLiteral("AnimationsWindowMotionPage.qml"),
               QStringLiteral("window-new"));
    // Window Dragging is deliberately its own page, not a row under Window
    // Motion: the drag event is its own opt-in shader class (`move`) that
    // takes no inherited shader, so parking it under the "All Windows"
    // cascade parent would misrepresent the inheritance.
    regVirtual(QStringLiteral("animations-window-dragging"), QStringLiteral("animations-motion"),
               PhosphorI18n::tr("Window Dragging"), QStringLiteral("AnimationsWindowDraggingPage.qml"),
               QStringLiteral("transform-move"));
    regVirtual(QStringLiteral("animations-side-panels"), QStringLiteral("animations-motion"),
               PhosphorI18n::tr("Side Panels"), QStringLiteral("AnimationsSidePanelsPage.qml"),
               QStringLiteral("sidebar-collapse-symbolic"));
    regVirtual(QStringLiteral("animations-widgets"), QStringLiteral("animations-motion"), PhosphorI18n::tr("Widgets"),
               QStringLiteral("AnimationsWidgetsPage.qml"), QStringLiteral("preferences-desktop-theme"));
    regVirtual(QStringLiteral("animations-editor"), QStringLiteral("animations-motion"),
               PhosphorI18n::tr("Layout Editor"), QStringLiteral("AnimationsEditorPage.qml"),
               QStringLiteral("document-edit"));

    regVirtual(QStringLiteral("animations-presets"), QStringLiteral("animations-library"), PhosphorI18n::tr("Presets"),
               QStringLiteral("AnimationsPresetsPage.qml"), QStringLiteral("bookmarks"));
    regVirtual(QStringLiteral("animations-motionsets"), QStringLiteral("animations-library"),
               PhosphorI18n::tr("Motion Sets"), QStringLiteral("AnimationsMotionSetsPage.qml"),
               QStringLiteral("color-palette"));
    regVirtual(QStringLiteral("animations-shaders"), QStringLiteral("animations-library"), PhosphorI18n::tr("Shaders"),
               QStringLiteral("AnimationsShadersPage.qml"), QStringLiteral("preferences-desktop-display"));

    // Decoration children — Surfaces / Library categories drill in, the same
    // two-bucket shape as animations. Unlike animations there is NO General
    // page: decoration has no meaningful global default (borders and title
    // bars are window-only; daemon surfaces default to no decoration), so
    // Surfaces fans straight out to the per-surface override pages and
    // Library holds the installed-pack browser.
    // Decoration → General: the shared, mode-neutral window-appearance page
    // (config-backed window border / title bar Windows.* + the unified gap
    // model Gaps.*). Lives under Decoration as its General page — the same
    // slot animations-general occupies — keeping its historical
    // "window-appearance" id so dirty tracking, the per-page reset manifest,
    // and deep links stay stable.
    regPage(m_windowAppearancePage, QStringLiteral("decorations"), PhosphorI18n::tr("General"),
            QStringLiteral("WindowAppearancePage.qml"), QStringLiteral("configure"), /*collapsible=*/false,
            /*divider=*/true);

    regVirtual(QStringLiteral("decorations-surfaces"), QStringLiteral("decorations"), PhosphorI18n::tr("Surfaces"),
               QString(), QStringLiteral("preferences-desktop-multimedia"), /*collapsible=*/true);
    regVirtual(QStringLiteral("decorations-library"), QStringLiteral("decorations"), PhosphorI18n::tr("Library"),
               QString(), QStringLiteral("folder-open"), /*collapsible=*/true);

    regVirtual(QStringLiteral("decorations-windows"), QStringLiteral("decorations-surfaces"),
               PhosphorI18n::tr("Windows"), QStringLiteral("DecorationWindowsPage.qml"), QStringLiteral("window-new"));
    regVirtual(QStringLiteral("decorations-osds"), QStringLiteral("decorations-surfaces"), PhosphorI18n::tr("OSDs"),
               QStringLiteral("DecorationOsdsPage.qml"), QStringLiteral("dialog-information"));
    regVirtual(QStringLiteral("decorations-popups"), QStringLiteral("decorations-surfaces"), PhosphorI18n::tr("Popups"),
               QStringLiteral("DecorationPopupsPage.qml"), QStringLiteral("view-presentation"));

    regVirtual(QStringLiteral("decorations-sets"), QStringLiteral("decorations-library"),
               PhosphorI18n::tr("Decoration Sets"), QStringLiteral("DecorationSetsPage.qml"),
               QStringLiteral("color-palette"));
    regVirtual(QStringLiteral("decorations-shaders"), QStringLiteral("decorations-library"),
               PhosphorI18n::tr("Shaders"), QStringLiteral("DecorationShadersPage.qml"),
               QStringLiteral("preferences-desktop-display"));

    // ── Simple/advanced classification ──
    // Now that every page is registered, stamp the tier: any navigable leaf
    // NOT in simpleModeAllowedPages() is AdvancedOnly, so it drops out of the
    // rail in simple mode (and its now-empty category parents auto-hide). The
    // allow-set holds only leaves; virtual parents stay Always. Then seed the
    // registry's mode from m_advancedMode (default simple) so the very first
    // sidebar build is already filtered. The QML QtCore.Settings block in
    // Main.qml pushes the user's remembered choice in over this at startup.
    for (const QString& leaf : validPageNames()) {
        if (!simpleModeAllowedPages().contains(leaf)) {
            m_app->registry()->setPageVisibility(leaf, PhosphorControl::PageRegistry::PageVisibility::AdvancedOnly);
        }
    }
    // The simple-mode animations surface exists ONLY in simple mode — advanced
    // mode has the full per-event tree instead. It is the sole SimpleOnly page
    // today; future combined simple pages join it here.
    m_app->registry()->setPageVisibility(QStringLiteral("animations-simple"),
                                         PhosphorControl::PageRegistry::PageVisibility::SimpleOnly);
    m_app->registry()->setShowAdvanced(m_advancedMode);

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

} // namespace PlasmaZones
