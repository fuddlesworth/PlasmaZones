// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Central controller for the standalone settings application.
// Manages page navigation, layout CRUD (via D-Bus), screen info, and owns
// the shared Settings instance. Per-page Q_PROPERTY surfaces are split out
// into page-scoped sub-controllers (EditorPageController, …) hung off this
// class via child Q_PROPERTYs so QML reads `settingsController.<page>.<prop>`.

#pragma once

#include "../config/configdefaults.h"
#include "../phosphor_i18n.h"
#include "../config/settings.h"
#include "../config/updatechecker.h"
#include "../common/daemoncontroller.h"
#include "screenhelper.h"
#include "../core/constants.h"
#include "../core/enums.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorLayoutApi/LayoutSourceBundle.h>

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorWindowRules {
// Forward-declared for the `std::unique_ptr<WindowRuleStore>` member
// below. The complete type is needed only in settingscontroller.cpp
// (where m_localRuleStore is constructed); pulling
// <PhosphorWindowRules/WindowRuleStore.h> into the header would force
// every consumer of this controller to re-parse the WindowRuleStore
// dependency graph.
class WindowRuleStore;
class WindowRuleStoreWatcher;
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

#include "algorithmservice.h"
#include "animationspagecontroller.h"
#include "editorpagecontroller.h"
#include "externaleditscope.h"
#include "generalpagecontroller.h"
#include "snappingzonescontroller.h"
#include "snappingbehaviorcontroller.h"
#include "snappingeffectscontroller.h"
#include "snappingshaderspagecontroller.h"
#include "snappingzoneselectorcontroller.h"
#include "stagingservice.h"
#include "tilingalgorithmcontroller.h"
#include "snappingwindowappearancecontroller.h"
#include "tilingappearancecontroller.h"
#include "tilingbehaviorcontroller.h"
#include "windowrulecontroller.h"

namespace PlasmaZones {

class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)
    Q_PROPERTY(bool needsSave READ needsSave NOTIFY dirtyPagesChanged)
    Q_PROPERTY(QStringList dirtyPages READ dirtyPages NOTIFY dirtyPagesChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(Settings* settings READ settings CONSTANT)
    Q_PROPERTY(DaemonController* daemonController READ daemonController CONSTANT)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(QString dismissedUpdateVersion READ dismissedUpdateVersion WRITE setDismissedUpdateVersion NOTIFY
                   dismissedUpdateVersionChanged)

    // What's New
    Q_PROPERTY(QString lastSeenWhatsNewVersion READ lastSeenWhatsNewVersion NOTIFY lastSeenWhatsNewVersionChanged)
    Q_PROPERTY(bool hasUnseenWhatsNew READ hasUnseenWhatsNew NOTIFY lastSeenWhatsNewVersionChanged)
    Q_PROPERTY(QVariantList whatsNewEntries READ whatsNewEntries CONSTANT)

    // PhosphorZones::Layout management
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)

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

    // Snapping/Tiling behavior pages — trigger surfaces moved to per-page controllers.
    Q_PROPERTY(SnappingBehaviorController* snappingBehaviorPage READ snappingBehaviorPage CONSTANT)
    Q_PROPERTY(TilingBehaviorController* tilingBehaviorPage READ tilingBehaviorPage CONSTANT)
    Q_PROPERTY(SnappingZoneSelectorController* snappingZoneSelectorPage READ snappingZoneSelectorPage CONSTANT)
    Q_PROPERTY(SnappingZonesController* snappingZonesPage READ snappingZonesPage CONSTANT)
    Q_PROPERTY(SnappingEffectsController* snappingEffectsPage READ snappingEffectsPage CONSTANT)
    Q_PROPERTY(SnappingShadersPageController* snappingShadersPage READ snappingShadersPage CONSTANT)
    Q_PROPERTY(
        SnappingWindowAppearanceController* snappingWindowAppearancePage READ snappingWindowAppearancePage CONSTANT)
    Q_PROPERTY(TilingAppearanceController* tilingAppearancePage READ tilingAppearancePage CONSTANT)
    Q_PROPERTY(TilingAlgorithmController* tilingAlgorithmPage READ tilingAlgorithmPage CONSTANT)
    Q_PROPERTY(GeneralPageController* generalPage READ generalPage CONSTANT)
    Q_PROPERTY(AnimationsPageController* animationsPage READ animationsPage CONSTANT)
    // Window Rules page — the unified rule surface. The controller owns one
    // WindowRuleModel and talks to the daemon's org.plasmazones.WindowRules
    // adaptor; QML reads `settingsController.windowRulesPage.model`.
    Q_PROPERTY(WindowRuleController* windowRulesPage READ windowRulesPage CONSTANT)

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
    static const QHash<QString, QString>& parentPageRedirects();
    /// Parent name → set of leaf child page names. Covers top-level sidebar
    /// parents (snapping / tiling / animations) AND mid-level virtual parents
    /// (animations-surfaces / animations-library) whose children don't share
    /// their name prefix. Drives dirty-state propagation in `isPageDirty`.
    static const QHash<QString, QSet<QString>>& pageGroupChildren();

    bool needsSave() const
    {
        return !m_dirtyPages.isEmpty();
    }
    QStringList dirtyPages() const;
    /// Returns true if the page (or any of its children, for parent categories
    /// like "snapping" / "tiling") currently has unsaved changes.
    Q_INVOKABLE bool isPageDirty(const QString& page) const;

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

    Settings* settings()
    {
        return &m_settings;
    }
    DaemonController* daemonController()
    {
        return &m_daemonController;
    }
    UpdateChecker* updateChecker()
    {
        return &m_updateChecker;
    }
    QString dismissedUpdateVersion() const
    {
        return m_dismissedUpdateVersion;
    }
    void setDismissedUpdateVersion(const QString& version);
    Q_INVOKABLE void dismissUpdate();

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

    // ─── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───
    // Loads the on-disk layouts via an in-process LayoutRegistry +
    // ZonesLayoutSource so QML preview paths render even when the daemon
    // is down (early launch, crash). Returns the QML projection produced
    // by PlasmaZones::toVariantMap — intentionally different from the
    // D-Bus getLayoutPreviewList wire shape.
    //
    // @note Autotile preview-parameter drift: the local AlgorithmRegistry
    // is independent of the daemon's (see m_localAlgorithmRegistry below),
    // so daemon-side tuning (master count, split ratio, per-algorithm
    // settings) does NOT propagate here — fallback previews render with
    // built-in defaults. When the daemon is up, D-Bus carries the tuned
    // previews; the fallback is only a "daemon is down" safety net.
    Q_INVOKABLE QVariantList localLayoutPreviews() const;
    // Non-const: ILayoutSource::previewAt is non-const so implementations
    // can populate a query cache (scripted autotile algorithms would be
    // prohibitively expensive to re-run on every picker redraw). Const
    // would silently dodge that cache.
    Q_INVOKABLE QVariantMap localLayoutPreview(const QString& id, int windowCount = 4);

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
    Q_INVOKABLE QString createNewAlgorithm(const QString& name, const QString& baseTemplate, bool supportsMasterCount,
                                           bool supportsSplitRatio, bool producesOverlappingZones, bool supportsMemory);
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);
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
    SnappingBehaviorController* snappingBehaviorPage() const
    {
        return m_snappingBehaviorPage;
    }
    TilingBehaviorController* tilingBehaviorPage() const
    {
        return m_tilingBehaviorPage;
    }
    SnappingZoneSelectorController* snappingZoneSelectorPage() const
    {
        return m_snappingZoneSelectorPage;
    }
    SnappingZonesController* snappingZonesPage() const;
    SnappingEffectsController* snappingEffectsPage() const;
    SnappingShadersPageController* snappingShadersPage() const;
    SnappingWindowAppearanceController* snappingWindowAppearancePage() const;
    TilingAppearanceController* tilingAppearancePage() const;
    TilingAlgorithmController* tilingAlgorithmPage() const;
    GeneralPageController* generalPage() const
    {
        return m_generalPage;
    }
    AnimationsPageController* animationsPage() const
    {
        return m_animationsPage;
    }
    WindowRuleController* windowRulesPage() const
    {
        return m_windowRulesPage;
    }

    PhosphorControl::ApplicationController* app() const
    {
        return m_app.get();
    }

    // ── Running window picker (async flow) ──────────────────────────────────
    //
    // The QML picker dialog calls requestRunningWindows() and binds to
    // runningWindowsAvailable(list) — no blocking D-Bus round-trip. The
    // controller caches the most recent list in m_cachedRunningWindows so
    // QML dialogs can read it directly between calls. The old synchronous
    // getRunningWindows() was removed in Phase 6 of refactor/dbus-performance.
    //
    // requestRunningWindows() invalidates the cache before issuing the
    // call, so QML readers binding to cachedRunningWindows() during a
    // refresh see an empty list (intentional — distinguishes "loading"
    // from "stale-but-cached").
    //
    // A client-side timeout guards against the KWin effect being unloaded:
    // if no reply arrives within RunningWindowsTimeoutMs, we emit
    // runningWindowsTimedOut() so the QML dialog can show a "no response"
    // state instead of hanging on a spinner forever.
    Q_INVOKABLE void requestRunningWindows();
    Q_INVOKABLE QVariantList cachedRunningWindows() const
    {
        return m_cachedRunningWindows;
    }
    Q_INVOKABLE bool runningWindowsPending() const
    {
        return m_runningWindowsTimeout.isActive();
    }

    // ── Config export/import ────────────────────────────────────────────────
    Q_INVOKABLE bool exportAllSettings(const QString& filePath);
    Q_INVOKABLE bool importAllSettings(const QString& filePath);

    // ── Screen state query ─────────────────────────────────────────────────
    Q_INVOKABLE QVariantList getScreenStates() const;
    Q_INVOKABLE QVariantMap getStagedAssignment(const QString& screenName, int virtualDesktop = 0,
                                                const QString& activityId = QString()) const;

    // ── Atomic mode+layout staging (overview page) ──────────────────────────
    Q_INVOKABLE void stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                          int mode, const QString& snappingLayoutId, const QString& tilingAlgorithmId);

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
                                                      int masterCount) const;
    Q_INVOKABLE QVariantList generateAlgorithmDefaultPreview(const QString& algorithmId) const;
    Q_INVOKABLE void openAlgorithmsFolder();
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
    // Per-card sub-domains (Gaps vs Algorithm) of the shared autotile map, so
    // each card's scope chip dot/reset only touches its own keys.
    Q_INVOKABLE bool hasPerScreenAutotileGapsSettings(const QString& screenName) const;
    Q_INVOKABLE void clearPerScreenAutotileGapsSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenAutotileAlgorithmSettings(const QString& screenName) const;
    Q_INVOKABLE void clearPerScreenAutotileAlgorithmSettings(const QString& screenName);

    // ── Per-screen snapping overrides ────────────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenSnappingSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenSnappingSettings(const QString& screenName) const;

    // ── Virtual screen configuration ──────────────────────────────────────────
    Q_INVOKABLE QStringList getPhysicalScreens() const;
    Q_INVOKABLE QVariantList getVirtualScreenConfig(const QString& physicalScreenId) const;
    Q_INVOKABLE void applyVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens);
    Q_INVOKABLE void removeVirtualScreenConfig(const QString& physicalScreenId);
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
    void layoutsChanged();
    void layoutAdded(const QString& layoutId);
    void availableAlgorithmsChanged();
    void algorithmCreated(const QString& algorithmId);
    void algorithmOperationFailed(const QString& reason);
    void layoutOperationFailed(const QString& reason);
    /// Emitted when `applyVirtualScreenConfig` / `removeVirtualScreenConfig`
    /// fails at the daemon — QML can surface the reason in a toast so the
    /// user knows the change wasn't saved.
    void virtualScreenConfigFailed(const QString& physicalScreenId, const QString& reason);
    void screensChanged();
    void scopeScreenNameChanged();
    /// Emitted whenever any per-screen override map changes (set or clear,
    /// any domain). The monitor scope map re-polls hasPerScreen*Settings()
    /// to refresh its per-output override dots, which a plain WRITE on an
    /// individual key can't drive on its own.
    void perScreenOverridesChanged();
    void dismissedUpdateVersionChanged();
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
     * is a QVariantList of {windowClass, appName, caption} maps ready for
     * QML consumption. Also updates cachedRunningWindows() so later
     * queries can read the last-seen value synchronously.
     */
    void runningWindowsAvailable(const QVariantList& windows);

    /**
     * @brief No reply arrived within RunningWindowsTimeoutMs.
     *
     * Emitted by the client-side timeout timer when the KWin effect never
     * answers a requestRunningWindows() call (effect unloaded, crashed,
     * or slow). QML dialogs should surface an error state so the user
     * can distinguish "no windows" from "daemon or effect not responding".
     * Note: cachedRunningWindows() is empty by the time this fires —
     * requestRunningWindows() invalidated the cache at the start of the
     * request, so the timeout signal means "no data, refresh failed."
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
    // 50 ms m_layoutLoadTimer. Reachable by SLOT() because it's a
    // private slot.
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

    /// Daemon WindowRules.rulesChanged → reload m_localRuleStore so the
    /// in-process LayoutRegistry assignment cascade sees rule edits.
    void reloadLocalRuleStore(bool persisted);

private:
    void setNeedsSave(bool needs);
    void refreshVirtualDesktops();
    void refreshActivities();

    /// Single WindowRule store shared by m_settings (disable lists) and the
    /// LayoutRegistry. Declared FIRST so it outlives all borrowers.
    std::unique_ptr<PhosphorWindowRules::WindowRuleStore> m_localRuleStore;
    /// Opt-in cross-process auto-reload of m_localRuleStore on external writes
    /// (mainly the no-daemon case). Declared after the store; tears down first.
    std::unique_ptr<PhosphorWindowRules::WindowRuleStoreWatcher> m_localRuleStoreWatcher;
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
    SnappingZoneSelectorController* m_snappingZoneSelectorPage = nullptr;
    SnappingZonesController* m_snappingZonesPage = nullptr;
    SnappingEffectsController* m_snappingEffectsPage = nullptr;
    SnappingWindowAppearanceController* m_snappingWindowAppearancePage = nullptr;
    TilingAppearanceController* m_tilingAppearancePage = nullptr;
    GeneralPageController* m_generalPage = nullptr;
    /// Parented to `this` so Qt manages lifetime; the raw pointer is fine
    /// because every consumer is also a child of this controller and Qt's
    /// child cleanup walks in reverse-insertion order. Constructed before
    /// m_animationsPage so the page controller's non-owned pointer
    /// outlives the page through child-destruction order.
    PhosphorAnimationShaders::AnimationShaderRegistry* m_animationShaderRegistry = nullptr;
    AnimationsPageController* m_animationsPage = nullptr;
    /// Window Rules page sub-controller. Parented to `this`; owns its
    /// WindowRuleModel internally. Constructed after m_animationsPage so its
    /// dirty-tracking connection is wired in the same ctor block.
    WindowRuleController* m_windowRulesPage = nullptr;
    /// Settings-side mirror of the daemon's overlay-shader registry —
    /// drives the read-only Snapping → Shaders browser. Same parent /
    /// declaration-order rationale as `m_animationShaderRegistry` above.
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
    UpdateChecker m_updateChecker;
    QString m_dismissedUpdateVersion;
    QString m_lastSeenWhatsNewVersion;
    QVariantList m_whatsNewEntries;
    ScreenHelper m_screenHelper;
    QString m_activePage = QStringLiteral("overview");
    QSet<QString> m_dirtyPages;
    /// Stack of external-edit page ids — `setNeedsSave(true)` targets
    /// `top()` instead of `m_activePage`. Nesting-aware so an inner
    /// begin/end pair restores the outer target on pop rather than
    /// clearing it. See ExternalEditScope for the RAII wrapper.
    QStack<QString> m_externalEditStack;
    bool m_saving = false;
    bool m_loading = false;
    /// Reentrancy guard for setActivePage(). A slot connected to
    /// activePageChanged that calls back into setActivePage would
    /// otherwise corrupt the m_loading toggle window.
    bool m_settingActivePage = false;

    // PhosphorZones::Layout state
    QVariantList m_layouts;
    QTimer m_layoutLoadTimer;
    QString m_pendingSelectLayoutId;

    // Suppresses the local-path layoutsChanged emit while a D-Bus
    // getLayoutList round-trip is in flight. Without this gate, every
    // loadLayoutsAsync() emits twice: once synchronously from the
    // PhosphorZones::LayoutRegistry::layoutsChanged lambda (local composite) and once
    // from the async D-Bus reply lambda (daemon-enriched list). Set true
    // right before the D-Bus asyncCall dispatch; cleared in the reply
    // lambda's entry (before any early-return on error, so the local
    // fallback emit resumes if the daemon is unreachable). Only relevant
    // when the daemon is available — when the D-Bus call errors out, the
    // local path's emit remains the authoritative refresh.
    bool m_awaitingDaemonLayouts = false;

    // Daemon-independent layout source — see localLayoutPreviews() doc.
    // PhosphorZones::LayoutRegistry opens its own assignments backend + scans the standard
    // layouts directory; the bundle's composite aggregates manual + autotile
    // entries so consumers query a single ILayoutSource and never branch on
    // id-prefix.
    //
    // ─── DECLARATION ORDER INVARIANT ─────────────────────────────────
    // m_localAlgorithmRegistry + m_localLayoutManager are borrowed by the
    // bundle's sources AND by m_scriptLoader below. Reverse-order member
    // destruction must tear down the loader and the bundle BEFORE the
    // registries those consumers borrow. With the order below:
    //   1. ~m_scriptLoader first (unregisters scripted algorithms while
    //      the registry is still alive — fixes a UAF the QObject-child-
    //      parent pattern had, where ~QObject ran after unique_ptr reset).
    //   2. ~m_localSources drops borrowed source pointers.
    //   3. ~m_localLayoutManager, ~m_localAlgorithmRegistry.
    // Do not reorder without revisiting every borrower's destructor.
    std::unique_ptr<PhosphorTiles::AlgorithmRegistry> m_localAlgorithmRegistry;
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

    /// Tiling→Algorithm page sub-controller. Declared as unique_ptr (not
    /// parented to `this`) and placed AFTER m_localAlgorithmRegistry so
    /// reverse-order destruction runs ~TilingAlgorithmController BEFORE
    /// the registry unique_ptr resets — the controller holds a raw pointer
    /// to the registry. Parenting to `this` would defer destruction to
    /// ~QObject, which runs AFTER these member unique_ptrs have already
    /// released their borrowed targets.
    std::unique_ptr<TilingAlgorithmController> m_tilingAlgorithmPage;

    /// Snapping→Shaders page sub-controller. Same declaration-order
    /// rationale as `m_tilingAlgorithmPage`: borrows
    /// `m_localLayoutManager` (the registry walked by `shaderEffectUsages`
    /// for the "Used in:" reverse-lookup), so it MUST be a `unique_ptr<>`
    /// declared AFTER that registry. A QObject-child raw pointer would
    /// defer destruction to ~QObject, which runs AFTER the registry's
    /// unique_ptr has already reset, leaving the controller holding a
    /// dangling layout-registry pointer if any teardown signal fires.
    /// Borrows `m_overlayShaderRegistry` too, but that registry is a
    /// QObject child of `this` and survives until ~QObject — fine.
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
    // m_app FIRST: its PageRegistry holds raw PageController* refs to the
    // m_tilingAlgorithmPage / m_snappingShadersPage unique_ptrs (and the
    // PageAdapter QObject children of `this`). A QObject-child raw pointer
    // would defer m_app's destruction to ~QObject, which runs AFTER the page
    // unique_ptrs reset — leaving the registry briefly holding dangling refs
    // any queued event-loop tick could trip on.
    std::unique_ptr<PhosphorControl::ApplicationController> m_app;

    void buildApplicationController();

    /// Wire daemon D-Bus broadcast subscriptions. Failed connects are
    /// appended to @p failedSubscriptions for one batched ctor warning.
    /// Defined in settingscontroller_dbuswire.cpp (800-line cap).
    void wireDaemonSubscriptions(QStringList& failedSubscriptions);

    // File-scope sort helper exposed as a private static member so both
    // settingscontroller.cpp (file-watcher rebind path) and
    // settingscontroller_layouts.cpp (D-Bus refresh path) link to the
    // same external-linkage symbol regardless of unity-build batching.
    // Manual layouts sort first; within each category alphabetical by
    // displayName (case-insensitive).
    static void sortMergedLayoutList(QVariantList& list);
    // Defence-in-depth path sanitiser shared by importLayout/exportLayout
    // (layouts TU) and importAllSettings/exportAllSettings (session TU).
    // See the implementation comment for the rejection rules.
    static QString sanitizeIOPath(const QString& raw);
};

} // namespace PlasmaZones
