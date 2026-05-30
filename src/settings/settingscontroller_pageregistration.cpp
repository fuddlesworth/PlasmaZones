// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-registration + sidebar topology methods for SettingsController:
//   * buildApplicationController() — wires the PhosphorSettingsUi
//     PageRegistry with PlasmaZones' 30 settings pages.
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
#include "../pz_i18n.h"
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
    m_app = std::make_unique<PhosphorSettingsUi::ApplicationController>();

    const QString qmlPrefix = QStringLiteral("qrc:/qt/qml/org/plasmazones/settings/qml/");

    // Two helpers — both delegate to ApplicationController::registerPage:
    //   regPage:     the page already has its own PageController subclass
    //                (the existing m_xxxPage controllers, now PhosphorSettingsUi::
    //                PageController-derived).
    //   regVirtual:  no concrete controller — either a drill-down parent /
    //                inline-collapsible category header, or a leaf whose QML
    //                page binds to a Settings property directly (no per-page
    //                controller). Uses PageAdapter as the framework-facing
    //                identity so the registry still gets a stable
    //                PageController*.
    const auto regPage = [this, &qmlPrefix](PhosphorSettingsUi::PageController* page, const QString& parentId,
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

    // Top-level entries — matches the legacy _mainItems in Main.qml.
    // Divider placements mirror the legacy `hasDividerAfter: true` flags
    // on overview/display/tiling/window-rules so the rail's visual
    // rhythm is preserved across the migration. (The legacy parent
    // virtual "rules" was retired when its only child was promoted to
    // the top-level "window-rules" entry; the divider semantics moved
    // with the promotion.)
    regVirtual(QStringLiteral("overview"), QString(), PzI18n::tr("Overview"), QStringLiteral("MonitorStatePage.qml"),
               QStringLiteral("monitor"), /*collapsible=*/false, /*divider=*/true);
    regVirtual(QStringLiteral("display"), QString(), PzI18n::tr("Display"), QString(),
               QStringLiteral("preferences-desktop-display"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("snapping"), QString(), PzI18n::tr("Snapping"), QString(),
               QStringLiteral("view-split-left-right"));
    regVirtual(QStringLiteral("tiling"), QString(), PzI18n::tr("Tiling"), QString(), QStringLiteral("window-duplicate"),
               /*collapsible=*/false, /*divider=*/true);
    // DESIGN-NOTE: m_animationsPage carries PageController id "animations"
    // (see AnimationsPageController ctor — animationspagecontroller.cpp:159)
    // which is ALSO the navigation-parent id we redirect to
    // "animations-general" via parentPageRedirects(). The round-trip works
    // today because setActivePage resolves the parent before storing
    // m_activePage, but the dual role means QML or D-Bus callers can't
    // distinguish "navigate to the Animations parent (and land on its first
    // child)" from "address the staging controller's own id". Consider
    // splitting: keep the parent navigation handle as "animations" and
    // rename the controller id to something like "animations-staging" so
    // the two surfaces are independent.
    regPage(m_animationsPage, QString(), PzI18n::tr("Animations"), QString(), QStringLiteral("media-playback-start"));
    // Window Rules sits at the top level — it used to live under a
    // collapsible "Rules" category alongside Exclusions, but after the v4
    // fold there is only one rule surface left, so the parent category
    // would add navigation without organising anything. Promoting it
    // gets the page one click closer too. Divider after it closes the
    // feature-configuration block (Display / Snapping / Tiling /
    // Animations / Window Rules) and separates it from the tools-and-
    // meta block below (Editor / General / About) so the rail's visual
    // rhythm has a clear seam where the per-feature pages end.
    regPage(m_windowRulesPage, QString(), PzI18n::tr("Window Rules"), QStringLiteral("WindowRulesPage.qml"),
            QStringLiteral("view-list-details"), /*collapsible=*/false, /*divider=*/true);
    regPage(m_editorPage, QString(), PzI18n::tr("Editor"), QStringLiteral("EditorPage.qml"),
            QStringLiteral("document-edit"));
    regPage(m_generalPage, QString(), PzI18n::tr("General"), QStringLiteral("GeneralPage.qml"),
            QStringLiteral("configure"));
    regVirtual(QStringLiteral("about"), QString(), PzI18n::tr("About"), QStringLiteral("AboutPage.qml"),
               QStringLiteral("help-about"));

    // Display children
    regVirtual(QStringLiteral("virtualscreens"), QStringLiteral("display"), PzI18n::tr("Virtual Screens"),
               QStringLiteral("VirtualScreensPage.qml"), QStringLiteral("virtual-desktops"));
    regVirtual(QStringLiteral("layouts"), QStringLiteral("display"), PzI18n::tr("Layouts"),
               QStringLiteral("LayoutsPage.qml"), QStringLiteral("view-grid"));

    // Snapping children — the *-cat entries mirror the legacy collapsible
    // category headers (Visual / Behavior / Configuration). They have no
    // delegate and no qmlSource; PageHost.qml shows the placeholder when one
    // of them is the active page.
    regVirtual(QStringLiteral("snapping-visual-cat"), QStringLiteral("snapping"), PzI18n::tr("Visual"), QString(),
               QStringLiteral("preferences-desktop-color"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("snapping-behavior-cat"), QStringLiteral("snapping"), PzI18n::tr("Behavior"), QString(),
               QStringLiteral("preferences-system"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("snapping-config-cat"), QStringLiteral("snapping"), PzI18n::tr("Configuration"),
               QString(), QStringLiteral("configure"), /*collapsible=*/true);

    regPage(m_snappingAppearancePage, QStringLiteral("snapping-visual-cat"), PzI18n::tr("Appearance"),
            QStringLiteral("SnappingAppearancePage.qml"), QStringLiteral("preferences-desktop-color"));
    regPage(m_snappingEffectsPage, QStringLiteral("snapping-visual-cat"), PzI18n::tr("Effects"),
            QStringLiteral("SnappingEffectsPage.qml"), QStringLiteral("preferences-desktop-effects"));
    regPage(m_snappingShadersPage.get(), QStringLiteral("snapping-visual-cat"), PzI18n::tr("Shaders"),
            QStringLiteral("SnappingShadersPage.qml"), QStringLiteral("preferences-desktop-display"));

    regPage(m_snappingBehaviorPage, QStringLiteral("snapping-behavior-cat"), PzI18n::tr("Behavior"),
            QStringLiteral("SnappingBehaviorPage.qml"), QStringLiteral("preferences-system"));
    regPage(m_snappingZoneSelectorPage, QStringLiteral("snapping-behavior-cat"), PzI18n::tr("Zone Selector"),
            QStringLiteral("SnappingZoneSelectorPage.qml"), QStringLiteral("view-choose"));

    regVirtual(QStringLiteral("snapping-ordering"), QStringLiteral("snapping-config-cat"), PzI18n::tr("Priority"),
               QStringLiteral("SnappingOrderingPage.qml"), QStringLiteral("view-sort"));
    regVirtual(QStringLiteral("snapping-shortcuts"), QStringLiteral("snapping-config-cat"),
               PzI18n::tr("Quick Shortcuts"), QStringLiteral("SnappingQuickShortcutsPage.qml"),
               QStringLiteral("bookmark"));

    // Tiling children — same shape as snapping.
    regVirtual(QStringLiteral("tiling-visual-cat"), QStringLiteral("tiling"), PzI18n::tr("Visual"), QString(),
               QStringLiteral("preferences-desktop-color"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("tiling-behavior-cat"), QStringLiteral("tiling"), PzI18n::tr("Behavior"), QString(),
               QStringLiteral("preferences-system"), /*collapsible=*/true, /*divider=*/true);
    regVirtual(QStringLiteral("tiling-config-cat"), QStringLiteral("tiling"), PzI18n::tr("Configuration"), QString(),
               QStringLiteral("configure"), /*collapsible=*/true);

    regPage(m_tilingAppearancePage, QStringLiteral("tiling-visual-cat"), PzI18n::tr("Appearance"),
            QStringLiteral("TilingAppearancePage.qml"), QStringLiteral("preferences-desktop-color"));
    regPage(m_tilingBehaviorPage, QStringLiteral("tiling-behavior-cat"), PzI18n::tr("Behavior"),
            QStringLiteral("TilingBehaviorPage.qml"), QStringLiteral("preferences-system"));
    regPage(m_tilingAlgorithmPage.get(), QStringLiteral("tiling-behavior-cat"), PzI18n::tr("Algorithms"),
            QStringLiteral("TilingAlgorithmPage.qml"), QStringLiteral("view-grid"));

    regVirtual(QStringLiteral("tiling-ordering"), QStringLiteral("tiling-config-cat"), PzI18n::tr("Priority"),
               QStringLiteral("TilingOrderingPage.qml"), QStringLiteral("view-sort"));
    regVirtual(QStringLiteral("tiling-shortcuts"), QStringLiteral("tiling-config-cat"), PzI18n::tr("Quick Shortcuts"),
               QStringLiteral("TilingQuickShortcutsPage.qml"), QStringLiteral("bookmark"));

    // Animations children — Surfaces / Library categories drill in.
    regVirtual(QStringLiteral("animations-general"), QStringLiteral("animations"), PzI18n::tr("General"),
               QStringLiteral("AnimationsGeneralPage.qml"), QStringLiteral("configure"), /*collapsible=*/false,
               /*divider=*/true);
    regVirtual(QStringLiteral("animations-surfaces"), QStringLiteral("animations"), PzI18n::tr("Surfaces"), QString(),
               QStringLiteral("preferences-desktop-multimedia"), /*collapsible=*/true);
    regVirtual(QStringLiteral("animations-library"), QStringLiteral("animations"), PzI18n::tr("Library"), QString(),
               QStringLiteral("folder-open"), /*collapsible=*/true);

    regVirtual(QStringLiteral("animations-windows"), QStringLiteral("animations-surfaces"), PzI18n::tr("Windows"),
               QStringLiteral("AnimationsWindowsPage.qml"), QStringLiteral("window-new"));
    regVirtual(QStringLiteral("animations-osds"), QStringLiteral("animations-surfaces"), PzI18n::tr("OSDs"),
               QStringLiteral("AnimationsOsdsPage.qml"), QStringLiteral("dialog-information"));
    regVirtual(QStringLiteral("animations-overlays"), QStringLiteral("animations-surfaces"), PzI18n::tr("Overlays"),
               QStringLiteral("AnimationsOverlaysPage.qml"), QStringLiteral("view-presentation"));
    regVirtual(QStringLiteral("animations-side-panels"), QStringLiteral("animations-surfaces"),
               PzI18n::tr("Side Panels"), QStringLiteral("AnimationsSidePanelsPage.qml"),
               QStringLiteral("sidebar-collapse-symbolic"));
    regVirtual(QStringLiteral("animations-widgets"), QStringLiteral("animations-surfaces"), PzI18n::tr("Widgets"),
               QStringLiteral("AnimationsWidgetsPage.qml"), QStringLiteral("preferences-desktop-theme"));
    regVirtual(QStringLiteral("animations-editor"), QStringLiteral("animations-surfaces"), PzI18n::tr("Layout Editor"),
               QStringLiteral("AnimationsEditorPage.qml"), QStringLiteral("document-edit"));

    regVirtual(QStringLiteral("animations-presets"), QStringLiteral("animations-library"), PzI18n::tr("Presets"),
               QStringLiteral("AnimationsPresetsPage.qml"), QStringLiteral("bookmarks"));
    regVirtual(QStringLiteral("animations-motionsets"), QStringLiteral("animations-library"), PzI18n::tr("Motion Sets"),
               QStringLiteral("AnimationsMotionSetsPage.qml"), QStringLiteral("color-palette"));
    regVirtual(QStringLiteral("animations-shaders"), QStringLiteral("animations-library"), PzI18n::tr("Shaders"),
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
    connect(m_app.get(), &PhosphorSettingsUi::ApplicationController::currentPageIdChanged, this, [this]() {
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
        {QStringLiteral("snapping"), QStringLiteral("snapping-appearance")},
        {QStringLiteral("tiling"), QStringLiteral("tiling-appearance")},
        {QStringLiteral("animations"), QStringLiteral("animations-general")},
        {QStringLiteral("animations-surfaces"), QStringLiteral("animations-windows")},
        {QStringLiteral("animations-library"), QStringLiteral("animations-presets")},
        // The "rules" parent virtual retired when Window Rules promoted
        // to a top-level entry; no redirect needed because there is no
        // longer a parent id to land on.
        // The *-cat virtual headers (registered as collapsible category
        // entries in buildApplicationController above) are real entries
        // in the framework PageRegistry — without an explicit redirect,
        // anything that drives setCurrentPageId("snapping-visual-cat")
        // would land on a page id that isn't in validPageNames() and
        // m_activePage would diverge from m_app->currentPageId(). Map
        // each *-cat to its first leaf so the active-page state stays
        // coherent regardless of how the id was reached.
        {QStringLiteral("snapping-visual-cat"), QStringLiteral("snapping-appearance")},
        {QStringLiteral("snapping-behavior-cat"), QStringLiteral("snapping-behavior")},
        {QStringLiteral("snapping-config-cat"), QStringLiteral("snapping-ordering")},
        {QStringLiteral("tiling-visual-cat"), QStringLiteral("tiling-appearance")},
        {QStringLiteral("tiling-behavior-cat"), QStringLiteral("tiling-behavior")},
        {QStringLiteral("tiling-config-cat"), QStringLiteral("tiling-ordering")},
    };
    return redirects;
}

const QHash<QString, QSet<QString>>& SettingsController::pageGroupChildren()
{
    // Single source of truth: parent name → set of leaf child page
    // names. Used by `isPageDirty` to propagate dirty state from a
    // leaf to any group it belongs to. Covers both top-level parents
    // (snapping / tiling / animations) AND mid-level virtual parents
    // (animations-surfaces / animations-library) whose children don't
    // share their name prefix — the explicit set sidesteps the
    // asymmetry between prefix-walk and direct membership lookup.
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
    // Mid-level *-cat collapsible category headers under the top-level
    // snapping / tiling parents. Sidebar.qml renders these as collapsible
    // section headers; when COLLAPSED the `sidebar.trailingDelegate` in
    // Main.qml calls isPageDirty(<*-cat>) to decide whether to light the
    // badge. Without these entries that lookup would always
    // return false even when a leaf inside the collapsed section is
    // dirty (mirrors the snapping/tiling parent entries above, just one
    // level deeper). Keep in sync with the regVirtual *-cat registrations
    // in buildApplicationController() above.
    static const QSet<QString> kSnappingVisualChildren{
        QStringLiteral("snapping-appearance"),
        QStringLiteral("snapping-effects"),
        QStringLiteral("snapping-shaders"),
    };
    static const QSet<QString> kSnappingBehaviorChildren{
        QStringLiteral("snapping-behavior"),
        QStringLiteral("snapping-zoneselector"),
    };
    static const QSet<QString> kSnappingConfigChildren{
        QStringLiteral("snapping-ordering"),
        QStringLiteral("snapping-shortcuts"),
    };
    static const QSet<QString> kTilingVisualChildren{
        QStringLiteral("tiling-appearance"),
    };
    static const QSet<QString> kTilingBehaviorChildren{
        QStringLiteral("tiling-behavior"),
        QStringLiteral("tiling-algorithm"),
    };
    static const QSet<QString> kTilingConfigChildren{
        QStringLiteral("tiling-ordering"),
        QStringLiteral("tiling-shortcuts"),
    };
    static const QHash<QString, QSet<QString>> groups{
        {QStringLiteral("snapping"), kSnappingVisualChildren + kSnappingBehaviorChildren + kSnappingConfigChildren},
        {QStringLiteral("tiling"), kTilingVisualChildren + kTilingBehaviorChildren + kTilingConfigChildren},
        {QStringLiteral("snapping-visual-cat"), kSnappingVisualChildren},
        {QStringLiteral("snapping-behavior-cat"), kSnappingBehaviorChildren},
        {QStringLiteral("snapping-config-cat"), kSnappingConfigChildren},
        {QStringLiteral("tiling-visual-cat"), kTilingVisualChildren},
        {QStringLiteral("tiling-behavior-cat"), kTilingBehaviorChildren},
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
        QStringLiteral("snapping-appearance"),
        QStringLiteral("snapping-behavior"),
        QStringLiteral("snapping-zoneselector"),
        QStringLiteral("snapping-effects"),
        QStringLiteral("snapping-shaders"),
        QStringLiteral("snapping-shortcuts"),
        QStringLiteral("tiling-appearance"),
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
