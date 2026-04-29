// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusVariant>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QHash>
#include <optional>

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorLayout {
class ILayoutSource;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PhosphorZones {
class Layout;
}

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {
class VirtualDesktopManager;
class ActivityManager;
class ISettings;

/**
 * @brief D-Bus adaptor for layout management operations
 *
 * Provides D-Bus interface: org.plasmazones.LayoutRegistry
 *  PhosphorZones::Layout CRUD and assignment operations
 */
class PLASMAZONES_EXPORT LayoutAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.LayoutRegistry")

public:
    explicit LayoutAdaptor(PhosphorZones::LayoutRegistry* manager, QObject* parent = nullptr);
    explicit LayoutAdaptor(PhosphorZones::LayoutRegistry* manager, VirtualDesktopManager* vdm,
                           Phosphor::Screens::ScreenManager* screenManager = nullptr, QObject* parent = nullptr);
    ~LayoutAdaptor() override = default;

    void setVirtualDesktopManager(VirtualDesktopManager* vdm);
    void setActivityManager(ActivityManager* am);
    void setSettings(ISettings* settings);

    /// Inject the daemon-owned tile-algorithm registry — required for
    /// autotile entries in @ref getLayoutList and @ref getLayout.
    void setAlgorithmRegistry(PhosphorTiles::ITileAlgorithmRegistry* registry);

    /// Inject the daemon's bundle-owned autotile layout source. Optional —
    /// when set, @ref getLayoutList reuses its preview cache across calls
    /// instead of constructing a transient source per call. The existing
    /// @ref setLayoutSource handles the composite used by the
    /// @c getLayoutPreview* D-Bus surface separately; this setter threads
    /// the autotile-specific source to the @c buildUnifiedLayoutList path.
    ///
    /// @note Expected to be called at most once after construction. The
    /// adaptor does not subscribe to the source's own signals — match the
    /// "set-once" discipline used by every other setAutotileLayoutSource
    /// call site (UnifiedLayoutController, OverlayService, …).
    void setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source);

    /**
     * @brief Wire in the source-agnostic ILayoutSource bridge.
     *
     * Backs the @c getLayoutPreviewList / @c getLayoutPreview slots. When
     * unset, those slots return empty JSON — clients should call them only
     * after the daemon has finished init(). Accepts the abstract base so
     * the future autotile preview source (or a composite) can plug in
     * without changing the adaptor.
     */
    void setLayoutSource(PhosphorLayout::ILayoutSource* source);

public Q_SLOTS:
    // PhosphorZones::Layout queries
    QString getActiveLayout();
    QStringList getLayoutList();
    QString getLayout(const QString& id);

    /**
     * @brief Source-agnostic preview list (manual layouts today, autotile
     *        algorithms once phosphor-tile-algo lands).
     *
     * Returns a JSON array. Each entry is a serialized
     * PhosphorLayout::LayoutPreview (id, displayName, zones in 0–1
     * relative coords, isAutotile, optional algorithm metadata).
     * Renderers consume this uniformly without branching on whether
     * the entry is manual or autotile.
     *
     * Returns "[]" when no source is wired (early startup).
     */
    QString getLayoutPreviewList();

    /**
     * @brief Source-agnostic preview for one entry.
     *
     * @p id from getLayoutPreviewList. @p windowCount is honoured by
     * autotile sources (algorithm runs at that count); manual sources
     * ignore it. Returns "{}" when @p id is unknown to the wired
     * source or no source is set.
     */
    QString getLayoutPreview(const QString& id, int windowCount);

    // PhosphorZones::Layout management
    void setActiveLayout(const QString& id);
    void applyQuickLayout(int number, const QString& screenId);
    QString createLayout(const QString& name, const QString& type);
    void deleteLayout(const QString& id);
    QString duplicateLayout(const QString& id);

    // Import/Export
    QString importLayout(const QString& filePath);
    void exportLayout(const QString& layoutId, const QString& filePath);

    // Visibility filtering
    void setLayoutHidden(const QString& layoutId, bool hidden);

    // Auto-assign
    void setLayoutAutoAssign(const QString& layoutId, bool enabled);

    // Aspect ratio classification
    void setLayoutAspectRatioClass(const QString& layoutId, int aspectRatioClass);

    // Editor support
    bool updateLayout(const QString& layoutJson);
    QString createLayoutFromJson(const QString& layoutJson);

    // Editor launch
    void openEditor();
    void openEditorForScreen(const QString& screenId);
    void openEditorForLayout(const QString& layoutId);
    void openEditorForLayoutOnScreen(const QString& layoutId, const QString& screenId);

    // Screen assignments
    QString getLayoutForScreen(const QString& screenId);
    void assignLayoutToScreen(const QString& screenId, const QString& layoutId);
    void clearAssignment(const QString& screenId);
    void setAllScreenAssignments(const QVariantMap& assignments); // Batch set - saves once

    // Per-virtual-desktop screen assignments
    QString getLayoutForScreenDesktop(const QString& screenId, int virtualDesktop);
    void assignLayoutToScreenDesktop(const QString& screenId, int virtualDesktop, const QString& layoutId);
    void clearAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop);
    bool hasExplicitAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop);
    int getModeForScreenDesktop(const QString& screenId, int virtualDesktop);
    QString getSnappingLayoutForScreenDesktop(const QString& screenId, int virtualDesktop);
    QString getTilingAlgorithmForScreenDesktop(const QString& screenId, int virtualDesktop);
    void setAllDesktopAssignments(const QVariantMap& assignments); // Batch set - key: "screen:desktop", value: layoutId

    // Individual full-entry assignment (KCM sends complete PhosphorZones::AssignmentEntry per context)
    void setAssignmentEntry(const QString& screenId, int virtualDesktop, const QString& activity, int mode,
                            const QString& snappingLayout, const QString& tilingAlgorithm);

    // Suppress screenLayoutChanged D-Bus signals during KCM save batch.
    void setSaveBatchMode(bool enabled);

    // Trigger resnap/retile + OSD after KCM assignment changes.
    // Called ONCE after all setAssignmentEntry/clear calls complete.
    void applyAssignmentChanges();

    // Virtual desktop information
    int getVirtualDesktopCount();
    QStringList getVirtualDesktopNames();

    /// Stable screen-id enumeration for UI / settings consumers.
    /// Mirrors @c ScreenManager::effectiveScreenIds() — one entry per
    /// physical monitor plus any virtual-screen subdivisions.
    /// Separated from @ref getAllScreenAssignments so the JSON readback
    /// can stay narrowly scoped to *stored* assignment state without
    /// also having to enumerate unconfigured screens.
    QStringList getAvailableScreenIds();

    QString getAllScreenAssignments();
    QVariantMap getAllDesktopAssignments(); // Get all per-desktop assignments as key -> layoutId
    QVariantMap getAllActivityAssignments(); // Get all per-activity assignments as key -> layoutId

    // Quick layout slots (1-9)
    QString getQuickLayoutSlot(int slotNumber);
    void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    void setAllQuickLayoutSlots(const QVariantMap& slots); // Batch set - saves once
    QVariantMap getAllQuickLayoutSlots();

    // ═══════════════════════════════════════════════════════════════════════════════
    // KDE Activities Support
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current virtual desktop number (1-indexed)
     * @return Current desktop number, or 0 if unavailable
     */
    int getCurrentVirtualDesktop();

    /**
     * @brief Check if KDE Activities support is available
     * @return true if KActivities is running
     */
    bool isActivitiesAvailable();

    /**
     * @brief Get list of all activity IDs
     * @return List of activity UUIDs
     */
    QStringList getActivities();

    /**
     * @brief Get the current activity ID
     * @return Current activity UUID, or empty if unavailable
     */
    QString getCurrentActivity();

    /**
     * @brief Get activity info as JSON
     * @param activityId Activity UUID
     * @return JSON with id, name, icon fields
     */
    QString getActivityInfo(const QString& activityId);

    /**
     * @brief Get all activities as JSON array
     * @return JSON array with activity info objects
     */
    QString getAllActivitiesInfo();

    // Per-activity layout assignments (screen + activity, any desktop)
    QString getLayoutForScreenActivity(const QString& screenId, const QString& activityId);
    void assignLayoutToScreenActivity(const QString& screenId, const QString& activityId, const QString& layoutId);
    void clearAssignmentForScreenActivity(const QString& screenId, const QString& activityId);
    bool hasExplicitAssignmentForScreenActivity(const QString& screenId, const QString& activityId);
    void
    setAllActivityAssignments(const QVariantMap& assignments); // Batch set - key: "screen:activity", value: layoutId

    // Full assignment (screen + desktop + activity)
    QString getLayoutForScreenDesktopActivity(const QString& screenId, int virtualDesktop, const QString& activityId);
    void assignLayoutToScreenDesktopActivity(const QString& screenId, int virtualDesktop, const QString& activityId,
                                             const QString& layoutId);
    void clearAssignmentForScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                 const QString& activityId);

    /**
     * @brief Get current mode, layout, and algorithm for all screens
     *
     * Returns a JSON array with one object per screen:
     *   screenName, mode (0=Snapping, 1=Autotile), layoutId, layoutName,
     *   algorithmId, algorithmName.
     *
     * @return JSON string
     */
    QString getScreenStates();

    // Screen layout lock
    void toggleScreenLock(const QString& screenId);
    bool isScreenLocked(const QString& screenId);
    void toggleContextLock(const QString& screenId, int virtualDesktop, const QString& activity);
    bool isContextLocked(const QString& screenId, int virtualDesktop, const QString& activity);

