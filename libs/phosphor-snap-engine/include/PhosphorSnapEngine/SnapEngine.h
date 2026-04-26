// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngineTypes/EngineTypes.h>
#include <PhosphorEngineApi/IVirtualDesktopManager.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorEngineApi/IWindowTrackingService.h>
#include <PhosphorEngineApi/PlacementEngineBase.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorProtocol/WireTypes.h>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace PhosphorZones {
class IZoneDetector;
class LayoutRegistry;
class SnapState;
}

namespace PlasmaZones {

using NavigationContext = PhosphorEngineApi::NavigationContext;
using SnapResult = PhosphorEngineApi::SnapResult;
using SnapIntent = PhosphorEngineApi::SnapIntent;
using ZoneAssignmentEntry = PhosphorEngineApi::ZoneAssignmentEntry;
using UnfloatResult = PhosphorEngineApi::UnfloatResult;
using PendingRestore = PhosphorEngineApi::PendingRestore;
using ResnapEntry = PhosphorEngineApi::ResnapEntry;
using StickyWindowHandling = PhosphorEngineApi::StickyWindowHandling;

using PhosphorProtocol::CycleTargetResult;
using PhosphorProtocol::FocusTargetResult;
using PhosphorProtocol::MoveTargetResult;
using PhosphorProtocol::RestoreTargetResult;
using PhosphorProtocol::SnapAllResultEntry;
using PhosphorProtocol::SnapAllResultList;
using PhosphorProtocol::SwapTargetResult;
using PhosphorProtocol::WindowGeometryEntry;
using PhosphorProtocol::WindowGeometryList;
using PhosphorProtocol::WindowStateEntry;

class SnapNavigationTargetResolver;

/**
 * @brief Engine for manual zone-based window snapping
 *
 * Implements IPlacementEngine for screens using manual zone layouts (non-autotile).
 * Handles auto-snap on window open, zone-based navigation, floating state,
 * rotation, and resnap operations.
 *
 * Uses WindowTrackingService as a shared state store for zone assignments,
 * pre-tile geometries, and floating state. The snap engine adds behavior
 * on top: auto-snap fallback chains, directional navigation via zone
 * adjacency, and layout-change resnapping.
 *
 * @see PhosphorEngineApi::IPlacementEngine, AutotileEngine, WindowTrackingService
 */
class PHOSPHORSNAPENGINE_EXPORT SnapEngine : public PhosphorEngineApi::PlacementEngineBase
{
    Q_OBJECT

public:
    explicit SnapEngine(PhosphorZones::LayoutRegistry* layoutManager,
                        PhosphorEngineApi::IWindowTrackingService* windowTracker,
                        PhosphorZones::IZoneDetector* zoneDetector, PhosphorEngineApi::IVirtualDesktopManager* vdm,
                        QObject* parent = nullptr);
    ~SnapEngine() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    bool isActiveOnScreen(const QString& screenId) const override;
    using IPlacementEngine::windowOpened;
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat) override;
    void saveState() override;
    void loadState() override;

    // Cross-engine handoff
    QString engineId() const override
    {
        return QStringLiteral("snap");
    }
    void handoffReceive(const HandoffContext& ctx) override;
    void handoffRelease(const QString& windowId) override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    /// Whether this engine considers the window owned (snapped, snap-floated,
    /// or otherwise carried in SnapState's screen/zone maps). Used by the
    /// daemon to disambiguate which engine should handle a shortcut and to
    /// decide whether a cross-engine handoff is needed.
    bool isWindowTracked(const QString& windowId) const override;

    /**
     * @brief Resnap windows from previous layout to current layout after layout switch
     *
     * Maps windows by zone number (1->1, 2->2, etc.) with wrapping when new
     * layout has fewer zones.
     */
    void resnapToNewLayout();

    /**
     * @brief Resnap windows to their current zone assignments (re-apply geometries)
     * @param screenFilter Optional screen name filter (empty = all screens)
     */
    void resnapCurrentAssignments(const QString& screenFilter = QString());

