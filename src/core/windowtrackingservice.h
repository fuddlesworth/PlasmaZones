// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "types.h"
#include <PhosphorProtocol/WireTypes.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QRect>
#include <functional>
#include <optional>
#include <utility>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class LayoutRegistry;
class SnapState;
class Zone;
}

namespace PlasmaZones {
class ISettings;
// Phosphor::Screens::ScreenManager moved to libs/phosphor-screens (Phosphor::Screens::ScreenManager).
} // namespace PlasmaZones
namespace Phosphor::Screens {
class ScreenManager;
}
namespace PlasmaZones {

using PhosphorProtocol::EmptyZoneList;
using PhosphorProtocol::WindowGeometryEntry;
using PhosphorProtocol::WindowGeometryList;
using PhosphorProtocol::WindowStateEntry;

class VirtualDesktopManager;
class WindowRegistry;

/**
 * @brief Window-zone tracking service (business logic layer)
 *
 * This service encapsulates all window tracking business logic that was
 * previously in WindowTrackingAdaptor. Following separation of concerns,
 * Principle, it handles:
 *
 * - PhosphorZones::Zone assignment management (which window is in which zone)
 * - Pre-snap geometry storage (for restoring original size)
 * - Floating window state tracking
 * - Session persistence (save/load state across restarts)
 * - Auto-snap logic (snap new windows to last zone)
 * - Window rotation calculations
 *
 * The WindowTrackingAdaptor becomes a thin D-Bus facade that delegates
 * all business logic to this service.
 *
 * Design benefits:
 * - Testable: Service can be unit tested without D-Bus
 * - Reusable: Logic can be used by other components
 * - Maintainable: Clear separation of concerns
 * - Debuggable: Easier to trace logic flow
 */
class PLASMAZONES_EXPORT WindowTrackingService : public QObject
{
    Q_OBJECT

public:
    explicit WindowTrackingService(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorZones::IZoneDetector* zoneDetector,
                                   Phosphor::Screens::ScreenManager* screenManager, ISettings* settings,
                                   VirtualDesktopManager* vdm, QObject* parent = nullptr);
    ~WindowTrackingService() override;

    /**
     * @brief Wire up the shared WindowRegistry.
     *
     * Optional — unit tests construct WTS without a registry and fall back to
     * parsing composite windowIds. Production daemons set this so the service
     * queries live class via appIdFor() and ignores first-seen strings.
     *
     * Must be set before start. Not owned.
     */
    void setWindowRegistry(WindowRegistry* registry)
    {
        m_windowRegistry = registry;
    }

    void setSnapState(PhosphorZones::SnapState* state)
    {
        m_snapState = state;
    }

    /**
     * @brief Accessor for consumers that need direct access (effect, adaptor).
     */
    WindowRegistry* windowRegistry() const
    {
        return m_windowRegistry;
    }

    Phosphor::Screens::ScreenManager* screenManager() const
    {
        return m_screenManager;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Zone Assignment Management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Assign a window to a zone
     * @param windowId Full window ID
     * @param zoneId PhosphorZones::Zone UUID string
     * @param screenId Screen where the zone is located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                            int virtualDesktop);

    /**
     * @brief Assign a window to multiple zones (multi-zone snap)
     * @param windowId Full window ID
     * @param zoneIds List of zone UUID strings (first is primary)
     * @param screenId Screen where the zones are located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             int virtualDesktop);

    /**
     * @brief Remove window from its assigned zone
     * @param windowId Full window ID
     */
    void unassignWindow(const QString& windowId);

    /**
     * @brief Get the primary zone ID for a window
     * @param windowId Full window ID
     * @return PhosphorZones::Zone ID or empty string if not assigned
     */
    QString zoneForWindow(const QString& windowId) const;

    /**
     * @brief Get all zone IDs for a window (multi-zone support)
     * @param windowId Full window ID
     * @return List of zone IDs (empty if not assigned)
     */
    QStringList zonesForWindow(const QString& windowId) const;

    /**
     * @brief Get all windows in a specific zone
     * @param zoneId PhosphorZones::Zone UUID string
     * @return List of window IDs
     */
    QStringList windowsInZone(const QString& zoneId) const;

