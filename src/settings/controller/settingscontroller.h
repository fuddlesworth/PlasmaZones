// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Central controller for the standalone settings application.
// Manages page navigation, layout CRUD (via D-Bus), screen info, and owns
// the shared Settings instance. Per-page Q_PROPERTY surfaces are split out
// into page-scoped sub-controllers (EditorPageController, …) hung off this
// class via child Q_PROPERTYs so QML reads `settingsController.<page>.<prop>`.
//
// FILE-SIZE EXCEPTION (sanctioned), CEILING 1215 LINES: what remains here after
// that split is the root object QML binds to. Its Q_PROPERTY surface IS the QML
// contract, so moving another group of properties out means either a new child
// controller every page URL and binding has to be rewritten for, or a second
// root QML cannot see. The implementation is already split across
// settingscontroller_*.cpp by concern, same shape as daemon.h.
//
// The number above is a real budget, not a description of wherever the file
// happens to sit: the exception is for the QML contract, so a new declaration
// that pushes past it has to buy its room by removing another (a retired
// Q_INVOKABLE, a property that moved to a child controller). It does NOT
// license a comment block — those belong on the definition in the matching
// settingscontroller_*.cpp when they will not fit here.
//
// The 1215 above supersedes the general 1150 hard ceiling in CLAUDE.md for this
// file, the same way the repo's other sanctioned file-size exceptions do, so
// sitting between the two figures is not a review finding here.

#pragma once

#include "config/configdefaults.h"
#include "phosphor_i18n.h"
#include "config/settings.h"
#include "common/daemoncontroller.h"
#include "settings/utils/screenhelper.h"
#include "core/types/constants.h"
#include "core/types/enums.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorLayoutApi/LayoutSourceBundle.h>

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PlasmaZones::KZonesImporter {
struct ImportResult;
}

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PhosphorRules {
// Forward-declared for the `std::unique_ptr<RuleStore>` member
// below. The complete type is needed only in settingscontroller.cpp
// (where m_localRuleStore is constructed); pulling
// <PhosphorRules/RuleStore.h> into the header would force
// every consumer of this controller to re-parse the RuleStore
// dependency graph.
class RuleStore;
class RuleStoreWatcher;
}

namespace PlasmaZones {
class ShaderRegistry;
class ShaderPreviewController;
class RegistryShaderPreviewBackend;
}

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStack>
#include <QString>
#include <QDBusConnection>
#include <QUrl>
#include <QVariantList>
#include <QTimer>
#include <memory>
#include <optional>

#include <PhosphorControl/ApplicationController.h>

#include "settings/services/algorithmservice.h"
#include "settings/pages/animationspagecontroller.h"
#include "settings/pages/editorpagecontroller.h"
#include "settings/services/externaleditscope.h"
#include "settings/pages/generalpagecontroller.h"
#include "settings/pages/profilepagecontroller.h"
#include "settings/pages/snappingzonescontroller.h"
#include "settings/pages/snappingbehaviorcontroller.h"
#include "settings/pages/snappingeffectscontroller.h"
#include "settings/pages/snappingshaderspagecontroller.h"
#include "settings/pages/snappingzoneselectorcontroller.h"
#include "settings/pages/decorationpagecontroller.h"
#include "settings/services/stagingservice.h"
#include "settings/pages/tilingalgorithmcontroller.h"
#include "settings/pages/scrollingbehaviorcontroller.h"
#include "settings/pages/tilingbehaviorcontroller.h"
#include "settings/pages/windowappearancecontroller.h"
#include "settings/rules/rulecontroller.h"