    /**
     * @brief Resnap windows using autotile window order as assignment source
     * @param autotileWindowOrder Ordered list of window IDs from autotile engine
     * @param screenId Screen to resnap on
     * @note Falls back to resnapCurrentAssignments if no entries are calculated
     */
    void resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId);

    /**
     * @brief Calculate resnap entries from autotile order WITHOUT emitting signal
     *
     * Returns the computed ZoneAssignmentEntry vector so the caller can batch entries
     * from multiple screens into a single resnapToNewLayoutRequested emission.
     * Falls back to current-assignment entries if autotile order yields nothing.
     *
     * @param autotileWindowOrder Ordered list of window IDs from autotile engine
     * @param screenId Screen to resnap on
     * @return Vector of ZoneAssignmentEntry (may be empty)
     */
    QVector<ZoneAssignmentEntry> calculateResnapEntriesFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                         const QString& screenId);

    /**
     * @brief Calculate snap-all-windows assignments without applying them
     * @param windowIds List of window IDs to snap
     * @param screenId Screen to snap on
     * @return JSON array of zone assignment entries for KWin effect to apply
     */
    SnapAllResultList calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    /**
     * @brief Emit a single batched resnapToNewLayoutRequested signal
     *
     * Serializes the given entries and emits the D-Bus signal once.
     * Used by the daemon to combine entries from multiple screens into
     * one signal, eliminating the per-screen race condition.
     *
     * @param entries Combined ZoneAssignmentEntry vector from all screens
     */
    void emitBatchedResnap(const QVector<ZoneAssignmentEntry>& entries);

    /**
     * @brief Request the KWin effect to collect and snap all unsnapped windows
     * @param screenId Screen to operate on
     */
    void snapAllWindows(const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Window restore (auto-snap on window open)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resolve auto-snap for a newly opened window
     *
     * Runs the 4-level fallback chain:
     *   1. App rules (highest priority)
     *   2. Persisted zone (session restore)
     *   3. Auto-assign to empty zone
     *   4. Snap to last zone (final fallback)
     *
     * Returns a SnapResult so the D-Bus adaptor can unpack geometry for the
     * KWin effect. Also handles floating windows (skips snap, emits feedback).
     *
     * @param windowId Window identifier
     * @param screenId Screen where the window appeared
     * @param sticky Whether the window is on all desktops
     * @return SnapResult with geometry and zone info, or SnapResult::noSnap()
     */
    SnapResult resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky);

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile engine reference (for isActiveOnScreen routing)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set the autotile engine for screen ownership checks
     *
     * SnapEngine is active on screens where AutotileEngine is NOT active.
     * This must be called after the AutotileEngine is created.
     *
     * @param engine AutotileEngine instance (not owned, must outlive SnapEngine)
     */
    void setAutotileEngine(PhosphorEngineApi::IPlacementEngine* engine);

    PhosphorZones::SnapState* snapState() const
    {
        return m_snapState;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Zone detection adaptor (for daemon-driven navigation)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set ZoneDetectionAdaptor for adjacency queries
     *
     * @param adaptor ZoneDetectionAdaptor instance (not owned, must outlive SnapEngine)
     */
    void setZoneDetectionAdaptor(QObject* adaptor);

    // ═══════════════════════════════════════════════════════════════════════════
    // WindowTrackingAdaptor back-reference
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Wire the shared WindowTrackingAdaptor back-reference.
     *
     * SnapEngine owns the snap-mode navigation logic (focusInDirection,
     * moveFocusedInDirection, etc.) but some of the state those methods
     * consult — the SnapNavigationTargetResolver, the last-active-window
     * shadow, the frame-geometry shadow, and the snap-bookkeeping helpers
     * (windowSnapped / windowUnsnapped / storePreTileGeometry / ...) —
     * is still held on WindowTrackingAdaptor.
     *
     * This back-reference lets SnapEngine call into that state without
     * duplicating it. A future refactor should either move the state to
     * WindowTrackingService (for cross-cutting pieces like the bookkeeping
     * helpers) or onto SnapEngine itself (for snap-only pieces like the
     * target resolver) and retire this back-reference entirely. Until
     * then it's the honest expression of the coupling.
     *
     * Must be set after construction and before any navigation method is
     * called. Not owned; must outlive SnapEngine.
     */
    void setWindowTrackingAdaptor(QObject* adaptor);

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation (moved out of WindowTrackingAdaptor)
    //
    // Every method takes a NavigationContext populated by the daemon's
    // shortcut handler. The engine uses ctx.windowId directly rather than
    // re-reading it from the WTA shadow, which is a step toward retiring
    // the SnapEngine → WTA back-reference entirely. When ctx.windowId is
    // empty, the engine may consult m_wta->lastActiveWindowId() as a
    // best-effort fallback — but in the normal path the daemon always
    // provides a resolved target.
    // ═══════════════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — navigation overrides
    // ═══════════════════════════════════════════════════════════════════════════

    /// Walk to the adjacent window in @p direction and transfer keyboard focus.
    /// Empty direction is a no-op with feedback.
    void focusInDirection(const QString& direction, const NavigationContext& ctx) override;

    /// Move the focused window into the adjacent zone in @p direction
    /// (displacing or filling the target). Empty direction is a no-op.
    void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;

    /// Swap the focused window with whatever's in the adjacent zone in
    /// @p direction. Empty direction is a no-op.
    void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;

    /// Move the focused window to the layout zone with @p zoneNumber
    /// (1-based) on ctx.screenId. PhosphorZones::Zone numbers outside [1,9] are rejected.
    void moveFocusedToPosition(int zoneNumber, const NavigationContext& ctx) override;

    /// Rotate snapped windows through the layout's zone order, dispatched
    /// via IPlacementEngine. Forwards to rotateWindowsInLayout().
    void rotateWindows(bool clockwise, const NavigationContext& ctx) override;

    /// Re-apply the current layout to all managed windows. Forwards to
    /// resnapToNewLayout().
    void reapplyLayout(const NavigationContext& ctx) override;

    /// Snap every unmanaged window on the screen. The IPlacementEngine
    /// override takes NavigationContext; coexists with the existing
    /// snapAllWindows(const QString&) method which it delegates to.
    void snapAllWindows(const NavigationContext& ctx) override;

    /// Move the focused window to the first empty zone on ctx.screenId.
    void pushToEmptyZone(const NavigationContext& ctx) override;

    /// Restore the focused window to its captured pre-snap size and unsnap.
    void restoreFocusedWindow(const NavigationContext& ctx) override;

    /// Toggle the focused window between snapped and floating.
    void toggleFocusedFloat(const NavigationContext& ctx) override;

    /// Cycle keyboard focus forward/backward through managed windows in
    /// the active zone (or the layout cycle order if single-window per
    /// zone).
    void cycleFocus(bool forward, const NavigationContext& ctx) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap commit orchestration
    //
    // Full snap lifecycle: clear floating, assign to zone, update tracking,
    // emit state-changed signals. Owns the orchestration that was previously
    // on WindowTrackingService.
    // ═══════════════════════════════════════════════════════════════════════════

    void commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId,
                    SnapIntent intent = SnapIntent::UserInitiated);

    void commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             SnapIntent intent = SnapIntent::UserInitiated);

    void uncommitSnap(const QString& windowId);

    UnfloatResult resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const;

    WindowGeometryList applyBatchAssignments(const QVector<ZoneAssignmentEntry>& entries,
                                             SnapIntent intent = SnapIntent::UserInitiated,
                                             std::function<QString()> fallbackScreenResolver = {});

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap calculations (moved from WTS)
    // ═══════════════════════════════════════════════════════════════════════════

    SnapResult calculateSnapToAppRule(const QString& windowId, const QString& windowScreenName, bool isSticky) const;
    SnapResult calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId, bool isSticky) const;
    SnapResult calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool isSticky) const;
    SnapResult calculateRestoreFromSession(const QString& windowId, const QString& screenId, bool isSticky) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap / rotation calculations (moved from WTS)
    // ═══════════════════════════════════════════════════════════════════════════

    QVector<ZoneAssignmentEntry> calculateResnapFromPreviousLayout();
    QVector<ZoneAssignmentEntry> calculateResnapFromCurrentAssignments(const QString& screenFilter = QString()) const;
    QVector<ZoneAssignmentEntry> calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                  const QString& screenId) const;
    QVector<ZoneAssignmentEntry> calculateSnapAllWindowEntries(const QStringList& windowIds,
                                                               const QString& screenId) const;
    QVector<ZoneAssignmentEntry> calculateRotation(bool clockwise, const QString& screenFilter = QString()) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Saved snap-floating windows (mode-transition bookkeeping)
    // ═══════════════════════════════════════════════════════════════════════════

    void saveSnapFloating(const QString& windowId);
    bool restoreSnapFloating(const QString& windowId);
    void clearSavedSnapFloating();

    // IPlacementEngine — generic mode-float overrides
    bool restoreSavedModeFloat(const QString& windowId) override
    {
        return restoreSnapFloating(windowId);
    }
    void clearSavedFloatingForWindows(const QStringList& windowIds) override
    {
        for (const QString& windowId : windowIds) {
            m_savedSnapFloatingWindows.remove(windowId);
        }
    }
    void saveModeFloat(const QString& windowId) override
    {
        saveSnapFloating(windowId);
    }
    void clearSavedModeFloating() override
    {
        clearSavedSnapFloating();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Effect-reported windows (runtime flag — not persisted)
    // ═══════════════════════════════════════════════════════════════════════════

    void markWindowReported(const QString& windowId);
    const QSet<QString>& effectReportedWindows() const
    {
        return m_effectReportedWindows;
    }

    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — state access
    //
    // Returns the single SnapState wired by Daemon::init(). Currently a
    // global state (not per-screen); a future PR will introduce per-screen
    // ownership. Returns nullptr in headless unit tests that don't wire a
    // SnapState.
    // ═══════════════════════════════════════════════════════════════════════════

    PhosphorEngineApi::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngineApi::IPlacementState* stateForScreen(const QString& screenId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Internal navigation helpers (concrete SnapEngine methods that the
    // IPlacementEngine overrides delegate to)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Move the focused window to the first empty zone on ctx.screenId.
    /// The IPlacementEngine override pushToEmptyZone() delegates here.
    void pushFocusedToEmptyZone(const NavigationContext& ctx);

    /// Rotate snapped windows through the layout's zone order on @p screenId.
    void rotateWindowsInLayout(bool clockwise, const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence delegate
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set persistence callbacks for save/load
     *
     * KConfig persistence is owned by WindowTrackingAdaptor (WTS is KConfig-free).
     * These callbacks allow SnapEngine to fulfill the IPlacementEngine persistence
     * contract without introducing KConfig as a dependency.
     *
     * @param saveFn Called by saveState() to persist WTS state
     * @param loadFn Called by loadState() to restore WTS state
     */
    void setPersistenceDelegate(std::function<void()> saveFn, std::function<void()> loadFn)
    {
        m_saveFn = std::move(saveFn);
        m_loadFn = std::move(loadFn);
    }

    /// Last screen the engine saw via windowFocused. Used for OSD fallback
    /// when a navigation failure needs to cite a screen and the live cursor
    /// hasn't landed on one yet. Exposed as a const getter so tests can
    /// verify the focus-tracking contract without dummy signal stubs.
    QString lastActiveScreenId() const
    {
        return m_lastActiveScreenId;
    }

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Signals (relayed via SnapAdaptor → WTA → D-Bus → effect)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Snap state changed (commit / uncommit). WTA relays to D-Bus windowStateChanged.
    void windowSnapStateChanged(const QString& windowId, const WindowStateEntry& entry);

    /// Floating state cleared as part of a commit. WTA relays as windowFloatingChanged(id, false, screen).
    void windowFloatingClearedForSnap(const QString& windowId, const QString& screenId);

    /// Daemon-driven geometry application (used by autotile float restore)
    void applyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                const QString& screenId, bool sizeOnly);

    /// Batched resnap data (routed through WTA::handleBatchedResnap for bookkeeping)
    void resnapToNewLayoutRequested(const QString& resnapData);

    /// Request KWin effect to collect unsnapped windows and snap them all
    void snapAllWindowsRequested(const QString& screenId);

    /// Batch of window-geometry updates, applied by the KWin effect in a
    /// single operation (rotate, resnap, snap-all paths). The @p action
    /// label disambiguates the cause downstream ("rotate", "resnap",
    /// "snap_all", "vs_reconfigure"). Relayed to D-Bus via WTA.
    void applyGeometriesBatch(const PlasmaZones::WindowGeometryList& geometries, const QString& action);

protected:
    void onWindowClaimed(const QString& windowId) override;
    void onWindowReleased(const QString& windowId) override;
    void onWindowFloated(const QString& windowId) override;
    void onWindowUnfloated(const QString& windowId) override;

private:
    PhosphorEngineApi::ISnapSettings* snapSettings() const;

    struct GapParams
    {
        int zonePadding;
        ::PhosphorLayout::EdgeGaps outerGaps;
    };
    GapParams resolveGapParams() const;

    void commitSnapImpl(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                        SnapIntent intent);

    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorEngineApi::IWindowTrackingService* m_windowTracker = nullptr;
    PhosphorZones::SnapState* m_snapState = nullptr;
    PhosphorZones::IZoneDetector* m_zoneDetector = nullptr;
    PhosphorEngineApi::IVirtualDesktopManager* m_virtualDesktopManager = nullptr;
    QPointer<QObject> m_autotileEngineObj;
    PhosphorEngineApi::IPlacementEngine* m_autotileEngineTyped = nullptr;
    QPointer<QObject> m_zoneDetectionAdaptor;
    // Back-reference to WindowTrackingAdaptor for state SnapEngine reads
    // but doesn't own yet: the last-active-window / last-active-screen /
    // last-cursor-screen shadows populated via D-Bus windowActivated and
    // cursorScreenChanged slots, plus the frame-geometry shadow populated
    // via setFrameGeometry. Not owned. Remaining uses are all read-only
    // accessors — SnapEngine no longer routes BEHAVIOUR through WTA.
    QPointer<QObject> m_wta;
    // Snap-mode navigation target resolver. Owned by SnapEngine — moved
    // here in Phase 5E from WindowTrackingAdaptor. Constructed lazily on
    // first navigation call (so the construction order isn't constrained
    // by the fact that SnapEngine has to exist before a resolver that
    // takes WTS + PhosphorZones::LayoutRegistry can be built).
    std::unique_ptr<SnapNavigationTargetResolver> m_targetResolver;
    QSet<QString> m_savedSnapFloatingWindows;
    QSet<QString> m_effectReportedWindows;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float helpers (snapengine/float.cpp)
    //
    // The historical clearFloatingStateForSnap / assignToZones pair was
    // removed — all snap commits now route through commitSnapImpl.
    // ═══════════════════════════════════════════════════════════════════════════

    bool unfloatToZone(const QString& windowId, const QString& screenId);
    bool applyGeometryForFloat(const QString& windowId, const QString& screenId);

    /// Lazy-constructs m_targetResolver on first call. Returns nullptr if
    /// the service or layout manager is missing (unit tests with stub
    /// deps, or shutdown race where WTS/PhosphorZones::LayoutRegistry are already gone).
    ///
    /// When @p action is non-empty and the resolver can't be built, emits
    /// navigationFeedback(false, action, "engine_unavailable", ...) so the
    /// OSD shows a specific reason instead of a silent no-op. Pass an
    /// empty @p action to skip the emit (for call sites that want to
    /// handle the null case themselves).
    SnapNavigationTargetResolver* ensureTargetResolver(const QString& action = QString());

    /// Check whether the window is excluded from the given navigation
    /// action by the user's excluded-apps / excluded-classes rules.
    /// Emits navigationFeedback(false, action, "excluded", ...) and returns
    /// true when excluded so callers can early-return. False otherwise.
    bool isWindowExcludedForAction(const QString& windowId, const QString& action, const QString& screenId);

    // Persistence delegates (KConfig stays in adaptor layer)
    std::function<void()> m_saveFn;
    std::function<void()> m_loadFn;

    // Last-focused screen (updated by windowFocused)
    QString m_lastActiveScreenId;
};

} // namespace PlasmaZones
