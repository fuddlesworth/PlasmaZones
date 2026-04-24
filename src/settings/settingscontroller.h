// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Central controller for the standalone settings application.
// Manages page navigation, layout CRUD (via D-Bus), screen info, and owns
// the shared Settings instance. Per-page Q_PROPERTY surfaces are split out
// into page-scoped sub-controllers (EditorPageController, …) hung off this
// class via child Q_PROPERTYs so QML reads `settingsController.<page>.<prop>`.

#pragma once

#include "../config/configdefaults.h"
#include "../pz_i18n.h"
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

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QDBusConnection>
#include <QVariantList>
#include <QTimer>
#include <memory>
#include <optional>

#include "algorithmservice.h"
#include "editorpagecontroller.h"
#include "generalpagecontroller.h"
#include "snappingappearancecontroller.h"
#include "snappingbehaviorcontroller.h"
#include "snappingeffectscontroller.h"
#include "snappingzoneselectorcontroller.h"
#include "stagingservice.h"
#include "tilingalgorithmcontroller.h"
#include "tilingappearancecontroller.h"
#include "tilingbehaviorcontroller.h"

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

    // Editor page — properties live on EditorPageController, exposed here as a
    // child QObject so QML reads `settingsController.editorPage.duplicateShortcut`.
    Q_PROPERTY(EditorPageController* editorPage READ editorPage CONSTANT)

    // Snapping/Tiling behavior pages — trigger surfaces moved to per-page controllers.
    Q_PROPERTY(SnappingBehaviorController* snappingBehaviorPage READ snappingBehaviorPage CONSTANT)
    Q_PROPERTY(TilingBehaviorController* tilingBehaviorPage READ tilingBehaviorPage CONSTANT)
    Q_PROPERTY(SnappingZoneSelectorController* snappingZoneSelectorPage READ snappingZoneSelectorPage CONSTANT)
    Q_PROPERTY(SnappingAppearanceController* snappingAppearancePage READ snappingAppearancePage CONSTANT)
    Q_PROPERTY(SnappingEffectsController* snappingEffectsPage READ snappingEffectsPage CONSTANT)
    Q_PROPERTY(TilingAppearanceController* tilingAppearancePage READ tilingAppearancePage CONSTANT)
    Q_PROPERTY(TilingAlgorithmController* tilingAlgorithmPage READ tilingAlgorithmPage CONSTANT)
    Q_PROPERTY(GeneralPageController* generalPage READ generalPage CONSTANT)

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

    static const QSet<QString>& validPageNames();
    static const QHash<QString, QString>& parentPageRedirects();

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

    // PhosphorZones::Layout accessors
    QVariantList layouts() const
    {
        return m_layouts;
    }

    // ─── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───
    //
    // Settings runs in its own process, separate from the daemon. The legacy
    // path fetches the layout list over D-Bus (getLayoutList) which only
    // works while the daemon is running. The methods below load the SAME
    // on-disk layouts via an in-process PhosphorZones::LayoutRegistry + PhosphorZones::ZonesLayoutSource,
    // so QML preview-rendering paths can render layouts even when the
    // daemon isn't up (early settings launch, daemon crashed, etc.).
    //
    // Returns the QML-facing projection produced by
    // PlasmaZones::toVariantMap (src/common/layoutpreviewserialize.h):
    // id / name / zones[]{relativeGeometry{x,y,width,height},zoneNumber} /
    // isAutotile / aspectRatioClass (string tag) / flat supports* capability
    // flags. Intentionally different from the D-Bus getLayoutPreviewList JSON
    // shape, which is optimised for wire transfer.
    //
    // @note Autotile preview-parameter drift: the local AlgorithmRegistry
    // here is independent of the daemon's (see m_localAlgorithmRegistry
    // below) — preview params (master count, split ratio, saved per-
    // algorithm settings) configured via the daemon's D-Bus do NOT
    // propagate to this process's registry. The fallback previews always
    // render with the algorithm's built-in defaults rather than the
    // user's live tuning. This only affects the daemon-independent code
    // path; when the daemon is up the D-Bus @c getLayoutPreviewList
    // carries the fully-tuned previews. Acceptable because the fallback
    // is primarily a "daemon is down" safety net for early launch /
    // crash-recovery, not a replacement for the D-Bus surface.
    Q_INVOKABLE QVariantList localLayoutPreviews() const;
    // Non-const: ILayoutSource::previewAt is non-const so implementations
    // can populate a query cache (scripted autotile algorithms would be
    // prohibitively expensive to re-run on every picker redraw). Changing
    // this invoker to const would silently dodge that cache.
    Q_INVOKABLE QVariantMap localLayoutPreview(const QString& id, int windowCount = 4);

    // Screen accessors
    QVariantList screens() const
    {
        return m_screenHelper.screens();
    }
    Q_INVOKABLE QVariantMap physicalScreenResolution(const QString& screenId) const;

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

    // Screen helpers
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);
    Q_INVOKABLE bool isDesktopDisabled(const QString& screenName, int desktop) const;
    Q_INVOKABLE void setDesktopDisabled(const QString& screenName, int desktop, bool disabled);
    Q_INVOKABLE bool isActivityDisabled(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE void setActivityDisabled(const QString& screenName, const QString& activityId, bool disabled);

    // Font helpers (for FontPickerDialog)
    Q_INVOKABLE QStringList fontStylesForFamily(const QString& family) const;
    Q_INVOKABLE int fontStyleWeight(const QString& family, const QString& style) const;
    Q_INVOKABLE bool fontStyleItalic(const QString& family, const QString& style) const;

    // Assignment helpers (D-Bus to daemon)
    Q_INVOKABLE void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearScreenAssignment(const QString& screenName);
    Q_INVOKABLE void assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenAssignment(const QString& screenName);

    // Assignment query helpers (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreen(const QString& screenName) const;
    Q_INVOKABLE QString getTilingLayoutForScreen(const QString& screenName) const;

    // Per-desktop assignments (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                 const QString& layoutId);
    Q_INVOKABLE void clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // Tiling per-desktop assignments (D-Bus to daemon)
    Q_INVOKABLE void assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                       const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // Per-activity assignments (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                  const QString& layoutId);
    Q_INVOKABLE void clearScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getSnappingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;

    // Tiling per-activity assignments (D-Bus to daemon)
    Q_INVOKABLE void assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                        const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                  const QString& activityId) const;

    // Quick layout slots (D-Bus to daemon)
    Q_INVOKABLE QString getQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    Q_INVOKABLE QString getQuickLayoutShortcut(int slotNumber) const;
    Q_INVOKABLE QString getTilingQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);

    // App-to-zone rules (D-Bus to daemon)
    Q_INVOKABLE QVariantList getAppRulesForLayout(const QString& layoutId) const;
    Q_INVOKABLE void addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                        const QString& targetScreen = QString());
    Q_INVOKABLE void removeAppRuleFromLayout(const QString& layoutId, int index);

    // Assignment lock helpers
    Q_INVOKABLE bool isScreenLocked(const QString& screenName, int mode) const;
    Q_INVOKABLE void toggleScreenLock(const QString& screenName, int mode);
    Q_INVOKABLE bool isContextLocked(const QString& screenName, int virtualDesktop, const QString& activity,
                                     int mode) const;
    Q_INVOKABLE void toggleContextLock(const QString& screenName, int virtualDesktop, const QString& activity,
                                       int mode);

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
    SnappingAppearanceController* snappingAppearancePage() const
    {
        return m_snappingAppearancePage;
    }
    SnappingEffectsController* snappingEffectsPage() const
    {
        return m_snappingEffectsPage;
    }
    TilingAppearanceController* tilingAppearancePage() const
    {
        return m_tilingAppearancePage;
    }
    TilingAlgorithmController* tilingAlgorithmPage() const
    {
        return m_tilingAlgorithmPage.get();
    }
    GeneralPageController* generalPage() const
    {
        return m_generalPage;
    }

    // ── Running window picker (async flow) ──────────────────────────────────
    //
    // The QML picker dialog calls requestRunningWindows() and binds to
    // runningWindowsAvailable(list) — no blocking D-Bus round-trip. The
    // controller caches the most recent list in m_cachedRunningWindows so
    // repeat opens of the dialog can show the last-seen values immediately
    // while the fresh fetch is in flight. The old synchronous
    // getRunningWindows() was removed in Phase 6 of refactor/dbus-performance.
    //
    // A client-side timeout guards against the KWin effect being unloaded:
    // if no reply arrives within RunningWindowsTimeoutMs, we surface the
    // last-cached list via runningWindowsTimedOut() so the QML dialog can
    // show a "no response" state instead of hanging on a spinner forever.
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
    Q_INVOKABLE bool hasStagedAssignment(const QString& screenName, int virtualDesktop = 0,
                                         const QString& activityId = QString()) const;
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
    void daemonRunningChanged();
    void layoutsChanged();
    void layoutAdded(const QString& layoutId);
    void availableAlgorithmsChanged();
    void algorithmCreated(const QString& algorithmId);
    void algorithmOperationFailed(const QString& reason);
    void layoutOperationFailed(const QString& reason);
    void screensChanged();
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
     * Cached list (possibly stale) is still available via
     * cachedRunningWindows().
     */
    void runningWindowsTimedOut();

    // KZones import signals
    void kzonesImportFinished(int count, const QString& message);
    void lockedScreensChanged();
    void disabledDesktopsChanged();
    void disabledActivitiesChanged();

    // Ordering staged signals
    void stagedSnappingOrderChanged();
    void stagedTilingOrderChanged();

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