namespace PlasmaZones {

class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)
    Q_PROPERTY(bool needsSave READ needsSave NOTIFY dirtyPagesChanged)
    Q_PROPERTY(QStringList dirtyPages READ dirtyPages NOTIFY dirtyPagesChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    // Simple/advanced UI mode. Runtime state only — persistence lives in the
    // QML layer (a QtCore.Settings block in Main.qml, the same mechanism the
    // filter/sort chrome uses), which pushes the restored value in at startup
    // and writes user flips back out. The setter mirrors the value into the
    // page registry (which filters the sidebar) and redirects away from a
    // page the new mode can't show. Pages bind card/row `visible:` to this.
    Q_PROPERTY(bool advancedMode READ advancedMode WRITE setAdvancedMode NOTIFY advancedModeChanged)
    Q_PROPERTY(QString activeDirtyScope READ activeDirtyScope NOTIFY activeDirtyScopeChanged)
    Q_PROPERTY(Settings* settings READ settings CONSTANT)
    Q_PROPERTY(DaemonController* daemonController READ daemonController CONSTANT)

    // What's New
    Q_PROPERTY(QString lastSeenWhatsNewVersion READ lastSeenWhatsNewVersion NOTIFY lastSeenWhatsNewVersionChanged)
    Q_PROPERTY(bool hasUnseenWhatsNew READ hasUnseenWhatsNew NOTIFY lastSeenWhatsNewVersionChanged)
    Q_PROPERTY(QVariantList whatsNewEntries READ whatsNewEntries CONSTANT)

    // PhosphorZones::Layout management
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    // The rule editor's ActiveLayout value-picker model: every entry of
    // `layouts`, with each native scrolling-template row rewritten to its
    // prefixed "scrolling:<uuid>" wire id and a "Template: …" label (nothing is
    // derived from manual layouts), plus the bare "scrolling:" sentinel entry
    // for "scrolling with no template". A rule can therefore target one
    // specific template the way it targets a snap layout or an autotile
    // algorithm.
    Q_PROPERTY(QVariantList activeLayoutMatchOptions READ activeLayoutMatchOptions NOTIFY layoutsChanged)

    // Screen management
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

    // Per-monitor scope: which screen the per-monitor setting groups are
    // currently editing. Empty = "All Monitors" (edit the global default).
    // App-wide and shared, so a pick persists as the user moves between
    // per-monitor pages — replaces the old per-page selectedScreenName that
    // each page tracked independently.
    Q_PROPERTY(QString scopeScreenName READ scopeScreenName WRITE setScopeScreenName NOTIFY scopeScreenNameChanged)

    // Editor page — properties live on EditorPageController, exposed here as a
    // child QObject so QML reads `settingsController.editorPage.duplicateShortcut`.
    Q_PROPERTY(EditorPageController* editorPage READ editorPage CONSTANT)

    // Snapping/Tiling/Scrolling behavior pages — trigger surfaces moved to per-page controllers.
    Q_PROPERTY(SnappingBehaviorController* snappingBehaviorPage READ snappingBehaviorPage CONSTANT)
    Q_PROPERTY(TilingBehaviorController* tilingBehaviorPage READ tilingBehaviorPage CONSTANT)
    Q_PROPERTY(ScrollingBehaviorController* scrollingBehaviorPage READ scrollingBehaviorPage CONSTANT)
    Q_PROPERTY(SnappingZoneSelectorController* snappingZoneSelectorPage READ snappingZoneSelectorPage CONSTANT)
    Q_PROPERTY(SnappingZonesController* snappingZonesPage READ snappingZonesPage CONSTANT)
    Q_PROPERTY(SnappingEffectsController* snappingEffectsPage READ snappingEffectsPage CONSTANT)
    Q_PROPERTY(SnappingShadersPageController* snappingShadersPage READ snappingShadersPage CONSTANT)
    Q_PROPERTY(WindowAppearanceController* windowAppearancePage READ windowAppearancePage CONSTANT)
    Q_PROPERTY(TilingAlgorithmController* tilingAlgorithmPage READ tilingAlgorithmPage CONSTANT)
    Q_PROPERTY(GeneralPageController* generalPage READ generalPage CONSTANT)
    Q_PROPERTY(AnimationsPageController* animationsPage READ animationsPage CONSTANT)
    // Decoration drill-down — per-surface chains of decoration shader packs,
    // resolved through a DecorationProfileTree. QML reads
    // `settingsController.decorationPage.<invokable>()`.
    Q_PROPERTY(DecorationPageController* decorationPage READ decorationPage CONSTANT)
    // Rules page — the unified rule surface. The controller owns one
    // RuleModel and talks to the daemon's org.plasmazones.Rules
    // adaptor; QML reads `settingsController.rulesPage.model`.
    Q_PROPERTY(RuleController* rulesPage READ rulesPage CONSTANT)

    // Profiles page — settings-profile CRUD + inheritance. QML reads the store
    // via `settingsController.profilesPage.bridge`.
    Q_PROPERTY(ProfilePageController* profilesPage READ profilesPage CONSTANT)

    // PhosphorControl ApplicationController hosting the PageRegistry that
    // SettingsAppWindow's sidebar / breadcrumbs / footer consume. Constructed
    // lazily after every page controller has been built so the registry
    // entries can carry stable PageController* pointers.
    Q_PROPERTY(PhosphorControl::ApplicationController* app READ app CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController() override;

    QString activePage() const
    {
        return m_activePage;
    }

    /// Switch the active settings page.
    ///
    /// Used by QML (via the `activePage` Q_PROPERTY WRITE), directly from
    /// `main.cpp` for the initial `--page` arg, and indirectly by
    /// `SettingsLaunchController::handleSetActivePage` when a second
    /// launcher forwards its `--page` request over D-Bus. Does not raise
    /// the window; the D-Bus forward path just updates state and lets the
    /// user focus the existing window themselves.
    void setActivePage(const QString& page);

    /// Navigate to an addressable target `pageId#anchor`. The page part is
    /// resolved + switched via setActivePage (parent→leaf redirect, dirty
    /// handling — identical to a sidebar click); the optional `#anchor`
    /// fragment is stashed as a deep-link reveal request keyed to the
    /// RESOLVED leaf page, so PageHost reveals it once the page is built.
    /// A fragment-free address behaves byte-for-byte like setActivePage.
    /// Entry point for `--page`/`--setting` CLI args and the D-Bus forward.
    Q_INVOKABLE void navigateTo(const QString& address);

    static const QSet<QString>& validPageNames();
    /// Resolve a page address to a navigable leaf id. Valid leaf names and
    /// unregistered ids pass through untouched; a registered virtual node
    /// (category parent, *-cat header) resolves to its first leaf visible
    /// under the CURRENT simple/advanced mode, falling back to its first
    /// leaf regardless of mode (the caller's mode gate then redirects) when
    /// the whole subtree is hidden. Derived from the live registry topology
    /// and the active mode instead of a hand-synced static table.
    QString resolveToLeaf(const QString& page) const;
    /// Condensed SimpleOnly page id → the advanced pages whose config keys
    /// it surfaces. These pages own no keys of their own, so their dirty
    /// state, Reset, and Discard all delegate to the backing pages (see
    /// isPageDirty / resetPage / discardPage), and a backing page's
    /// reconcile cascades to them (reconcilePageDirty).
    static const QHash<QString, QStringList>& simplePageBackingPages();
    /// Parent name → set of leaf child page names. Covers the top-level sidebar
    /// categories AND the mid-level virtual parents nested beneath them (among
    /// them snapping / tiling / scrolling under placement, and the animations-*
    /// parents whose children don't share their prefix). See the definition for
    /// the full classification rather than trusting a list here to stay
    /// current. Drives dirty-state propagation in `isPageDirty`.
    static const QHash<QString, QSet<QString>>& pageGroupChildren();

    bool needsSave() const
    {
        return !m_dirtyPages.isEmpty();
    }
    QStringList dirtyPages() const;
    /// Returns true if the page (or any of its children, for parent categories
    /// like "snapping" / "tiling") currently has unsaved changes. For pages in
    /// the per-page config manifest (@ref pageOwnedConfigKeys) the answer is
    /// value-based — any owned key differing from the committed baseline —
    /// which stays correct across a per-page Discard/Reset. The ordering,
    /// shortcuts, virtual-screens, animation and decoration pages are
    /// value-based too, each against its own staged state rather than the
    /// manifest, and the condensed simple pages (@ref simplePageBackingPages)
    /// answer with the union of their backing pages. Only a page in none of
    /// those groups falls back to the m_dirtyPages membership set.
    Q_INVOKABLE bool isPageDirty(const QString& page) const;

    // ── Per-page Reset / Discard (kebab menu in the breadcrumb row) ──────────
    /// True when @p page can be reset to defaults: config-manifest pages (schema
    /// defaults — this includes the Windows appearance page, whose Windows.* /
    /// Gaps.* keys are plain config), the ordering pages (drop the custom order),
    /// the shortcuts pages (unassign every quick slot), the virtual screens page
    /// (unsplit every monitor), the animation pages (clear overrides + reset
    /// animation keys), the decoration pages (clear their surface root), the
    /// condensed simple pages (delegate to their backing pages), and the parent
    /// categories (reset every resettable leaf of the group), which simple mode
    /// reaches through activeDirtyScope.
    Q_INVOKABLE bool pageSupportsReset(const QString& page) const;

    /// True when @p page can discard its own unsaved edits. Byte-identical to
    /// pageSupportsReset today; kept separate so the two kebab items can diverge.
    Q_INVOKABLE bool pageSupportsDiscard(const QString& page) const;
    /// The id whose dirty state @p pageId REPRESENTS, which for a condensed
    /// `SimpleOnly` page is the group it stands in for, so a badge covers the
    /// subtree's hidden leaves and its Discard can reach them. A page shown in
    /// both modes returns itself. Full walk and the `layouts`/`display` case
    /// that gates it: dirtyScopeFor in settingscontroller_pagetopology.cpp.
    Q_INVOKABLE QString dirtyScopeFor(const QString& pageId) const;
    /// `dirtyScopeFor(activePage)` as a NOTIFYing property. QML must bind this,
    /// not call the invokable directly: the invokable does not re-evaluate on a
    /// mode flip that leaves `activePage` in place, which stranded the page
    /// kebab's Discard scope on the pre-flip value.
    QString activeDirtyScope() const;

    /// Reset every config key owned by @p page to its schema default, staged
    /// for the user to Save or Discard (never persisted here). Manifest pages
    /// (including Windows appearance) reset their keys; the ordering / shortcuts /
    /// virtual-screens / animation / decoration pages reset through their own
    /// staged machinery; a condensed simple page delegates to each backing page,
    /// which resets those pages' FULL key sets (wider than the subset the simple
    /// page shows — see the pageSupportsReset definition); a parent category
    /// resets its group's resettable leaves. No-op for a page with none of those.
    Q_INVOKABLE void resetPage(const QString& page);

    /// Revert every config key owned by @p page to the committed baseline,
    /// dropping that page's unsaved edits while leaving other pages untouched.
    /// Handles the same special pages as resetPage (condensed simple pages
    /// included, with the same widened scope), plus parent categories
    /// (discards every discardable child leaf, skipping the simple pages whose
    /// backing pages are siblings in that set). No-op only for a page with
    /// neither a manifest entry, a special-case branch, nor a child group.
    Q_INVOKABLE void discardPage(const QString& page);

    /// The per-page config-key manifest: page id → the (group, key) pairs that
    /// page owns. Phase-1 scope is the KConfig-backed settings pages; pages not
    /// listed here revert through their own staged machinery rather than the
    /// config manifest (see resetPage / discardPage), not because they lack
    /// Reset/Discard. Public + static so the manifest (and its hand-maintained
    /// partition/schema invariants — see the definition) can be inspected
    /// without a SettingsController instance.
    static const QHash<QString, Settings::ConfigKeyList>& pageOwnedConfigKeys();

    /// The mode enable master switches: manifest-owned for dirty/save/discard
    /// but excluded from per-page Reset (a page Reset must not flip its mode).
    static const Settings::ConfigKeyList& resetExemptModeEnableKeys();

    /// Override the page that the next setNeedsSave(true) calls (and any
    /// property NOTIFY routed through onSettingsPropertyChanged) will mark
    /// dirty, instead of the currently active page. Use for changes made
    /// from sidebar / global widgets that mutate settings owned by a
    /// different page than the one the user is viewing.
    ///
    /// Pair with endExternalEdit() — the sidebar pattern is:
    ///     beginExternalEdit("snapping");
    ///     appSettings.snappingEnabled = newValue;
    ///     endExternalEdit();
    Q_INVOKABLE void beginExternalEdit(const QString& page);
    Q_INVOKABLE void endExternalEdit();

    bool daemonRunning() const
    {
        return m_daemonController.isRunning();
    }

    bool advancedMode() const
    {
        return m_advancedMode;
    }
    void setAdvancedMode(bool advanced);

    Settings* settings()
    {
        return &m_settings;
    }
    DaemonController* daemonController()
    {
        return &m_daemonController;
    }
    // What's New
    QString lastSeenWhatsNewVersion() const
    {
        return m_lastSeenWhatsNewVersion;
    }
    bool hasUnseenWhatsNew() const;
    QVariantList whatsNewEntries() const
    {
        return m_whatsNewEntries;
    }
    Q_INVOKABLE void markWhatsNewSeen();

private:
    /// Highest entry version in m_whatsNewEntries, or empty if no entries.
    QString latestWhatsNewVersion() const;

public:
    // PhosphorZones::Layout accessors
    QVariantList layouts() const
    {
        return m_layouts;
    }

    /// The rules-editor ActiveLayout picker model. Impl in
    /// settingscontroller_layouts.cpp; recomputed per read (rule editing is
    /// not a hot path) and change-notified by layoutsChanged.
    QVariantList activeLayoutMatchOptions() const;

    /// The options an enum-valued setting's picker should offer, as
    /// `[{ text, value }, ...]` in declaration order — a WideComboBox model
    /// with textRole "text" / valueRole "value". Values come from the config
    /// schema's choices (filtered by SettingsValueLabels::uiChoiceSubset) and
    /// words from the label table, so a picker cannot drift from the key.
    /// Empty list + warning for a key with no declared choices.
    Q_INVOKABLE QVariantList valueOptions(const QString& group, const QString& key) const;

    /// The scrolling kind vocabularies and value bounds, from ConfigDefaults.
    /// See the definition for what the map carries and why.
    Q_INVOKABLE QVariantMap scrollingConstants() const;

    // ─── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───
    // On-disk layouts read through an in-process registry so QML preview paths
    // render with the daemon down. What the projection carries, what the D-Bus
    // path adds on top, and the autotile parameter drift: see the definition in
    // settingscontroller_layouts.cpp.
    Q_INVOKABLE QVariantList localLayoutPreviews() const;

    // Screen accessors
    QVariantList screens() const
    {
        return m_screenHelper.screens();
    }
    Q_INVOKABLE QVariantMap physicalScreenResolution(const QString& screenId) const;

    QString scopeScreenName() const
    {
        return m_scopeScreenName;
    }
    void setScopeScreenName(const QString& name);

    /// Physical-output id for a screen name token, collapsing a virtual-screen
    /// id ("id/vs:N") to its physical parent. Single source of truth for QML
    /// that needs the physical id — the canonical "/vs:" separator lives in
    /// C++ (PhosphorIdentity::VirtualScreenId), so QML must not re-spell it.
    Q_INVOKABLE QString physicalScreenId(const QString& name) const;

    // Virtual desktops / activities (reactive via D-Bus signals)
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopsChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopsChanged)
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesChanged)
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY activitiesChanged)

    int virtualDesktopCount() const
    {
        return m_virtualDesktopCount;
    }
    QStringList virtualDesktopNames() const
    {
        return m_virtualDesktopNames;
    }
    bool activitiesAvailable() const
    {
        return m_activitiesAvailable;
    }
    QVariantList activities() const
    {
        return m_activities;
    }
    QString currentActivity() const
    {
        return m_currentActivity;
    }

    // PhosphorZones::Layout CRUD (D-Bus to daemon)
    Q_INVOKABLE void createNewLayout();
    Q_INVOKABLE bool createNewLayout(const QString& name, const QString& type, int aspectRatioClass, bool openInEditor);
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);

    // Native scrolling-template CRUD (daemon-first; the local store is a
    // read view refreshed on scrollingTemplatesChanged). The layouts model
    // already carries the template entries (isScrollingTemplate flag);
    // scrollingTemplateForEditing answers the full column/default detail the
    // editor form needs.
    Q_INVOKABLE QVariantMap scrollingTemplateForEditing(const QString& templateId) const;
    /// D-Bus subscription slot: reload the local template store, then run
    /// the debounced layout refresh.
    Q_SLOT void onScrollingTemplatesChanged();
    Q_INVOKABLE bool saveScrollingTemplate(const QVariantMap& templateData);
    /// The id-returning form of saveScrollingTemplate, for callers that mint a
    /// NEW template and want the list to select it once the refresh lands
    /// (import). Empty on refusal. Not Q_INVOKABLE: QML saves through the bool
    /// form, which is this one's caller.
    QString saveScrollingTemplateReturningId(const QVariantMap& templateData);
    Q_INVOKABLE void deleteScrollingTemplate(const QString& templateId);
    Q_INVOKABLE void duplicateScrollingTemplate(const QString& templateId);
    /// Import mints a fresh id and routes through the daemon-first save;
    /// export writes the persisted schema from the local read view.
    Q_INVOKABLE void importScrollingTemplate(const QString& filePath);
    Q_INVOKABLE void exportScrollingTemplate(const QString& templateId, const QString& filePath);
    Q_INVOKABLE void openScrollingTemplatesFolder();
    Q_INVOKABLE void openScrollingTemplateFile(const QString& templateId);
    Q_INVOKABLE void editLayout(const QString& layoutId);
    Q_INVOKABLE void editLayoutOnScreen(const QString& layoutId, const QString& screenId);
    Q_INVOKABLE void openLayoutsFolder();
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& layoutId, const QString& filePath);

    // KZones import
    Q_INVOKABLE bool hasKZonesConfig();
    Q_INVOKABLE int importFromKZones();
    Q_INVOKABLE int importFromKZonesFile(const QString& filePath);
    Q_INVOKABLE void setLayoutHidden(const QString& layoutId, bool hidden);
    Q_INVOKABLE void setLayoutAutoAssign(const QString& layoutId, bool enabled);
    Q_INVOKABLE void setLayoutAspectRatio(const QString& layoutId, int aspectRatioClass);

    // Font helpers (for FontPickerDialog)
    Q_INVOKABLE QStringList fontStylesForFamily(const QString& family) const;
    Q_INVOKABLE int fontStyleWeight(const QString& family, const QString& style) const;
    Q_INVOKABLE bool fontStyleItalic(const QString& family, const QString& style) const;

    // Quick layout slots (D-Bus to daemon)
    Q_INVOKABLE QString getQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE QString getScrollingQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setScrollingQuickLayoutSlot(int slotNumber, const QString& templateId);
    Q_INVOKABLE void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    Q_INVOKABLE QString getQuickLayoutShortcut(int slotNumber) const;
    Q_INVOKABLE QString getTilingQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);

    /// Convert a file:// URL from Qt's FileDialog to a local filesystem
    /// path. Replaces ad-hoc regex stripping in QML — QUrl::toLocalFile()
    /// handles percent-decoding, embedded query/fragment, and non-trivial
    /// schemes that the QML-side regex would silently mishandle.
    Q_INVOKABLE QString urlToLocalFile(const QUrl& url) const;

    // ── Page sub-controllers ─────────────────────────────────────────────
    EditorPageController* editorPage() const
    {
        return m_editorPage;
    }
    SnappingBehaviorController* snappingBehaviorPage() const;
    TilingBehaviorController* tilingBehaviorPage() const;
    ScrollingBehaviorController* scrollingBehaviorPage() const;
    SnappingZoneSelectorController* snappingZoneSelectorPage() const
    {
        return m_snappingZoneSelectorPage;
    }
    SnappingZonesController* snappingZonesPage() const;
    SnappingEffectsController* snappingEffectsPage() const;
    SnappingShadersPageController* snappingShadersPage() const;
    WindowAppearanceController* windowAppearancePage() const;
    TilingAlgorithmController* tilingAlgorithmPage() const;
    GeneralPageController* generalPage() const
    {
        return m_generalPage;
    }
    AnimationsPageController* animationsPage() const
    {
        return m_animationsPage;
    }
    DecorationPageController* decorationPage() const
    {
        return m_decorationPage;
    }
    RuleController* rulesPage() const
    {
        return m_rulesPage;
    }
    ProfilePageController* profilesPage() const
    {
        return m_profilesPage;
    }

    PhosphorControl::ApplicationController* app() const
    {
        return m_app.get();
    }

    // ── Running window picker (async flow) ──────────────────────────────────
    // The QML picker dialog calls requestRunningWindows() and binds to
    // runningWindowsAvailable(list) — no blocking D-Bus round-trip. Cache
    // invalidation and the client-side timeout: see the definition in
    // settingscontroller_session.cpp.
    Q_INVOKABLE void requestRunningWindows();
    Q_INVOKABLE QVariantList cachedRunningWindows() const
    {
        return m_cachedRunningWindows;
    }

    // ── Config export/import ────────────────────────────────────────────────
    Q_INVOKABLE bool exportAllSettings(const QString& filePath);
    Q_INVOKABLE bool importAllSettings(const QString& filePath);

    // ── Screen state query ─────────────────────────────────────────────────
    Q_INVOKABLE QVariantList getScreenStates() const;
    /// The live scrolling strip of @p screenId as zone maps for
    /// LayoutThumbnail (relativeGeometry + zoneNumber per visible tile),
    /// fetched from org.plasmazones.Scrolling. Empty when the screen has
    /// no strip right now (not scrolling, no windows, daemon down) — the
    /// Monitors page then falls back to a representative static strip.
    Q_INVOKABLE QVariantList getScrollingStripPreview(const QString& screenId) const;
    /// The staged (not yet applied) assignment for the (screen × desktop ×
    /// activity) context, as a map of only the fields that are actually staged.
    ///
    /// Key ABSENCE is meaningful and spans three files: staging collapses an
    /// EMPTY id to "not staged" on the way in (StagingService maps it to
    /// nullopt), so once an entry is staged, an absent "layoutId" or
    /// "algorithmId" here is the echo of a staged CLEAR of that field. A
    /// present key always carries a non-empty id, and a producer must never
    /// insert an empty-string value expecting it to read back as a distinct
    /// clear state — there is no such state on this map. An absent "mode"
    /// means neither an explicit mode nor an inferable one was staged.
    Q_INVOKABLE QVariantMap getStagedAssignment(const QString& screenName, int virtualDesktop = 0,
                                                const QString& activityId = QString()) const;

    // ── Atomic mode+layout staging (overview page) ──────────────────────────
    Q_INVOKABLE void stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                          int mode, const QString& snappingLayoutId, const QString& tilingAlgorithmId);
    /// Remove any staged entry for the (screen × desktop × activity)
    /// assignment context — a true unstage: on Apply the context's
    /// daemon-side assignment is left untouched.
    ///
    /// No QML page calls this today. It is kept as the staging surface's
    /// inverse: stageAssignmentEntry is the only way in, and a page that stages
    /// a pick and then wants to take it back (rather than stage the opposite)
    /// has no other route. Deleting it would leave the surface one-way.
    Q_INVOKABLE void removeStagedAssignment(const QString& screenName, int virtualDesktop, const QString& activityId);

    // ── Ordering helpers (staged — flushed to settings on save) ────────────
    Q_INVOKABLE QVariantList resolvedSnappingOrder() const;
    Q_INVOKABLE QVariantList resolvedTilingOrder() const;
    Q_INVOKABLE void moveSnappingLayout(int fromIndex, int toIndex);
    Q_INVOKABLE void moveTilingAlgorithm(int fromIndex, int toIndex);
    Q_INVOKABLE void resetSnappingOrder();
    Q_INVOKABLE void resetTilingOrder();
    Q_INVOKABLE bool hasCustomSnappingOrder() const;
    Q_INVOKABLE bool hasCustomTilingOrder() const;
    Q_INVOKABLE QStringList effectiveSnappingOrder() const;
    Q_INVOKABLE QStringList effectiveTilingOrder() const;

    // ── Algorithm helpers ────────────────────────────────────────────────────
    // Q_PROPERTY for reactive QML bindings; Q_INVOKABLE retained for legacy
    // imperative call sites (wizard preview refresh, etc).
    Q_PROPERTY(QVariantList availableAlgorithms READ availableAlgorithms NOTIFY availableAlgorithmsChanged)
    Q_INVOKABLE QVariantList availableAlgorithms() const;
    Q_INVOKABLE QVariantList generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                                      int masterCount, const QVariantMap& customParams) const;
    Q_INVOKABLE QVariantList generateAlgorithmDefaultPreview(const QString& algorithmId) const;
    Q_INVOKABLE void openAlgorithmsFolder();
    Q_INVOKABLE QString createNewAlgorithm(const QString& name, const QString& baseTemplate,
                                           const QVariantMap& capabilities);
    Q_INVOKABLE bool importAlgorithm(const QString& filePath);
    Q_INVOKABLE static QString algorithmIdFromLayoutId(const QString& layoutId);
    Q_INVOKABLE void openAlgorithm(const QString& algorithmId);
    Q_INVOKABLE void openLayoutFile(const QString& layoutId);
    Q_INVOKABLE bool deleteAlgorithm(const QString& algorithmId);
    Q_INVOKABLE bool duplicateAlgorithm(const QString& algorithmId);
    Q_INVOKABLE bool exportAlgorithm(const QString& algorithmId, const QString& destPath);

    // NOTE: customParamsForAlgorithm / setCustomParam / customParamChanged
    // have moved to TilingAlgorithmController.

    // ── Per-screen autotile overrides ────────────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenName) const;
    // The Algorithm sub-domain of the shared autotile map, so the Tiling Algorithm
    // card's scope chip dot/reset only touches its own keys.
    Q_INVOKABLE bool hasPerScreenAutotileAlgorithmSettings(const QString& screenName) const;
    Q_INVOKABLE void clearPerScreenAutotileAlgorithmSettings(const QString& screenName);

    // ── Per-screen scrolling overrides ───────────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenScrollingSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenScrollingSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenScrollingSettings(const QString& screenName);
    // No sub-domain split: the scrolling map carries only the New-columns
    // card's sizing keys, so the whole-domain pair is that card's chip.
    Q_INVOKABLE bool hasPerScreenScrollingSettings(const QString& screenName) const;

    // Per-screen gaps are config-backed: a per-monitor override is the gap-
    // dimension sub-domain of the per-screen autotile store (unified snap+tile).
    // The Gaps card's monitor scope chip drives these; the gap controls
    // read/write via WindowAppearanceController's gapValue/writeGap.
    Q_INVOKABLE bool hasPerScreenGapOverride(const QString& screenName) const;
    Q_INVOKABLE void clearPerScreenGapOverride(const QString& screenName);

    // ── Virtual screen configuration ──────────────────────────────────────────
    Q_INVOKABLE QVariantList getVirtualScreenConfig(const QString& physicalScreenId) const;
    // No immediate apply/remove pair, deliberately: every writer goes through
    // the staging methods below, so a virtual-screen edit lands on the same
    // Apply / Discard footer as everything else. The two direct Q_INVOKABLEs
    // that used to sit here had no caller and bypassed the staging entirely.
    // ── Staged virtual screen configuration (flushed on Apply) ──────────────
    Q_INVOKABLE void stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens);
    Q_INVOKABLE void stageVirtualScreenRemoval(const QString& physicalScreenId);
    Q_INVOKABLE bool hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const;
    Q_INVOKABLE QVariantList getStagedVirtualScreenConfig(const QString& physicalScreenId) const;

    // ── Per-screen zone selector overrides ───────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value);
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenName) const;

    Q_INVOKABLE QVariantMap loadWindowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void defaults();
    Q_INVOKABLE void launchEditor();

    /// Why a `resetPage` refused, carried by `pageResetFailed`. These are
    /// stable tokens the shell branches on to pick wording, not user-facing
    /// text: i18n lives in QML in this tree.
    static constexpr QLatin1String ReasonDaemonUnreachable{"daemon-unreachable"};
    static constexpr QLatin1String ReasonOverridesNotCleared{"overrides-not-cleared"};
    /// The cleared configuration could not be written, so nothing was reset.
    /// Raised by the global `defaults()` with an EMPTY page id — a factory reset
    /// is not scoped to a page.
    static constexpr QLatin1String ReasonResetNotWritten{"reset-not-written"};