Q_SIGNALS:
    /**
     * @brief Emitted when the daemon has fully initialized and is ready
     * @note KCM should wait for this signal before querying layouts
     */
    void daemonReady();

    /**
     * @brief Full-layout change notification.
     *
     * Emitted by structural mutations — editor save, createLayoutFromJson,
     * updateLayout — where the client genuinely needs the whole serialized
     * layout. Carries the complete JSON payload (5–20 KB in prod).
     *
     * Compact property-level mutations (setLayoutHidden / setLayoutAutoAssign /
     * setLayoutAspectRatioClass) do NOT emit this signal any more — they
     * emit layoutPropertyChanged instead to avoid the full re-serialization.
     */
    void layoutChanged(const QString& layoutJson);

    /**
     * @brief Cheap active-layout-switch notification.
     *
     * Emitted alongside @c layoutChanged(json) on every active-layout switch,
     * but carries only the layout UUID instead of the full JSON payload.
     * Subscribers that re-load from disk (e.g. the editor's
     * reloadLocalLayouts) can bind to this signal and avoid paying the
     * 5–20 KB marshalling cost of @c layoutChanged every time the user
     * flips contexts. Additive: @c layoutChanged(json) remains for
     * compatibility with existing consumers.
     */
    void activeLayoutChanged(const QString& layoutId);

    /**
     * @brief Compact property-level change notification.
     *
     * Emitted when a single scalar property on a layout is mutated without
     * touching zone structure. Subscribers that only need to refresh a flag
     * (hidden, auto-assign, aspect ratio class) can react to this signal
     * directly instead of re-parsing the full layout JSON. Clients that
     * still want the full shape can pull it via getLayout(layoutId) — the
     * adaptor's cache is invalidated before this signal fires, so that
     * read reflects the mutation.
     *
     * @param layoutId The layout whose property changed
     * @param property Property name: "hidden", "autoAssign", or "aspectRatioClass"
     * @param value    New value as a QDBusVariant (bool for hidden/autoAssign,
     *                 int for aspectRatioClass)
     */
    void layoutPropertyChanged(const QString& layoutId, const QString& property, const QDBusVariant& value);

    void layoutListChanged();

    /**
     * @brief Emitted when a new layout is created.
     *
     * Fires alongside @c layoutListChanged from createLayout,
     * createLayoutFromJson, duplicateLayout, and importLayout so subscribers
     * that only care about additions (e.g. the settings list-view auto-select
     * path) can react without parsing the full list diff.
     */
    void layoutCreated(const QString& layoutId);

    /**
     * @brief Emitted when a layout is deleted.
     *
     * Fires alongside @c layoutListChanged from deleteLayout so subscribers
     * can evict stale per-layout state keyed by the UUID before the list
     * refresh round-trip completes.
     */
    void layoutDeleted(const QString& layoutId);

    void screenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop);
    void virtualDesktopCountChanged(int count);

    /**
     * @brief Emitted when the internal active layout changes
     * @param layoutId The ID of the newly active layout (transient, internal)
     * @note Used by resnap, geometry recalc, and overlay machinery.
     *       getActiveLayout() returns the settings-based default layout,
     *       which may differ from the ID in this signal.
     */
    void activeLayoutIdChanged(const QString& layoutId);

    /**
     * @brief Emitted when quick layout slots are modified
     * @note This allows the settings panel to refresh its quick layout slot assignments
     */
    void quickLayoutSlotsChanged();

    /**
     * @brief Emitted when the current KDE Activity changes
     * @param activityId The new activity ID
     */
    void currentActivityChanged(const QString& activityId);

    /**
     * @brief Emitted when the list of KDE Activities changes (added/removed)
     */
    void activitiesChanged();

    /**
     * @brief Emitted when the KCM requests resnap/retile after assignment changes
     * @param changedScreenIds Screen IDs whose assignments were modified in this batch
     *
     * Typed as QStringList (not QSet) because this is a Q_SIGNAL on a
     * QDBusAbstractAdaptor subclass and is therefore auto-exposed over D-Bus;
     * QSet is not a D-Bus-marshallable type. Internal callers that need
     * set semantics should convert via `QSet<QString>{list.begin(), list.end()}`.
     */
    void assignmentChangesApplied(const QStringList& changedScreenIds);