    /**
     * @brief Get all snapped windows
     * @return List of window IDs that are currently snapped
     */
    QStringList snappedWindows() const;

    /// Remove zone/screen/desktop assignments for windows not in the alive set.
    /// Returns the number of pruned entries.
    int pruneStaleAssignments(const QSet<QString>& aliveWindowIds);

    /**
     * @brief Check if a window is assigned to any zone
     */
    bool isWindowSnapped(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-Tile Geometry Storage (unified snap + autotile)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Stored pre-tile geometry with screen context
     *
     * Tracks both the geometry and the screen where it was captured, so that
     * cross-screen restores can detect coordinate mismatch and adjust.
     */
    struct PreTileGeometry
    {
        QRect geometry;
        QString connectorName; ///< connector name at time of save (may be empty for legacy entries)
    };

    /**
     * @brief Store geometry before tiling (snap or autotile)
     * @param windowId Full window ID
     * @param geometry Window geometry before tiling
     * @param connectorName Screen connector name where the window currently is
     * @param overwrite If false (snap mode), skip if entry already exists (first-only).
     *                  If true (autotile mode), always overwrite.
     */
    void storePreTileGeometry(const QString& windowId, const QRect& geometry, const QString& connectorName = QString(),
                              bool overwrite = false);

    /**
     * @brief Get stored pre-tile geometry
     * @param windowId Full window ID
     * @return Geometry if stored, nullopt otherwise
     */
    std::optional<QRect> preTileGeometry(const QString& windowId) const;

    /**
     * @brief Check if window has stored pre-tile geometry
     */
    bool hasPreTileGeometry(const QString& windowId) const;

    /**
     * @brief Clear stored pre-tile geometry (after restore)
     */
    void clearPreTileGeometry(const QString& windowId);

    /**
     * @brief Get validated pre-tile geometry within screen bounds
     * @param windowId Full window ID
     * @param currentScreenName Screen where the window currently is (for cross-screen adjustment).
     *        If empty, uses existing isGeometryOnScreen/adjustGeometryToScreen logic.
     * @return Adjusted geometry within visible screens, nullopt if not found
     */
    std::optional<QRect> validatedPreTileGeometry(const QString& windowId,
                                                  const QString& currentScreenName = QString()) const;

    /**
     * @brief Strict per-instance lookup: no appId fallback.
     *
     * Returns the pre-tile geometry ONLY when an entry was captured for the
     * EXACT runtime windowId. Cross-session appId-keyed entries (which may
     * hold stale coordinates from a prior instance or daemon session) are
     * deliberately ignored.
     *
     * Use this for restores that must not teleport a window to ancient
     * coordinates left behind by a ghost instance — e.g. autotile→snap
     * mode-toggle restoring windows that were never explicitly floated.
     *
     * @param windowId Full window ID (must include the '|uuid' suffix)
     * @param currentScreenName Screen where the window currently is (for
     *        cross-screen adjustment; see validatedPreTileGeometry)
     * @return Adjusted geometry within visible screens, nullopt if no exact
     *         per-instance entry exists
     */
    std::optional<QRect> validatedPreTileGeometryExact(const QString& windowId,
                                                       const QString& currentScreenName = QString()) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating Window State
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a window is floating (excluded from snapping)
     */
    bool isWindowFloating(const QString& windowId) const;

    /**
     * @brief Set window floating state
     * @param windowId Full window ID
     * @param floating true to float, false to unfloat
     */
    void setWindowFloating(const QString& windowId, bool floating);

    /**
     * @brief Get all floating window IDs
     */
    QStringList floatingWindows() const;

    /**
     * @brief Mark a window's float as originating from autotile mode
     *
     * Used to distinguish autotile-originated floats from manual snapping-mode
     * floats. Only autotile floats are cleared on autotile→snapping transitions;
     * manual floats are preserved. Must be called AFTER setWindowFloating(true).
     */
    void markAutotileFloated(const QString& windowId);

    /**
     * @brief Clear autotile-floated origin for a window
     */
    void clearAutotileFloated(const QString& windowId);

    /**
     * @brief Check if a window was floated by the autotile engine (not manually in snapping mode)
     */
    bool isAutotileFloated(const QString& windowId) const;

    /**
     * @brief Save a snap-mode floating window for later restoration
     *
     * Called when a snap-mode-floated window enters autotile and its WTS
     * floating state is cleared. Mirrors the engine's m_savedFloatingWindows.
     */
    void saveSnapFloating(const QString& windowId);

    /**
     * @brief Consume and restore a saved snap-mode floating window
     * @return true if the window was in the saved set
     */
    bool restoreSnapFloating(const QString& windowId);

    /**
     * @brief Clear all saved snap-mode floating state
     */
    void clearSavedSnapFloating();

    /**
     * @brief Unsnap window for floating (saves zone for later restore)
     * @param windowId Full window ID
     */
    void unsnapForFloat(const QString& windowId);

    /**
     * @brief Get primary zone to restore to when unfloating
     * @param windowId Full window ID
     * @return PhosphorZones::Zone ID or empty string if none
     */
    QString preFloatZone(const QString& windowId) const;

    /**
     * @brief Get all zones to restore to when unfloating (multi-zone support)
     * @param windowId Full window ID
     * @return List of zone IDs (empty if none)
     */
    QStringList preFloatZones(const QString& windowId) const;

    /**
     * @brief Get the screen name where the window was snapped before floating
     * @param windowId Full window ID
     * @return Screen name or empty string if unknown
     */
    QString preFloatScreen(const QString& windowId) const;

    /**
     * @brief Clear pre-float zone after restore (both windowId and appId keys)
     */
    void clearPreFloatZone(const QString& windowId);

    /**
     * @brief Clear pre-float zone for a specific window only (not appId)
     *
     * Used by autotile float sync to avoid destroying sibling instances' data.
     */
    void clearPreFloatZoneForWindow(const QString& windowId);

    /**
     * @brief Clear floating state when snapping a floating window
     *
     * Atomically clears floating flag and pre-float zone data.
     * Shared logic used by both SnapEngine and WindowTrackingAdaptor
     * to avoid duplicating the isFloating → clear → clearPreFloat pattern.
     *
     * @param windowId Window identifier
     * @return true if the window was floating (caller should emit windowFloatingChanged)
     */
    bool clearFloatingForSnap(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Sticky Window Handling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Record whether a window is sticky (on all desktops)
     */
    void setWindowSticky(const QString& windowId, bool sticky);

    /**
     * @brief Check if window is sticky
     */
    bool isWindowSticky(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Snap Logic
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Record that a window class was user-snapped
     * @param windowId Full window ID to extract class from
     * @param wasUserInitiated true if user-initiated snap
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    /**
     * @brief Get last used zone ID
     */
    QString lastUsedZoneId() const;

    /**
     * @brief App class string stamped on the last-used-zone tracking.
     *
     * Used by the reactive metadata handler to detect stale class tags after
     * a mid-session rename.
     */
    QString lastUsedZoneClass() const;

    /**
     * @brief Update the last-used-zone class tag without touching zone/screen.
     *
     * Called by the reactive metadata handler when a window renames mid-session
     * and its old class was the class tracked on last-used-zone. Only the
     * class string is refreshed so the next auto-snap-by-class lookup matches
     * against the live name.
     */
    void retagLastUsedZoneClass(const QString& newClass);

    /**
     * @brief Update last used zone tracking
     */
    void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                            int virtualDesktop);

    /**
     * @brief Mark a window as auto-snapped
     *
     * Auto-snapped windows should not update the last-used zone tracking
     * when snapped. This prevents unwanted zone changes when windows are
     * automatically restored on open.
     *
     * @param windowId Full window ID
     */
    void markAsAutoSnapped(const QString& windowId);

    /**
     * @brief Check if a window was auto-snapped
     * @param windowId Full window ID
     * @return true if the window was auto-snapped (not user-initiated)
     */
    bool isAutoSnapped(const QString& windowId) const;

    /**
     * @brief Clear auto-snapped flag for a window
     * @param windowId Full window ID
     * @return true if the window had the auto-snapped flag
     */
    bool clearAutoSnapped(const QString& windowId);

    /**
     * @brief Pop the oldest pending restore entry for this window's appId.
     *
     * The pending-restore queue is keyed by appId (FIFO), mirroring KWin's
     * takeSessionInfo pattern. Every call to this method consumes at most
     * one entry — the oldest one — and erases the queue entry entirely once
     * it's emptied. Call sites:
     *
     *   1. After a successful session restore — so the same window isn't
     *      restored again if reopened, and so other instances of the same
     *      app class don't incorrectly restore onto this window's zone.
     *   2. After a user-initiated snap or unsnap — so a stale entry from a
     *      previous session doesn't drag the window back to a different zone
     *      on its next close/reopen cycle.
     *
     * There is no "stale" vs "fresh" distinction inside the queue: every
     * entry is a FIFO head, and this method pops the head regardless of
     * provenance. Earlier versions split this into two methods
     * (consumePendingAssignment / clearStalePendingAssignment) that were
     * implementation-identical but named as if they did different things;
     * the duplication has been removed.
     *
     * @param windowId Full window ID — the appId is resolved via
     *                 currentAppIdFor() so the queue lookup sees the live
     *                 class (Electron/CEF apps that mutate their class
     *                 mid-session still hit the right queue).
     * @return true if an entry was popped, false if the queue was empty.
     *         Callers that don't care about the result may ignore it.
     */
    bool consumePendingAssignment(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Helpers
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Find the first empty zone in the layout for a screen
     * @param screenId Screen to find layout for (empty = active layout)
     * @return PhosphorZones::Zone ID or empty string if all occupied
     */
    QString findEmptyZone(const QString& screenId = QString()) const;

    /**
     * @brief Get typed list of all empty zones for Snap Assist continuation
     * @param screenId Screen to find layout for (e.g. DP-1)
     * @return EmptyZoneList of empty zone entries with overlay-local geometry
     */
    EmptyZoneList getEmptyZones(const QString& screenId) const;

    /**
     * @brief Get geometry for a zone on a specific screen
     * @param zoneId PhosphorZones::Zone UUID string
     * @param screenId Screen identifier (empty = primary)
     * @return PhosphorZones::Zone geometry in pixels, or invalid QRect if not found
     */
    QRect zoneGeometry(const QString& zoneId, const QString& screenId = QString()) const;

    /**
     * @brief Get combined geometry for multiple zones on a specific screen
     * @param zoneIds List of zone UUID strings
     * @param screenId Screen identifier (empty = primary)
     * @return Union of all zone geometries, or invalid QRect if none found
     */
    QRect multiZoneGeometry(const QStringList& zoneIds, const QString& screenId = QString()) const;

    /**
     * @brief Populate the resnap buffer for all screens independently.
     *
     * For each window, looks up its current zone assignment and determines
     * the zone position using a global zoneId→position map built from all
     * loaded layouts. This avoids relying on the global activeLayout/previousLayout
     * which only tracks one layout at a time.
     *
     * Used by the KCM save path where multiple screens can have different
     * layout assignments changed simultaneously.
     *
     * @param excludeScreens Screens to skip (e.g. autotile screens handled separately)
     * @param includeScreens When non-empty, only process windows on these screens.
     *        Restricts resnap to screens whose layout actually changed.
     */
    void populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens = {},
                                           const QSet<QString>& includeScreens = {});

    /**
     * @brief Clear the resnap buffer
     *
     * Called when virtual screen configuration changes to prevent stale
     * resnap data from referencing old screen IDs.
     */
    void clearResnapBuffer()
    {
        m_resnapBuffer.clear();
    }

    /**
     * @brief Build a zone-ordered window list for a screen from current zone assignments
     *
     * Iterates all window-zone assignments for the given screen, resolves each window's
     * primary zone number from the active layout, and returns the window IDs sorted by
     * zone number ascending. Used to pre-seed autotile window order during transitions.
     *
     * @param screenId Screen identifier to filter windows by
     * @return Window IDs sorted by zone number ascending
     */
    QStringList buildZoneOrderedWindowList(const QString& screenId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Virtual Screen Migration
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Migrate window screen assignments from physical to virtual screen IDs
     *
     * Windows snapped before virtual screens were configured have physical screen IDs
     * in m_windowScreenAssignments. When virtual screens are active, all per-screen
     * lookups use virtual IDs, so these windows become invisible to zone occupancy
     * checks, snap assist, float/unfloat, etc.
     *
     * This method iterates all screen assignments and, for any window whose screen
     * matches the given physical screen, determines which virtual screen the window's
     * zone falls within and updates the assignment accordingly.
     *
     * Also migrates m_preFloatScreenAssignments.
     *
     * @param physicalScreenId The physical screen being subdivided
     * @param virtualScreenIds Virtual screen IDs for the physical screen
     * @param mgr Phosphor::Screens::ScreenManager for geometry lookups
     */
    void migrateScreenAssignmentsToVirtual(const QString& physicalScreenId, const QStringList& virtualScreenIds,
                                           Phosphor::Screens::ScreenManager* mgr);

    /**
     * @brief Reverse migration: virtual screen IDs → physical screen ID
     *
     * Called when virtual screen configuration is removed for a physical screen.
     * Strips the "/vs:N" suffix from all tracked window screen assignments that
     * belong to the given physical screen, reverting them to the physical ID.
     *
     * @param physicalScreenId The physical screen ID to migrate back to
     */
    void migrateScreenAssignmentsFromVirtual(const QString& physicalScreenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Resolution Change Handling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get updated geometries for all tracked windows
     * @return Map of windowId -> new geometry
     *
     * Used when screen resolution changes to recalculate zone positions.
     */
    QHash<QString, QRect> updatedWindowGeometries() const;

    /**
     * @brief Pre-computed snap restore target: zone geometry + the saved screen it lives on.
     *
     * The effect-side cache carries both so it can tell "saved zone is on
     * snap-mode screen X" from "current KWin placement is autotile screen Y",
     * enabling correct cross-VS / cross-monitor restores instead of gating on
     * wherever KWin happened to drop the window.
     */
    struct PendingRestoreTarget
    {
        QRect geometry;
        QString screenId;
    };

    /**
     * @brief Pre-compute zone geometries for all pending restore entries.
     * @return Map of appId -> {geometry, savedScreenId}
     *
     * Used by the KWin effect to cache expected snap positions so that
     * windows can be teleported to their zone immediately on windowAdded,
     * eliminating the visible "flash" from KWin's session-restored position.
     * Only the first entry per appId is returned (FIFO consumption order).
     * Entries whose saved screen is currently in autotile mode are skipped:
     * the effect cache is a snap-mode-only fast path, and autotile on the
     * saved screen will own placement. Validates layout/desktop context so
     * the cache never contains geometries the async resolver would reject.
     */
    QHash<QString, PendingRestoreTarget> pendingRestoreGeometries() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Clean up all tracking data for a closed window
     * @param windowId Full window ID
     */
    void windowClosed(const QString& windowId);

    /**
     * @brief Handle layout change - validate/clear stale zone assignments
     */
    void onLayoutChanged();

    // ═══════════════════════════════════════════════════════════════════════════
    // State Access (for adaptor persistence via KConfig)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get all zone assignments for persistence
     * @return Map of windowId -> zoneIds (list of zone UUIDs)
     */
    const QHash<QString, QStringList>& zoneAssignments() const;

    /**
     * @brief Get all screen assignments for persistence
     */
    const QHash<QString, QString>& screenAssignments() const;

    /**
     * @brief Get all desktop assignments for persistence
     */
    const QHash<QString, int>& desktopAssignments() const;

    /**
     * @brief Get all pre-tile geometries for persistence
     */
    const QHash<QString, PreTileGeometry>& preTileGeometries() const
    {
        return m_preTileGeometries;
    }

    /**
     * @brief Pending restore entry for a single window instance
     *
     * Stores all data needed to restore a window to its previous zone.
     * Multiple entries per appId support multi-instance apps (e.g. 3 Konsole windows).
     */
    struct PendingRestore
    {
        QStringList zoneIds;
        QString screenId;
        int virtualDesktop = 0;
        QString layoutId;
        QList<int> zoneNumbers;
    };

    using ResnapEntry = PlasmaZones::ResnapEntry;

    /**
     * @brief Get pending restore queues (consumption queue: appId -> list of pending restores)
     */
    const QHash<QString, QList<PendingRestore>>& pendingRestoreQueues() const
    {
        return m_pendingRestoreQueues;
    }

    QVector<ResnapEntry> takeResnapBuffer()
    {
        return std::exchange(m_resnapBuffer, {});
    }

    /**
     * @brief Get user-snapped classes
     */
    const QSet<QString>& userSnappedClasses() const;

    /**
     * @brief Set active zone/screen/desktop assignments (loaded from KConfig by adaptor)
     *
     * Used to restore exact window-to-zone mappings after daemon-only restart
     * (KWin still running, so internalId UUIDs are stable). Prevents wrong-instance
     * restore for multi-instance apps (e.g. 2 Ghostty windows, only 1 was snapped).
     */
    void setActiveAssignments(const QHash<QString, QStringList>& zones, const QHash<QString, QString>& screens,
                              const QHash<QString, int>& desktops);

    /**
     * @brief Set pending restore queues (loaded from KConfig by adaptor)
     */
    void setPendingRestoreQueues(const QHash<QString, QList<PendingRestore>>& queues)
    {
        m_pendingRestoreQueues = queues;
    }

    /**
     * @brief Set pre-tile geometries (loaded from KConfig by adaptor)
     */
    void setPreTileGeometries(const QHash<QString, PreTileGeometry>& geometries)
    {
        m_preTileGeometries = geometries;
    }

    /**
     * @brief Set user-snapped classes (loaded from KConfig by adaptor)
     */
    void setUserSnappedClasses(const QSet<QString>& classes);

    /**
     * @brief Set last used zone info (loaded from KConfig by adaptor)
     */
    void setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    /**
     * @brief Set floating windows (loaded from KConfig by adaptor)
     */
    void setFloatingWindows(const QSet<QString>& windows)
    {
        m_floatingWindows = windows;
    }

    /**
     * @brief Get pre-float zone assignments for persistence
     */
    const QHash<QString, QStringList>& preFloatZoneAssignments() const
    {
        return m_preFloatZoneAssignments;
    }
    const QHash<QString, QString>& preFloatScreenAssignments() const
    {
        return m_preFloatScreenAssignments;
    }

    /**
     * @brief Set pre-float zone assignments (loaded from KConfig by adaptor)
     */
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments)
    {
        m_preFloatZoneAssignments = assignments;
    }
    void setPreFloatScreenAssignments(const QHash<QString, QString>& assignments)
    {
        m_preFloatScreenAssignments = assignments;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Dirty field tracking (Phase 3 of refactor/dbus-performance)
    //
    // Replaces "any mutation forces a full re-serialization" with a bitfield
    // mask of which persisted state has changed since the last successful
    // save. WindowTrackingAdaptor::saveState() reads this mask to decide
    // which JSON maps to re-write, and the persistence worker's write-
    // completed signal clears the committed bits — surviving bits either
    // represent new mutations that landed during the in-flight write, or
    // the write itself failed (same treatment in both cases: retry on the
    // next tick).
    //
    // The mask is initialized to All so the first save after a daemon
    // startup always writes every field. loadState() should clear the mask
    // immediately after populating in-memory state so the first real save
    // doesn't redundantly write back what we just loaded.
    // ═══════════════════════════════════════════════════════════════════════
    enum DirtyField : uint32_t {
        DirtyNone = 0,
        DirtyActiveLayoutId = 1u << 0,
        DirtyZoneAssignments = 1u << 1, // zones + screens + desktops (always written together)
        DirtyPendingRestores = 1u << 2,
        DirtyPreTileGeometries = 1u << 3,
        DirtyLastUsedZone = 1u << 4,
        DirtyPreFloatZones = 1u << 5,
        DirtyPreFloatScreens = 1u << 6,
        DirtyUserSnapped = 1u << 7,
        DirtyAutotileOrders = 1u << 8,
        DirtyAutotilePending = 1u << 9,
        DirtyAll = 0x3FFu,
    };
    using DirtyMask = uint32_t;

    /// OR the given fields into the dirty mask AND emit stateChanged.
    /// Primary (and only) entry point for mutators — replaces direct
    /// scheduleSaveState(). Public because the adaptor also needs to
    /// mark dirty from outside, e.g. when the active-layout change is
    /// observed via PhosphorZones::LayoutRegistry or when a failed async write needs
    /// its bits re-marked for retry. Multiple calls are idempotent
    /// (OR semantics) and cheap (bit OR + one signal emission).
    void markDirty(DirtyMask fields);

    /// Return the current dirty mask, clearing it atomically. Used by the
    /// adaptor's saveState() to snapshot "what needs writing" in one step.
    DirtyMask takeDirty();

    /// Return the current dirty mask without clearing. Read-only accessor
    /// for tests and instrumentation.
    DirtyMask peekDirty() const
    {
        return m_dirtyMask;
    }

    /// Clear every dirty bit. Called from loadState's end after in-memory
    /// state mirrors the disk file — nothing is dirty until the next
    /// mutation lands.
    void clearDirty();

Q_SIGNALS:
    /**
     * @brief Emitted when a window's zone assignment changes
     */
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /**
     * @brief Emitted when state needs to be saved
     */
    void stateChanged();

private:
    // Minimum visible area for geometry validation
    static constexpr int MinVisibleWidth = 100;
    static constexpr int MinVisibleHeight = 100;

    // Helpers
    //
    // scheduleSaveState() wraps markDirty(DirtyAll). Retained as the
    // default entry point for mutators that haven't been updated to
    // declare which specific fields they touch — marking everything dirty
    // is behaviorally equivalent to the pre-refactor code. Hot-path
    // mutators (assign/unassign zone, storePreTileGeometry, etc.) should
    // call markDirty() directly with a narrow mask so the next save
    // only re-serializes the fields that actually changed.
    void scheduleSaveState(DirtyMask fields = DirtyAll);
    bool isGeometryOnScreen(const QRect& geometry) const;
    QRect adjustGeometryToScreen(const QRect& geometry) const;
    PhosphorZones::Zone* findZoneById(const QString& zoneId) const;

    /// Shared implementation for validatedPreTileGeometry and validatedPreTileGeometryExact.
    /// When exactOnly is true, does not fall back to the appId-keyed entry.
    std::optional<QRect> validatePreTileEntry(const QString& windowId, const QString& currentScreenName,
                                              bool exactOnly) const;

    /// Clear m_lastUsedZoneId if it doesn't exist in the layout for targetScreen.
    void validateLastUsedZone(const QString& targetScreen);

    /// Find the nearest virtual screen by index proximity.
    /// Used when a stored virtual screen ID no longer exists in the current configuration.
    static QString findNearestVirtualScreen(const QStringList& vsIds, int oldIndex);

    /// Find a zone by UUID across all loaded layouts.
    /// Returns the zone and its parent layout, or {nullptr, nullptr} if not found.
    struct ZoneLookupResult
    {
        PhosphorZones::Zone* zone = nullptr;
        PhosphorZones::Layout* layout = nullptr;
    };
    ZoneLookupResult findZoneInAllLayouts(const QUuid& zoneUuid) const;

public:
    /// Resolve a screen ID to an effective screen ID, falling back to the physical
    /// screen ID if a virtual screen no longer exists in the current configuration.
    QString resolveEffectiveScreenId(const QString& screenId) const;

    /// Resolve zone geometry: combined geometry for multi-zone, single for single zone.
    /// Avoids repeating the (size>1) ? multiZoneGeometry : zoneGeometry ternary.
    QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const;

    QString findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId, int desktopFilter = 0) const;

    /// Sort zones by zone number ascending, with UUID tie-breaker for determinism
    /// when multiple zones share the same number.
    static void sortZonesByNumber(QVector<PhosphorZones::Zone*>& zones);

    /// Build a map from zone ID (toString) to 1-based position in sorted-by-zoneNumber order.
    static QHash<QString, int> buildZonePositionMap(PhosphorZones::Layout* layout);

    /// Build set of occupied zone UUIDs, optionally filtered by screen and virtual desktop.
    ///
    /// Uses Phosphor::Screens::ScreenIdentity::screensMatch() for format-agnostic screen comparison.
    ///
    /// @param desktopFilter When > 0, only counts assignments whose window desktop
    ///   matches (or is 0 = pinned/all-desktops). Pass the current virtual desktop
    ///   for snap-assist / empty-zone queries so windows parked on other desktops
    ///   do not make zones appear occupied — this mirrors the filtering done by
    ///   SnapAssistHandler::buildCandidates() in the KWin effect, keeping the
    ///   "occupied" and "candidate" definitions symmetric.
    QSet<QUuid> buildOccupiedZoneSet(const QString& screenFilter = QString(), int desktopFilter = 0) const;

    /**
     * @brief Current app class for a windowId, preferring the live registry.
     *
     * Equivalent to PhosphorIdentity::WindowId::extractAppId() when no registry is attached. With
     * a registry, returns the latest appId for the instance id — so snap rule
     * matching against a freshly-renamed window (Electron/CEF) sees the
     * current class.
     */
    QString currentAppIdFor(const QString& anyWindowId) const;

    /**
     * @brief Canonicalize for read-only callers (no map mutation).
     *
     * Delegates to the registry's canonicalizeForLookup when available.
     * Unit tests that don't attach a registry get a passthrough.
     */
    QString canonicalizeForLookup(const QString& rawWindowId) const;

private:
    // Dependencies
    PhosphorZones::LayoutRegistry* m_layoutManager;
    PhosphorZones::IZoneDetector* m_zoneDetector;
    PhosphorZones::SnapState* m_snapState = nullptr;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;
    // Shared registry for current-class queries and canonical key translation.
    // Not owned. Null in unit tests.
    WindowRegistry* m_windowRegistry = nullptr;
    Phosphor::Screens::ScreenManager* m_screenManager = nullptr;

    // Pre-tile geometries (unified snap + autotile): full windowId + appId at runtime,
    // appId only for session-restored entries. Converted on window close for persistence.
    // Each entry includes the screen name where the geometry was captured, enabling
    // cross-screen restore to detect coordinate mismatch and center on the target screen.
    //
    // Authoritative store. The kwin-effect keeps a per-screen cache
    // (`AutotileHandler::m_preAutotileGeometries`) populated on the first
    // autotile transition so it can restore frames without a D-Bus round-trip,
    // but this map is the source of truth: it persists across daemon restarts
    // via session save/load, drives session-restore geometry resolution, and
    // is the only copy keyed by appId for windows that haven't reopened yet.
    QHash<QString, PreTileGeometry> m_preTileGeometries;

    // Floating windows: full windowId at runtime, appId for session-restored entries
    // Converted from windowId to appId on window close for persistence
    QSet<QString> m_floatingWindows;

    // Subset of m_floatingWindows: windows whose float originated from autotile mode.
    // NOT persisted — on session restore all floats are treated as manual-mode.
    // Used to distinguish autotile floats from manual snapping-mode floats during
    // mode transitions (only autotile floats are cleared on autotile→snapping).
    QSet<QString> m_autotileFloatedWindows;

    // Saved snap-mode floating windows. When a snap-mode-floated window enters
    // autotile and its WTS floating is cleared, we save it here so it can be
    // restored when returning to snap mode. Mirrors the engine's m_savedFloatingWindows.
    QSet<QString> m_savedSnapFloatingWindows;

    // Session persistence: consumption queue (appId -> list of pending restores, consumed FIFO)
    QHash<QString, QList<PendingRestore>> m_pendingRestoreQueues;

    // Pre-float zone and screen (for unfloat restore to correct monitor).
    // Keyed by full windowId at runtime (to distinguish multiple instances of
    // the same app). Converted to appId on window close and session save.
    QHash<QString, QStringList> m_preFloatZoneAssignments;
    QHash<QString, QString> m_preFloatScreenAssignments;

    // Sticky window states
    QHash<QString, bool> m_windowStickyStates;

    QVector<ResnapEntry> m_resnapBuffer;

    // Delta-persistence dirty mask. Initial value DirtyAll forces the first
    // save after daemon startup to serialize every field. Cleared by
    // loadState() once in-memory state mirrors the disk file.
    DirtyMask m_dirtyMask = DirtyAll;

    // Note: No save timer - persistence handled by WindowTrackingAdaptor via KConfig
    // Service emits stateChanged() signal when state needs saving
};

} // namespace PlasmaZones