Q_SIGNALS:
    void activePageChanged();
    void dirtyPagesChanged();
    /// Emitted after a `save()` call has fully completed, including the
    /// deferred `singleShot(0)` reset of the internal `m_saving` guard
    /// that drains lingering daemon broadcasts. Consumers that need to
    /// chain post-save bookkeeping (e.g. SettingsStagingDomain's async
    /// applyResult emission) MUST wait for this signal rather than
    /// inspecting state on `save()` return — the m_saving flag is still
    /// set when save() returns and a second apply can race the open
    /// window otherwise.
    void savingFinished();
    void daemonRunningChanged();
    void advancedModeChanged();
    /// Emitted whenever `activeDirtyScope` may have moved: the active page
    /// changed, or the mode flip changed which pages are visible and therefore
    /// whether the active one is still the sole representative of its group.
    void activeDirtyScopeChanged();
    void layoutsChanged();
    void layoutAdded(const QString& layoutId);
    void availableAlgorithmsChanged();
    void algorithmCreated(const QString& algorithmId);
    void algorithmOperationFailed(const QString& reason);
    void layoutOperationFailed(const QString& reason);
    /// Emitted when exportAllSettings / importAllSettings gives up, and on the
    /// partial-success path (import landed but animation pages still hold
    /// pre-import snapshots). Both return a bool too, but no caller sequences on
    /// it: a refused path, a vanished file, and a non-settings file are one
    /// `false` wanting different words, which only this signal carries.
    void settingsTransferFailed(const QString& reason);
    /// Emitted when `resetPage` refuses. resetPage returns void and stages
    /// nothing on that path, so without this the page reconciles CLEAN and the
    /// refusal is indistinguishable from a page already at its defaults. Two
    /// branches emit it for unrelated reasons the shell must word differently:
    ///   * `ReasonDaemonUnreachable` — a value resetPage must READ first is
    ///     unavailable, currently the daemon's quick-layout slot map.
    ///   * `ReasonOverridesNotCleared` — a WRITE was refused: an animation or
    ///     decoration override could not be cleared (an async discard still owns
    ///     the snapshot map, or a file could not be removed). The daemon is fine.
    /// @p reason is one of the Reason* constants above, NOT user-facing text:
    /// the shell wires i18n in QML and branches on the token.
    void pageResetFailed(const QString& page, const QString& reason);
    /// Emitted when `discardPage` refuses — the Discard analogue of
    /// pageResetFailed. The animation branch reconciles value-based, so a
    /// refused revert leaves the page BADGED with no other word. The only
    /// refusal is a WRITE that could not complete, so `reason` is always
    /// `ReasonOverridesNotCleared`; the branch checks `asyncRevertInFlight()`
    /// first so a benign refusal during a global async discard never emits.
    void pageDiscardFailed(const QString& page, const QString& reason);
    void screensChanged();
    void scopeScreenNameChanged();
    /// Emitted whenever any per-screen override map changes (set or clear,
    /// any domain). The monitor scope map re-polls hasPerScreen*Settings()
    /// to refresh its per-output override dots, which a plain WRITE on an
    /// individual key can't drive on its own.
    void perScreenOverridesChanged();
    void lastSeenWhatsNewVersionChanged();

    // Virtual desktop / activity / assignment signals
    void virtualDesktopsChanged();
    void activitiesChanged();
    void screenLayoutChanged();
    void quickLayoutSlotsChanged();

    /**
     * @brief Fresh running-windows list has arrived from the daemon.
     *
     * Emitted in response to requestRunningWindows(). The @p windows list
     * is a QVariantList of {windowClass, appName, caption, desktopFile}
     * maps ready for QML consumption. Also updates cachedRunningWindows()
     * so later queries can read the last-seen value synchronously.
     */
    void runningWindowsAvailable(const QVariantList& windows);

    /**
     * @brief The running-windows request produced no data.
     *
     * Emitted by the client-side timeout timer when the KWin effect never
     * answers a requestRunningWindows() call within RunningWindowsTimeoutMs
     * (effect unloaded, crashed, or slow). ALSO emitted synchronously from
     * requestRunningWindows() itself when the daemon is not running: the
     * request is never dispatched at all, so waiting out the full timeout
     * would only delay the same outcome.
     *
     * QML dialogs should surface an error state so the user can distinguish
     * "no windows" from "daemon or effect not responding". Note:
     * cachedRunningWindows() is empty by the time this fires — both paths
     * invalidate the cache before emitting, so the signal always means
     * "no data, refresh failed."
     */
    void runningWindowsTimedOut();

    // KZones import signals
    void kzonesImportFinished(int count, const QString& message);

    // Ordering staged signals
    void stagedSnappingOrderChanged();
    void stagedTilingOrderChanged();

    // Internal forwarder for the Settings-NOTIFY meta-object loop —
    // see ctor for rationale (QMetaMethod::fromSignal vs indexOfSlot).
    void _settingsPropertyNotifyForwarder();