private Q_SLOTS:
    // String-based connection slots for PhosphorZones::LayoutRegistry signals
    // (PhosphorZones::LayoutRegistry redeclares signals for Q_PROPERTY, so we use string-based connections)
    void onActiveLayoutChanged(PhosphorZones::Layout* layout);
    void onLayoutsChanged();
    void onLayoutAssigned(const QString& screen, int virtualDesktop, PhosphorZones::Layout* layout);

public:
    void invalidateCache();

    /**
     * @brief Notify consumers that the layout list data has changed
     *
     * Called by the Daemon when tiling parameters (e.g. maxWindows) change
     * on Apply, so layout previews are regenerated.
     */
    void notifyLayoutListChanged();

private:
    void connectLayoutManagerSignals();
    void connectVirtualDesktopSignals();
    void connectActivitySignals();

    /**
     * @brief One-time setup for the ILayoutSource coalesce timer.
     *
     * Configures @c m_layoutSourceCoalesce (single-shot, 200 ms interval)
     * and wires its @c timeout into a single @c layoutListChanged emission.
     * Called from every public constructor so the init lives in exactly
     * one place — see constructor bodies in layoutadaptor.cpp.
     */
    void initCoalesceTimer();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Parse UUID string with validation and logging
     * @param id UUID string to parse
     * @param operation Description for error logging (e.g., "setActiveLayout")
     * @return Valid QUuid or std::nullopt on failure (logs warning)
     */
    std::optional<QUuid> parseAndValidateUuid(const QString& id, const QString& operation) const;

    /**
     * @brief Get layout by ID string with full validation
     * @param id UUID string
     * @param operation Description for error logging
     * @return PhosphorZones::Layout pointer or nullptr on failure (logs warning)
     *
     * Consolidates parseUuid + layoutById + error logging.
     */
    PhosphorZones::Layout* getValidatedLayout(const QString& id, const QString& operation);

    /**
     * @brief Validate that a required string parameter is not empty
     * @param value The value to check
     * @param paramName Parameter name for error message (e.g., "layout ID")
     * @param operation Operation name for error message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool validateNonEmpty(const QString& value, const QString& paramName, const QString& operation) const;

    /**
     * @brief Parse JSON string to QJsonObject with validation
     * @param jsonString JSON string to parse
     * @param operation Operation name for error messages
     * @return QJsonObject if valid, std::nullopt on parse error or non-object
     */
    std::optional<QJsonObject> parseJsonObject(const QString& jsonString, const QString& operation) const;

    /**
     * @brief Launch the editor with given arguments
     * @param args Command line arguments for the editor
     * @param description Description for logging (e.g., "for screen: DP-1")
     */
    void launchEditor(const QStringList& args, const QString& description);

    /**
     * @brief Build activity info JSON object
     * @param activityId The activity ID
     * @return QJsonObject with id, name, icon fields
     */
    QJsonObject buildActivityInfoJson(const QString& activityId) const;

    /**
     * @brief Drop any cached JSON for @p uuid so the next getLayout call
     * re-serializes from the live PhosphorZones::Layout. Also clears the active-layout
     * cache slot when the modified layout happens to be the active one,
     * otherwise getActiveLayout would keep serving the stale entry.
     * Centralizes cache invalidation for all per-layout mutation paths.
     */
    void invalidateLayoutJsonCacheFor(const QUuid& uuid);

    PhosphorZones::LayoutRegistry* m_layoutManager; // Concrete type for signal connections
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    ActivityManager* m_activityManager = nullptr;
    Phosphor::Screens::ScreenManager* m_screenManager = nullptr;
    ISettings* m_settings = nullptr;
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives adaptor
    PhosphorLayout::ILayoutSource* m_layoutSource = nullptr;
    /// Autotile-specific source used for buildUnifiedLayoutList preview-cache reuse.
    /// Separate from m_layoutSource (which is the full composite used for
    /// getLayoutPreview* D-Bus output).
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr;

    // Suppress screenLayoutChanged D-Bus signal during setAssignmentEntry —
    // the KCM initiated the change and doesn't need the echo back.
    bool m_suppressScreenLayoutSignal = false;

    // Track which screens had assignments modified during the current batch.
    // Populated by setAssignmentEntry/clearAssignment, consumed by applyAssignmentChanges.
    QSet<QString> m_changedScreenIds;

    // JSON caching for performance
    QString m_cachedActiveLayoutJson;
    QUuid m_cachedActiveLayoutId;
    QHash<QUuid, QString> m_cachedLayoutJson; // Cache for individual layouts

    // Coalesces bursts of ILayoutSource::contentsChanged into a single
    // layoutListChanged D-Bus emission. Autotile algorithm registration can
    // fire contentsChanged several times during startup / script hot-reload;
    // without debouncing, each hit would wake every KCM/editor client on
    // the bus. 200 ms is well under any human-visible refresh latency.
    QTimer m_layoutSourceCoalesce;
};

} // namespace PlasmaZones