private:
    void setNeedsSave(bool needs);
    void refreshVirtualDesktops();
    void refreshActivities();

    void saveAppRulesToDaemon(const QString& layoutId, const QVariantList& rules);

    Settings m_settings;
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
    SnappingAppearanceController* m_snappingAppearancePage = nullptr;
    SnappingEffectsController* m_snappingEffectsPage = nullptr;
    TilingAppearanceController* m_tilingAppearancePage = nullptr;
    GeneralPageController* m_generalPage = nullptr;

    DaemonController m_daemonController;
    UpdateChecker m_updateChecker;
    QString m_dismissedUpdateVersion;
    QString m_lastSeenWhatsNewVersion;
    QVariantList m_whatsNewEntries;
    ScreenHelper m_screenHelper;
    QString m_activePage = QStringLiteral("overview");
    QSet<QString> m_dirtyPages;
    QString m_externalEditPage; // Non-empty: setNeedsSave(true) targets this instead of m_activePage
    bool m_saving = false;
    bool m_loading = false;

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
    // it couples to per-page NOTIFY signals the service isn't a QObject.
    StagingService m_staging;

    // Staged ordering changes (flushed to m_settings on save)
    std::optional<QStringList> m_stagedSnappingOrder;
    std::optional<QStringList> m_stagedTilingOrder;
};

} // namespace PlasmaZones