private Q_SLOTS:
    void onExternalSettingsChanged();
    void onSettingsPropertyChanged();
    void loadLayoutsAsync();
    // Debounce slot: all layout-mutation D-Bus signals (layoutCreated,
    // layoutDeleted, layoutChanged, layoutPropertyChanged, layoutListChanged)
    // route here so bursts coalesce into one loadLayoutsAsync() on the
    // 50 ms m_layoutLoadTimer. Reachable by SLOT() because it is a slot at
    // all: the string-based connect resolves through the meta-object, which
    // carries private slots and public ones alike.
    void scheduleLayoutLoad();
    void onVirtualDesktopsChanged();
    void onActivitiesChanged();
    void onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop);

    /**
     * @brief Handle SettingsAdaptor::runningWindowsAvailable D-Bus signal.
     *
     * Parses the JSON payload into a QVariantList of window maps, stores
     * it in m_cachedRunningWindows, and emits the QML-facing
     * runningWindowsAvailable(list) signal.
     */
    void onRunningWindowsAvailable(const QString& json);

    /// Daemon Rules.rulesChanged → reload m_localRuleStore so the
    /// in-process LayoutRegistry assignment cascade sees rule edits.
    void reloadLocalRuleStore(bool persisted);

private:
    void setNeedsSave(bool needs);
    /// Reload deferred by onExternalSettingsChanged() while edits were
    /// pending — fire it once the app is fully clean again. Connected to
    /// dirtyPagesChanged in the ctor: that is the one signal every
    /// clean-transition path emits (footer save/discard, per-page kebab
    /// Discard, the virtual-screens discard branch).
    void maybeDrainPendingExternalReload();
    // Sync m_dirtyPages membership for @p page to its value-based dirty state
    // (isPageDirty) after a per-page Reset/Discard, emitting dirtyPagesChanged
    // when it flips so the footer's global needsSave stays consistent. Used for
    // any page whose isPageDirty is value-based — manifest, ordering, shortcuts,
    // animation and decoration pages.
    //
    // ALSO cascades to any condensed simple page backed by @p page (see
    // simplePageBackingPages): reverting a backing page must clear the stale
    // entry on the simple leaf the edit was attributed to while the user was
    // in simple mode. Both syncs share one dirtyPagesChanged emit.
    void reconcilePageDirty(const QString& page);
    /// Set @p page's m_dirtyPages membership to @p dirty, returning whether the
    /// membership actually flipped. The one place a SINGLE page's membership is
    /// written: the reconcile helpers, setNeedsSave, and the Reset/Discard
    /// branches that own their own staged state all go through this, so
    /// insert/remove and "did anything change" can never drift apart. Does not
    /// emit — the caller owns the batching.
    ///
    /// Whole-set operations are the exception and do not route through it:
    /// setNeedsSave(false) clears the set outright, and load() replaces it with
    /// the full page set. Neither is a per-page decision, and both compare the
    /// whole set before emitting.
    bool syncDirtyMembership(const QString& page, bool dirty);
    /// RAII batch window for the above: defers dirtyPagesChanged for the
    /// enclosing scope so a delegated Reset/Discard that walks several backing
    /// pages emits one NOTIFY instead of one per page. Nestable — only the
    /// outermost scope fires, and only if something actually flipped.
    ///
    /// DEFINED HERE, not in a .cpp. It is constructed from
    /// settingscontroller_pagereset.cpp,
    /// so an out-of-line definition made the latter a non-compiling
    /// translation unit that only linked because CMAKE_UNITY_BUILD happened to
    /// merge the two files into one batch. Adding sources ahead of them in the
    /// CMake list could split the batch and break the build with no source
    /// change. Same rationale as the inline definitions in
    /// animations_controller_detail.h.
    class DirtyEmitScope
    {
    public:
        explicit DirtyEmitScope(SettingsController& c)
            : m_c(c)
        {
            ++m_c.m_dirtyEmitDepth;
        }
        ~DirtyEmitScope()
        {
            if (--m_c.m_dirtyEmitDepth == 0 && m_c.m_dirtyEmitPending) {
                m_c.m_dirtyEmitPending = false;
                Q_EMIT m_c.dirtyPagesChanged();
            }
        }
        Q_DISABLE_COPY_MOVE(DirtyEmitScope)

    private:
        SettingsController& m_c;
    };
    /// Emit dirtyPagesChanged, or record it as pending when a DirtyEmitScope
    /// is open.
    void emitDirtyPagesChanged();
    // Batched variant for shared-domain groups (animation / decoration leaves):
    // reconciles every listed page but emits dirtyPagesChanged at most once,
    // matching the discard paths' single-emit discipline.
    void reconcilePagesDirty(const QSet<QString>& pages);
    // Value-based dirty attribution for the Rules page (the only page backed by
    // the RuleController model now that appearance is config): set m_dirtyPages
    // membership for "rules" (= userRulesDirty), emitting dirtyPagesChanged on a
    // change. Called on every rule-model mutation and on revert/apply completion.
    void reconcileRuleBackedDirty();
    /// Re-read the virtual-desktop count and names from the daemon. Returns
    /// whether BOTH reads succeeded. A failed count read falls the member back to
    /// 1 so QML stops rendering desktop indices the daemon no longer enumerates.
    /// That fallback is a display value: any caller that destroys state on the
    /// strength of the count (the disabled-desktop pruner) must check this return
    /// first, or a daemon hiccup reads as "every desktop but the first is gone".
    bool refreshVirtualDesktops();
    /// Void by design, unlike its virtual-desktop twin: every failing branch
    /// CLEARS the state it could not refresh, so the emptiness the pruner
    /// already gates on carries the failure. A destructive caller that needs to
    /// tell "no activities" from "could not ask" must give this a bool return.
    void refreshActivities();

    /// Stage an empty layout id for every quick-layout slot of @p wireMode
    /// that currently holds one (a slot's default IS "no assignment"), setting
    /// @p stagedAny to whether anything was staged. False when the daemon's slot
    /// map could not be read, and then NOTHING is staged: an error map reads the
    /// same as "all slots already unassigned", so reporting success would show a
    /// clean page for a reset that never happened. Shared by per-page Reset and
    /// defaults() — quick slots are daemon-backed, so Settings::reset() cannot
    /// clear them. @p wireMode is an AssignmentEntry::Mode value on the wire (see
    /// the QuickSlotMode* constants in settingscontroller_pagekeys.h).
    bool stageQuickSlotClears(int wireMode, bool& stagedAny);

    /// Adopt whatever is on disk as the session's state: reload settings and the
    /// local rule store, re-fetch the daemon's rules into the rules page, refresh
    /// screens and layouts, and drop every staged edit. Shared by load() (the
    /// Discard path) and the config-import success path, which need exactly the
    /// same thing done to the in-memory session. The whole body runs under
    /// m_loading, and the caller owns the trailing setNeedsSave(false).
    ///
    /// @param treatAsyncRevertAsClean whether an animation revert refused because
    ///        an async discard already owns the snapshot map counts as clean. True
    ///        on Discard, where that worker IS the restore. False on import, where
    ///        the snapshots hold pre-import content for files just rewritten.
    /// @return whether the animation page's snapshots came back clean. False means
    ///         the adopt only partly landed and needsSave must not be cleared.
    bool adoptOnDiskState(bool treatAsyncRevertAsClean);

    /// Shared tail of the two KZones import entry points: stash the layout to
    /// auto-select, schedule the layout refresh, report count and message to QML,
    /// and return the count both Q_INVOKABLEs hand back.
    int finishKZonesImport(const KZonesImporter::ImportResult& result);

    /// Single Rule store shared by m_settings (disable lists) and the
    /// LayoutRegistry. Declared FIRST so it outlives all borrowers.
    std::unique_ptr<PhosphorRules::RuleStore> m_localRuleStore;
    /// Opt-in cross-process auto-reload of m_localRuleStore on external writes
    /// (mainly the no-daemon case). Declared after the store; tears down first.
    std::unique_ptr<PhosphorRules::RuleStoreWatcher> m_localRuleStoreWatcher;
    /// Installs the process-global screen-id resolver before `m_settings`, whose
    /// constructor load()s and canonicalises per-screen override keys via
    /// `idForName`. Declared (and initialised) immediately before `m_settings`
    /// so member-init order guarantees the resolver is ready for that first
    /// migration on EVERY construction path. The stored value is unused.
    [[maybe_unused]] const bool m_screenIdResolverReady;
    Settings m_settings;
    /// Per-monitor editing scope; empty = "All Monitors". See the
    /// scopeScreenName Q_PROPERTY. Plain UI state, not persisted.
    QString m_scopeScreenName;
    /// Per-page sub-controllers: expose the Q_PROPERTY surface for a single
    /// settings page each. Parented to `this`, so Qt handles cleanup via
    /// ~QObject AFTER the member destructors below have run. Any
    /// sub-controller that borrows a unique_ptr member (e.g., the algorithm
    /// registry for TilingAlgorithmController) must instead be declared as
    /// a `std::unique_ptr<>` AFTER the borrowed member — see
    /// m_tilingAlgorithmPage below.
    EditorPageController* m_editorPage = nullptr;
    SnappingBehaviorController* m_snappingBehaviorPage = nullptr;
    TilingBehaviorController* m_tilingBehaviorPage = nullptr;
    ScrollingBehaviorController* m_scrollingBehaviorPage = nullptr;
    SnappingZoneSelectorController* m_snappingZoneSelectorPage = nullptr;
    SnappingZonesController* m_snappingZonesPage = nullptr;
    SnappingEffectsController* m_snappingEffectsPage = nullptr;
    WindowAppearanceController* m_windowAppearancePage = nullptr;
    GeneralPageController* m_generalPage = nullptr;
    /// Parented to `this` so Qt manages lifetime. Every consumer is also a child
    /// of this controller, and `~QObject` deletes children in INSERTION order
    /// (QObjectPrivate::deleteChildren walks the child list front to back — it
    /// is not a reverse walk). This registry is constructed before
    /// m_animationsPage, so it is destroyed BEFORE the page, and the page's
    /// non-owned pointer to it dangles for the remainder of the teardown. That
    /// is safe only because ~AnimationsPageController is `= default` and touches
    /// nothing through the pointer. Anything added to that dtor which reaches
    /// this registry is a use-after-free — construct the registry AFTER the page
    /// (or reparent it) before writing such a dtor.
    PhosphorAnimationShaders::AnimationShaderRegistry* m_animationShaderRegistry = nullptr;
    AnimationsPageController* m_animationsPage = nullptr;
    /// Settings-side mirror of the daemon's/compositor's surface-shader
    /// registry — drives the Decoration page's per-surface pack chains. Same
    /// parent / construction-order situation as `m_animationShaderRegistry`
    /// above: a QObject child of `this` constructed before `m_decorationPage`,
    /// so insertion-order child deletion tears the registry down FIRST and the
    /// page's non-owned registry pointer dangles through its own destruction.
    /// Safe only while `~DecorationPageController` stays `= default`.
    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_surfaceShaderRegistry = nullptr;
    DecorationPageController* m_decorationPage = nullptr;
    /// Rules page sub-controller. Parented to `this`; owns its
    /// RuleModel internally. Constructed after m_animationsPage so its
    /// dirty-tracking connection is wired in the same ctor block.
    RuleController* m_rulesPage = nullptr;
    /// Profiles page sub-controller. Parented to `this`; owns its ProfileStore
    /// internally. Registered via regPage so its active-pointer staging
    /// participates in the framework's Save/Discard.
    ProfilePageController* m_profilesPage = nullptr;
    /// Settings-side mirror of the daemon's overlay-shader registry —
    /// drives the read-only Snapping → Shaders browser. Same parent /
    /// construction-order situation as `m_animationShaderRegistry` above.
    /// The companion `m_snappingShadersPage` is declared further down as
    /// a `std::unique_ptr<>` (after `m_localLayoutManager`) because that
    /// page borrows the layout registry — see the declaration-order
    /// invariant block below.
    PlasmaZones::ShaderRegistry* m_overlayShaderRegistry = nullptr;

    // Shared zone-shader live-preview feed for the overlay-shader browser
    // (T3.1). The backend borrows m_overlayShaderRegistry + m_settings; the
    // controller borrows the backend. Declared backend-before-controller so
    // reverse member destruction tears the controller down first;
    // m_snappingShadersPage (declared later) borrows the controller and is
    // destroyed before it.
    std::unique_ptr<RegistryShaderPreviewBackend> m_shaderPreviewBackend;
    std::unique_ptr<ShaderPreviewController> m_shaderPreviewController;

    DaemonController m_daemonController;
    QString m_lastSeenWhatsNewVersion;
    QVariantList m_whatsNewEntries;
    ScreenHelper m_screenHelper;
    /// Simple/advanced UI mode. THE single source of the default: false
    /// (simple) so a brand-new user lands in the pared-down rail. The QML
    /// QtCore.Settings block in Main.qml derives its no-stored-value default
    /// from this at startup and restores the remembered choice for returning
    /// users; buildApplicationController seeds the registry's showAdvanced
    /// from it before the first sidebar build.
    bool m_advancedMode = false;
    QString m_activePage = QStringLiteral("overview");
    QSet<QString> m_dirtyPages;
    /// Depth counter for deferred dirtyPagesChanged emission. While > 0 the
    /// reconcile helpers mutate m_dirtyPages but record the NOTIFY in
    /// m_dirtyEmitPending instead of firing it, so a delegated Reset/Discard
    /// that walks several backing pages still emits once — the same
    /// single-emit discipline reconcilePagesDirty gives shared-domain groups.
    int m_dirtyEmitDepth = 0;
    bool m_dirtyEmitPending = false;
    /// Stack of external-edit page ids — `setNeedsSave(true)` targets
    /// `top()` instead of `m_activePage`. Nesting-aware so an inner
    /// begin/end pair restores the outer target on pop rather than
    /// clearing it. See ExternalEditScope for the RAII wrapper.
    QStack<QString> m_externalEditStack;
    bool m_saving = false;
    bool m_loading = false;
    /// A daemon settingsChanged broadcast arrived while local edits were
    /// pending, so onExternalSettingsChanged() deferred the reload instead
    /// of clobbering them. Drained (one queued reload) by
    /// maybeDrainPendingExternalReload() when the app next transitions to
    /// fully clean; cleared without a reload by save(), whose whole-schema
    /// flush supersedes the external state the reload would have adopted.
    bool m_pendingExternalReload = false;
    /// Reentrancy guard for setActivePage(). A slot connected to
    /// activePageChanged that calls back into setActivePage would
    /// otherwise corrupt the m_loading toggle window.
    bool m_settingActivePage = false;

    // PhosphorZones::Layout state
    QVariantList m_layouts;
    QTimer m_layoutLoadTimer;
    QString m_pendingSelectLayoutId;

    // Number of getLayoutList round-trips in flight; non-zero withholds the
    // unenriched local-path layout view into m_withheldLocalLayouts. Why it is
    // a count and when each is written: see loadLayoutsAsync in
    // settingscontroller_layouts.cpp.
    int m_pendingDaemonLayoutCalls = 0;
    std::optional<QVariantList> m_withheldLocalLayouts;

    // Daemon-independent layout source — see localLayoutPreviews() doc.
    // PhosphorZones::LayoutRegistry opens its own assignments backend + scans the standard
    // layouts directory; the bundle's composite aggregates manual + autotile
    // entries so consumers query a single ILayoutSource and never branch on
    // id-prefix.
    //
    // ─── DECLARATION ORDER INVARIANT ─────────────────────────────────
    // m_localAlgorithmRegistry + m_localLayoutManager are borrowed by the
    // bundle's sources and by m_scriptLoader. Reverse-order member destruction
    // runs, in order: the borrowers declared after them (~m_snappingShadersPage,
    // ~m_tilingAlgorithmPage, ~m_algorithmService, which disconnects its
    // registry watchers); ~m_scriptLoader, which unregisters scripted algorithms
    // while the registry is still alive (a UAF the QObject-child pattern had,
    // where ~QObject ran after the unique_ptr reset); ~m_localSources, dropping
    // borrowed source pointers; then the three owners below.
    // Do not reorder without revisiting every borrower's destructor.
    std::unique_ptr<PhosphorTiles::AlgorithmRegistry> m_localAlgorithmRegistry;
    /// Local read view of the scrolling-template store (same files the
    /// daemon's authoritative store reads); borrowed by the bundle's template
    /// source AND by m_localLayoutManager (setScrollingTemplateStore in the
    /// ctor body), so it is declared BEFORE both of them: reverse-order member
    /// destruction has to tear the two borrowers down while the store they
    /// point at is still alive. Refreshed on the daemon's
    /// scrollingTemplatesChanged D-Bus signal.
    std::unique_ptr<PhosphorZones::ScrollingTemplateStore> m_localTemplateStore;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_localLayoutManager;
    PhosphorLayout::LayoutSourceBundle m_localSources;
    /// Owned here (not parented to `this`) so destruction runs via the
    /// unique_ptr reset in reverse declaration order — BEFORE the
    /// m_localAlgorithmRegistry it borrows. A QObject-child parent would
    /// destroy the loader in ~QObject, which runs AFTER the registry
    /// unique_ptr, leaving the loader's destructor to call
    /// unregisterAlgorithm on a freed registry.
    std::unique_ptr<PhosphorTiles::ScriptedAlgorithmLoader> m_scriptLoader;

    /// Algorithm registry / loader surface — owns the scripted-algorithm
    /// lifecycle helpers (availableAlgorithms, import/export/duplicate/
    /// delete, createNewAlgorithm, etc.). Borrows the registry + loader
    /// above via raw pointers, so this unique_ptr MUST be declared AFTER
    /// them; reverse-order destruction tears the service down (which
    /// disconnects its watchers on the registry) BEFORE m_scriptLoader
    /// and m_localAlgorithmRegistry reset.
    std::unique_ptr<AlgorithmService> m_algorithmService;

    /// Tiling→Algorithm page sub-controller. Held by unique_ptr and placed
    /// AFTER m_localAlgorithmRegistry so reverse-order member destruction runs
    /// ~TilingAlgorithmController (which holds a raw pointer to the registry)
    /// BEFORE the registry unique_ptr resets. The unique_ptr — NOT ~QObject —
    /// is what destroys it, so that ordering holds regardless of parent.
    /// It is nonetheless constructed with parent `this` (see the ctor site):
    /// registerPage adopts parent-LESS pages to m_app, which is destroyed
    /// first and would double-free this object on close.
    std::unique_ptr<TilingAlgorithmController> m_tilingAlgorithmPage;

    /// Snapping→Shaders page sub-controller. Same rationale as
    /// `m_tilingAlgorithmPage`: borrows `m_localLayoutManager` (the registry
    /// walked by `shaderEffectUsages` for the "Used in:" reverse-lookup), so it
    /// MUST be a `unique_ptr<>` declared AFTER that registry — the unique_ptr
    /// reset (member order), not ~QObject, drives its destruction before the
    /// borrowed registry resets. Constructed with parent `this` so registerPage
    /// does not adopt it to the first-destroyed m_app (double-free on close).
    /// Borrows `m_overlayShaderRegistry` too, but that registry is a QObject
    /// child of `this` and survives until ~QObject — fine.
    std::unique_ptr<SnappingShadersPageController> m_snappingShadersPage;

    /// Recompute zone geometry for every manual layout in
    /// @c m_localLayoutManager against the primary screen so
    /// @c ZonesLayoutSource::previewFromLayout gets a populated
    /// @c lastRecalcGeometry() — without this, fixed-geometry layouts
    /// report @c referenceAspectRatio == 0 and zones render as zero-size
    /// rects.
    void recalcLocalLayouts();

    // Virtual desktop / activity state
    int m_virtualDesktopCount = 1;
    /// Whether m_virtualDesktopCount came from the daemon, as opposed to the
    /// display fallback of 1 a failed read leaves behind. Any caller that
    /// REFUSES or DESTROYS on the strength of the count must gate on this.
    bool m_virtualDesktopCountFromDaemon = false;
    QStringList m_virtualDesktopNames;
    bool m_activitiesAvailable = false;
    QVariantList m_activities;
    QString m_currentActivity;

    // Last-received running-windows list (async window picker).
    // Populated by onRunningWindowsAvailable. QML reads this via
    // cachedRunningWindows() for the initial paint while a fresh
    // request is in flight.
    QVariantList m_cachedRunningWindows;

    // Client-side timeout for the async window picker. Started on
    // requestRunningWindows(), stopped when the daemon's
    // runningWindowsAvailable signal arrives. On expiry, we emit
    // runningWindowsTimedOut() so the UI can give the user feedback
    // instead of hanging indefinitely on an unloaded KWin effect.
    QTimer m_runningWindowsTimeout;
    static constexpr int RunningWindowsTimeoutMs = 3000;

    // All staged (not-yet-saved) state owned by StagingService — assignments,
    // virtual screen configs, quick layout slots. Flushed by save() in a
    // specific order (persistence → Settings::save → D-Bus). Ordering
    // (m_stagedSnappingOrder / m_stagedTilingOrder below) stays here because
    // it couples to per-page NOTIFY signals, and the service isn't a QObject
    // so it can't emit them itself.
    StagingService m_staging;

    // Staged ordering changes (flushed to m_settings on save)
    std::optional<QStringList> m_stagedSnappingOrder;
    std::optional<QStringList> m_stagedTilingOrder;

    // PhosphorControl integration — owns the PageRegistry the framework's
    // SettingsAppWindow chrome consumes. Constructed in buildApplicationController()
    // after every page controller exists (so adapter registrations carry stable
    // pointers).
    //
    // Declared as a unique_ptr (rather than QObject child of `this`) AFTER the
    // page sub-controllers above so reverse-order member destruction tears down
    // m_app FIRST, while every page it tracks is still alive. Nothing here
    // dangles either way — the PageRegistry holds QPointer<PageController> and
    // ApplicationController null-compacts its domain list, and ~SettingsController
    // already destroys m_profilesPage with m_app up. The ordering is for
    // determinism: m_app unregisters its tracked domains against live objects
    // instead of leaving the teardown order to self-nulling handles.
    std::unique_ptr<PhosphorControl::ApplicationController> m_app;

    void buildApplicationController();

    /// Wire daemon D-Bus broadcast subscriptions. Failed connects are
    /// appended to @p failedSubscriptions for one batched ctor warning.
    /// Defined in settingscontroller_dbuswire.cpp.
    void wireDaemonSubscriptions(QStringList& failedSubscriptions);

    // File-scope sort helper exposed as a private static member so both
    // settingscontroller.cpp (the ctor-wired LayoutRegistry::layoutsChanged
    // lambda) and settingscontroller_layouts.cpp (D-Bus refresh path) link to the
    // same external-linkage symbol regardless of unity-build batching.
    // The only sort key is isAutotile: tiling algorithms sort last, everything
    // else first, then alphabetical by displayName (case-insensitive). Manual
    // layouts and scrolling templates therefore interleave in that first
    // block — they are distinguished by their row flags, not by position.
    static void sortMergedLayoutList(QVariantList& list);
};

} // namespace PlasmaZones
