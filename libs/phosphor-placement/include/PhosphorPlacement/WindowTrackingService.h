// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorplacement_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorPlacement/IGeometryResolver.h>
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorProtocol/ZoneTypes.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class LayoutRegistry;
class Zone;
}

namespace PhosphorSnapEngine {
class SnapState;
}

namespace PhosphorEngine {
class WindowRegistry;
}

namespace PhosphorWorkspaces {
class VirtualDesktopManager;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorPlacement {

/**
 * @brief Window-zone tracking service (business logic layer)
 *
 * This service encapsulates all window tracking business logic that was
 * previously in WindowTrackingAdaptor. Following the separation-of-concerns
 * principle, it handles:
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
class PHOSPHORPLACEMENT_EXPORT WindowTrackingService : public QObject, public PhosphorEngine::IWindowTrackingService
{
    Q_OBJECT

public:
    explicit WindowTrackingService(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorZones::IZoneDetector* zoneDetector,
                                   PhosphorScreens::ScreenManager* screenManager,
                                   PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                   IGeometryResolver* geometryResolver = nullptr, PlacementConfig config = {},
                                   QObject* parent = nullptr);

    /// The placement config installed at construction. Read-only after wiring:
    /// the constructor is the sole entry point. Consumers that observe
    /// `m_config` directly (no notify signal) rely on it being frozen
    /// post-construction; reassigning it later would silently desynchronise
    /// their cached values, which is why no setter is exposed.
    const PlacementConfig& config() const
    {
        return m_config;
    }

    QObject* asQObject() override
    {
        return this;
    }
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
    void setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
    {
        m_windowRegistry = registry;
    }

    void setSnapState(PhosphorSnapEngine::SnapState* state)
    {
        m_snapState = state;
    }

    /// The unified, engine-agnostic placement store (one WindowPlacement record
    /// per window). Both engines reach it via this service; the WTA persists it.
    PhosphorEngine::WindowPlacementStore& placementStore() override
    {
        return m_placementStore;
    }
    const PhosphorEngine::WindowPlacementStore& placementStore() const
    {
        return m_placementStore;
    }

    /**
     * @brief Predicate type for "is this snap-mode context active?".
     *
     * Receives the (screenId, virtualDesktop) tuple recorded for the closing
     * window. Returns true when the context is ACTIVE (i.e. tracking should
     * proceed); false when the context is disabled via the snapping-disabled
     * monitor / desktop lists.
     *
     * No activity parameter: SnapState does not track per-window activity, so
     * the placement library has nothing to thread through. If activity-mode
     * filtering grows here later, extend the signature then — until then the
     * dead slot would be misleading.
     *
     * The placement library is intentionally settings-agnostic (LGPL boundary),
     * so the daemon adaptor injects the predicate. When unset, the service
     * behaves as if every context is active — the historical default that unit
     * tests rely on.
     */
    using ShouldTrackPredicate = std::function<bool(const QString& screenId, int virtualDesktop)>;

    /**
     * @brief Inject a context-active predicate. See ShouldTrackPredicate.
     *
     * Used to suppress PendingRestore writes for windows that close on a
     * monitor/desktop the user has disabled snapping for. Without this, a
     * window closed on a disabled monitor still gets a snap restore
     * recorded against that monitor — when the same app reopens (or KWin
     * rehomes the window onto a surviving monitor after sleep), the snap
     * machinery picks the stale entry up and yanks the window back into a
     * zone the user told us to stay out of. See discussion #461 item 2.
     *
     * Ownership: the caller is responsible for keeping any captured state
     * (e.g. `this` pointers to a settings adaptor) valid for the lifetime
     * of this WindowTrackingService. If the captured object is destroyed
     * before WTS, clear the predicate first (`setShouldTrackPredicate({})`)
     * — otherwise a subsequent `windowClosed` call dereferences freed
     * memory.
     */
    void setShouldTrackPredicate(ShouldTrackPredicate predicate)
    {
        m_shouldTrackPredicate = std::move(predicate);
    }

    /**
     * @brief Wire the snap-mode placement engine.
     *
     * Float-back / free geometry is SHARED across modes and lives in the single
     * unified WindowPlacementStore (freeGeometryByScreen), not per-engine — so this
     * pointer is not the geometry store (validatedUnmanagedGeometry reads the record
     * directly). Retained for the engine reference used elsewhere (stale-window
     * pruning, the D-Bus facade's snapEngine() accessor).
     *
     * Must be set after construction. Not owned.
     */
    void setSnapEngine(PhosphorEngine::PlacementEngineBase* engine)
    {
        m_snapEngine = engine;
    }

    PhosphorEngine::PlacementEngineBase* snapEngine() const
    {
        return m_snapEngine.data();
    }

    /**
     * @brief Predicate: is the window currently in Autotile mode?
     *
     * Injected by the daemon (engine-/settings-agnostic LGPL boundary). The
     * single owning-engine signal used by the capture funnel and float
     * routing (see isWindowInAutotileMode). When unset, every window is
     * treated as snap-mode.
     */
    using AutotileModePredicate = std::function<bool(const QString& windowId)>;
    void setAutotileModePredicate(AutotileModePredicate predicate)
    {
        m_autotileModePredicate = std::move(predicate);
    }

    /**
     * @brief True if the window's CURRENT screen mode is autotile.
     * The single owning-engine signal — the same predicate that routes the float
     * resolver, the float writer, and validatedUnmanagedGeometry. Exposed so the
     * capture funnel (WindowTrackingAdaptor::captureWindowPlacement) picks the
     * owning engine the SAME way, instead of a divergent isWindowTracked() check
     * that disagrees mid-mode-flip. Returns false when the predicate is unwired
     * (snap-only tests / early init).
     */
    bool isWindowInAutotileMode(const QString& windowId) const
    {
        return m_autotileModePredicate && m_autotileModePredicate(windowId);
    }

    /**
     * @brief Predicate: is the window ACTIVELY TILED by the autotile engine
     * right now (engine-owned, non-floating)?
     *
     * Injected by the daemon (same LGPL-boundary pattern as
     * AutotileModePredicate). Distinct from the MODE predicate: a fresh
     * spawn on an autotile screen is in autotile mode but not yet tiled —
     * its frame is a genuine free geometry — while a tiled window's frame
     * IS the tile rect and must never be recorded as a float-back.
     * recordFreeGeometry uses this to refuse tiled frames the same way it
     * refuses snapped frames; it complements the effect-side capture guard,
     * which cannot help on an effect reload (the effect's border tracking
     * starts empty while this engine still holds the tiling state).
     */
    using AutotileTiledPredicate = std::function<bool(const QString& windowId)>;
    void setAutotileTiledPredicate(AutotileTiledPredicate predicate)
    {
        m_autotileTiledPredicate = std::move(predicate);
    }

    /// True if the autotile engine reports the window actively tiled.
    /// Returns false when the predicate is unwired (snap-only tests).
    bool isWindowAutotileTiled(const QString& windowId) const
    {
        return m_autotileTiledPredicate && m_autotileTiledPredicate(windowId);
    }

    /**
     * @brief Accessor for consumers that need direct access (effect, adaptor).
     */
    PhosphorEngine::WindowRegistry* windowRegistry() const
    {
        return m_windowRegistry;
    }

    PhosphorScreens::ScreenManager* screenManager() const override
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
                            int virtualDesktop) override;

    /**
     * @brief Assign a window to multiple zones (multi-zone snap)
     * @param windowId Full window ID
     * @param zoneIds List of zone UUID strings (first is primary)
     * @param screenId Screen where the zones are located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             int virtualDesktop) override;

    /**
     * @brief Remove window from its assigned zone
     * @param windowId Full window ID
     */
    void unassignWindow(const QString& windowId) override;

    /**
     * @brief Get the primary zone ID for a window
     * @param windowId Full window ID
     * @return PhosphorZones::Zone ID or empty string if not assigned
     */
    QString zoneForWindow(const QString& windowId) const override;

    /**
     * @brief Get all zone IDs for a window (multi-zone support)
     * @param windowId Full window ID
     * @return List of zone IDs (empty if not assigned)
     */
    QStringList zonesForWindow(const QString& windowId) const override;

    /// The screen a window is assigned to, or empty when it has none. Point
    /// accessor over the screen-assignment map that canonicalizes @p windowId to
    /// the first-seen composite (via SnapState), so external callers resolve a
    /// window even after the effect-restart-after-class-mutation skew rather than
    /// reading the raw whole-map getter with a stale composite (issue #628).
    QString screenForWindow(const QString& windowId) const override;

    /// Same, but returns @p defaultScreen when the window has no screen
    /// assignment — the canonicalizing replacement for
    /// `screenAssignments().value(windowId, defaultScreen)`.
    QString screenForWindow(const QString& windowId, const QString& defaultScreen) const override;

    /**
     * @brief Get all windows in a specific zone
     * @param zoneId PhosphorZones::Zone UUID string
     * @return List of window IDs
     */
    QStringList windowsInZone(const QString& zoneId) const override;

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
    bool isWindowSnapped(const QString& windowId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Geometry Validation Utility
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Validate and adjust a saved geometry for screen-aware restore.
     *
     * Pure utility — reads no internal state. Given a saved geometry and the
     * screen it was captured on, returns the geometry adjusted for the
     * @p currentScreenName. Cross-screen mismatches are resolved by centering
     * on the target screen (size clamped to fit). On-screen geometries are
     * returned as-is; off-screen geometries are nudged to the nearest screen.
     *
     * @param geo             Saved geometry (e.g. a window's recorded free geometry)
     * @param savedScreen     Screen connector name at capture time (may be empty)
     * @param currentScreenName Screen where the window currently is
     * @return Adjusted geometry, or nullopt if @p geo is invalid
     */
    std::optional<QRect> validateGeometryForScreen(const QRect& geo, const QString& savedScreen,
                                                   const QString& currentScreenName) const;

    /**
     * @brief Look up a window's free (unmanaged) geometry from the unified
     *        WindowPlacementStore, with appId fallback, and validate it.
     *
     * Combines the windowId lookup, appId fallback, and cross-screen validation
     * into a single call. Returns nullopt if no geometry is recorded.
     *
     * @param windowId        Full window ID
     * @param screenId        Screen where the window currently is (for cross-screen adjustment)
     * @param exactOnly       If true, skip the appId fallback (strict per-instance lookup)
     */
    std::optional<QRect> validatedUnmanagedGeometry(const QString& windowId, const QString& screenId,
                                                    bool exactOnly = false) const override;

    /// Write the window's shared free/float geometry into the unified record (the
    /// single float-back store). See IWindowTrackingService::recordFreeGeometry.
    void recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                            bool overwrite) override;

    /// Authoritative float-back capture for a window closing on @p screenId.
    /// Unlike recordFreeGeometry (a geometry-only partial that deliberately leaves
    /// the managed-context screen untouched), this records the float geometry AND
    /// updates the record's managed `screenId` to @p screenId — carrying an engine
    /// slot so the store merge adopts the new screen. Used by the close-capture
    /// fallback when a cross-screen move has orphaned the window from both engines,
    /// so the only authoritative source of its final screen is KWin (passed down
    /// from the effect). The existing record's per-engine slots and desktop/activity
    /// are preserved; only the screen and this screen's free geometry change.
    void recordFloatingClose(const QString& windowId, const QString& screenId, const QRect& geometry);

    /// Clear a window's shared free/float geometry from the record. See
    /// IWindowTrackingService::clearFreeGeometry.
    void clearFreeGeometry(const QString& windowId) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating Window State
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Per-engine float resolver/writer.
     *
     * Float state is genuinely per-engine: a window floated in autotile mode is
     * NOT floating in snapping mode and vice versa. The authoritative store lives
     * in each engine (SnapState::isFloating / TilingState::isFloating), keyed by
     * the screen's current mode. The placement library is intentionally engine-
     * and settings-agnostic (LGPL boundary), so the daemon injects a resolver
     * (reader) and writer that route to the engine owning the window's CURRENT
     * screen mode.
     *
     * When unset (unit tests / early init before the engines are wired), the
     * service falls back to the legacy shared `m_floatingWindows` set so existing
     * single-engine tests keep their historical behaviour.
     */
    using EngineFloatResolver = std::function<bool(const QString& windowId)>;
    using EngineFloatWriter = std::function<void(const QString& windowId, bool floating)>;
    using EngineFloatLister = std::function<QStringList()>;

    void setEngineFloatResolver(EngineFloatResolver resolver)
    {
        m_engineFloatResolver = std::move(resolver);
    }
    void setEngineFloatWriter(EngineFloatWriter writer)
    {
        m_engineFloatWriter = std::move(writer);
    }
    /// Aggregates both engines' floating windows for the engine-agnostic
    /// floatingWindows() enumeration. See setEngineFloatResolver rationale.
    void setEngineFloatLister(EngineFloatLister lister)
    {
        m_engineFloatLister = std::move(lister);
    }

    /**
     * @brief Check if a window is floating (excluded from snapping)
     *
     * Delegates to the per-engine resolver when wired; otherwise falls back to
     * the legacy shared floating set.
     */
    bool isWindowFloating(const QString& windowId) const override;

    /**
     * @brief Set window floating state
     *
     * Routes to the engine owning the window's current screen mode via the
     * injected writer when wired; otherwise updates the legacy shared set and
     * the snap state directly.
     *
     * @param windowId Full window ID
     * @param floating true to float, false to unfloat
     */
    void setWindowFloating(const QString& windowId, bool floating) override;

    /**
     * @brief Get all floating window IDs
     */
    QStringList floatingWindows() const;

    /**
     * @brief Unsnap window for floating (saves zone for later restore)
     * @param windowId Full window ID
     */
    void unsnapForFloat(const QString& windowId) override;

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
    QStringList preFloatZones(const QString& windowId) const override;

    /**
     * @brief Get the screen name where the window was snapped before floating
     * @param windowId Full window ID
     * @return Screen name or empty string if unknown
     */
    QString preFloatScreen(const QString& windowId) const override;

    /**
     * @brief Clear pre-float zone after restore (both windowId and appId keys)
     */
    void clearPreFloatZone(const QString& windowId) override;

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
    bool clearFloatingForSnap(const QString& windowId) override;

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
    bool isWindowSticky(const QString& windowId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Snap Logic
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Record that a window class was user-snapped
     * @param windowId Full window ID to extract class from
     * @param wasUserInitiated true if user-initiated snap
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated) override;

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
     * @brief Screen name companion of the last-used-zone tracking.
     *
     * Returned alongside `lastUsedZoneId` so persistence-layer reloads
     * can round-trip the companion fields without blanking them.
     */
    QString lastUsedScreenName() const;

    /**
     * @brief Virtual-desktop companion of the last-used-zone tracking.
     */
    int lastUsedDesktop() const;

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
                            int virtualDesktop) override;

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
    bool clearAutoSnapped(const QString& windowId) override;

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
    bool consumePendingAssignment(const QString& windowId) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Helpers
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Find the first empty zone in the layout for a screen
     * @param screenId Screen to find layout for (empty = active layout)
     * @return PhosphorZones::Zone ID or empty string if all occupied
     */
    QString findEmptyZone(const QString& screenId = QString()) const override;

    /**
     * @brief Get typed list of all empty zones for Snap Assist continuation
     * @param screenId Screen to find layout for (e.g. DP-1)
     * @return PhosphorProtocol::EmptyZoneList of empty zone entries with overlay-local geometry
     */
    PhosphorProtocol::EmptyZoneList getEmptyZones(const QString& screenId) const;

    /**
     * @brief Get geometry for a zone on a specific screen
     * @param zoneId PhosphorZones::Zone UUID string
     * @param screenId Screen identifier (empty = primary)
     * @return PhosphorZones::Zone geometry in pixels, or invalid QRect if not found
     */
    QRect zoneGeometry(const QString& zoneId, const QString& screenId = QString()) const override;

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
     * @param desktopFilter When > 0, only include windows whose virtualDesktop
     *        matches this value (or are sticky/unknown with virtualDesktop==0).
     *        Restricts resnap to a single virtual desktop so per-desktop
     *        layout changes don't reposition windows on other desktops.
     */
    void populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens = {},
                                           const QSet<QString>& includeScreens = {}, int desktopFilter = 0);

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
     * in SnapState's screen assignments (m_snapState->screenAssignments()). When
     * virtual screens are active, all per-screen
     * lookups use virtual IDs, so these windows become invisible to zone occupancy
     * checks, snap assist, float/unfloat, etc.
     *
     * This method iterates all screen assignments and, for any window whose screen
     * matches the given physical screen, determines which virtual screen the window's
     * zone falls within and updates the assignment accordingly.
     *
     * Also migrates pre-float screen assignments in SnapState.
     *
     * @param physicalScreenId The physical screen being subdivided
     * @param virtualScreenIds Virtual screen IDs for the physical screen
     * @param mgr PhosphorScreens::ScreenManager for geometry lookups
     */
    void migrateScreenAssignmentsToVirtual(const QString& physicalScreenId, const QStringList& virtualScreenIds,
                                           PhosphorScreens::ScreenManager* mgr);

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

    /**
     * @brief Find physical screens whose state still references virtual ids,
     *        excluding screens the caller knows are still subdivided.
     *
     * Sweeps every state store that holds a screen id (active screen
     * assignments, pre-float assignments, pending restore queues, pre-tile
     * geometry on the snap engine) and returns the set of physical screen ids
     * for which any stored value is still a "physId/vs:N" form whose physId
     * is NOT in @p subdividedPhysicalIds.
     *
     * Pair with @ref migrateScreenAssignmentsFromVirtual on each returned id
     * to clean up state left over from a config change applied while the
     * daemon was offline.
     *
     * Owning the scan here (rather than the daemon enumerating each store)
     * keeps state-shape knowledge inside WTS — adding a new screen-id-bearing
     * store updates this method, not every caller.
     *
     * @param subdividedPhysicalIds Physical ids that legitimately have
     *        virtual subdivisions in the current config (these are kept).
     * @return Physical ids that should have @ref migrateScreenAssignmentsFromVirtual
     *         applied. Empty when state is consistent with the policy.
     */
    QSet<QString> physicalScreensWithStaleVirtualAssignments(const QSet<QString>& subdividedPhysicalIds) const;

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
     * Sourced from the unified WindowPlacementStore's snapped records; for a
     * multi-instance appId the lowest-sequence (FIFO-head) record is chosen
     * deterministically. Entries whose saved screen is currently in autotile
     * mode are skipped: the effect cache is a snap-mode-only fast path, and
     * autotile on the saved screen will own placement. Validates desktop
     * context so the cache never contains geometries the async resolver rejects.
     */
    QHash<QString, PendingRestoreTarget> pendingRestoreGeometries() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Clean up all tracking data for a closed window
     * @param windowId Full window ID
     * @param kind Structural kind of the closing window. When a PendingRestore
     *             entry is enqueued for this close, the kind is stamped onto
     *             the entry so the consume path can refuse to assign it to a
     *             window of a different kind on reopen. Defaults to
     *             `WindowKind::Unknown` (the pre-fix behaviour: no kind gate).
     */
    void windowClosed(const QString& windowId, PhosphorEngine::WindowKind kind = PhosphorEngine::WindowKind::Unknown);

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
    const QHash<QString, QStringList>& zoneAssignments() const override;

    /// Live snap zones if present, else the durable placement-record snap slot.
    /// See IWindowTrackingService::recordedSnapZones.
    QStringList recordedSnapZones(const QString& windowId) const override;

    /**
     * @brief Get all screen assignments for persistence
     */
    const QHash<QString, QString>& screenAssignments() const override;

    /**
     * @brief Get all desktop assignments for persistence
     */
    const QHash<QString, int>& desktopAssignments() const;

    using PendingRestore = PhosphorEngine::PendingRestore;
    using ResnapEntry = PhosphorEngine::ResnapEntry;

    /**
     * @brief Get pending restore queues (consumption queue: appId -> list of pending restores)
     */
    const QHash<QString, QList<PendingRestore>>& pendingRestoreQueues() const override
    {
        return m_pendingRestoreQueues;
    }

    QVector<ResnapEntry> takeResnapBuffer() override
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
     *
     * Delegates to SnapState (authoritative store).
     */
    const QHash<QString, QStringList>& preFloatZoneAssignments() const;
    const QHash<QString, QString>& preFloatScreenAssignments() const;

    /**
     * @brief Set pre-float zone assignments (loaded from KConfig by adaptor)
     *
     * Delegates to SnapState (authoritative store).
     */
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments);
    void setPreFloatScreenAssignments(const QHash<QString, QString>& assignments);

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
    // NOTE: several bits below (DirtyZoneAssignments, DirtyPendingRestores,
    // DirtyPreTileGeometries, DirtyPreFloatZones, DirtyPreFloatScreens,
    // DirtyAutotileOrders, DirtyAutotilePending) no longer have a dedicated save
    // block — their legacy keys were collapsed into DirtyWindowPlacements. The
    // runtime mutators that still OR them in are retained because the act of
    // marking ANY bit schedules a save, and saveState()'s refreshOpenWindowPlacements()
    // re-derives the affected per-window state into the placement record. They are
    // therefore "schedule a save" triggers, not independent persisted fields; only
    // DirtyActiveLayoutId / DirtyLastUsedZone / DirtyUserSnapped / DirtyWindowPlacements
    // map to their own on-disk key.
    enum DirtyField : uint32_t {
        DirtyNone = 0,
        DirtyActiveLayoutId = 1u << 0,
        DirtyZoneAssignments = 1u << 1, // legacy save-trigger → DirtyWindowPlacements
        DirtyPendingRestores = 1u << 2, // legacy save-trigger → DirtyWindowPlacements
        DirtyPreTileGeometries = 1u << 3, // legacy save-trigger → DirtyWindowPlacements
        DirtyLastUsedZone = 1u << 4,
        DirtyPreFloatZones = 1u << 5, // legacy save-trigger → DirtyWindowPlacements
        DirtyPreFloatScreens = 1u << 6, // legacy save-trigger → DirtyWindowPlacements
        DirtyUserSnapped = 1u << 7,
        DirtyAutotileOrders = 1u << 8, // legacy save-trigger → DirtyWindowPlacements
        DirtyAutotilePending = 1u << 9, // legacy save-trigger → DirtyWindowPlacements
        // bit 10 reserved (was DirtyFloatRestores, removed with the FloatRestoreQueues key)
        DirtyWindowPlacements = 1u << 11, ///< unified WindowPlacementStore (sole per-window restore state)
        DirtyAll = 0xFFFu, // covers bits 0-11 incl. the reserved bit 10
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
    // WARNING: AutotileEngine connects to this signal via string-based SIGNAL/SLOT
    // (it holds IWindowTrackingService*, not WindowTrackingService*, so PMF connect
    // is unavailable). Renaming this signal will silently break autotile zone tracking.
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

    /// windowId-then-appId fallback lookup against SnapState.
    template<typename Func>
    auto preFloatLookup(const QString& windowId, Func&& getter) const -> decltype(getter(windowId));

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
    QString resolveEffectiveScreenId(const QString& screenId) const override;

    /// Resolve zone geometry: combined geometry for multi-zone, single for single zone.
    /// Avoids repeating the (size>1) ? multiZoneGeometry : zoneGeometry ternary.
    QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const override;

    QString findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                  int desktopFilter = 0) const override;

    // Zone sort / position-map helpers now live in PhosphorZones::LayoutUtils.
    // Call PhosphorZones::LayoutUtils::sortZonesByNumber / buildZonePositionMap directly.

    /// Build set of occupied zone UUIDs, optionally filtered by screen and virtual desktop.
    ///
    /// Uses PhosphorScreens::ScreenIdentity::screensMatch() for format-agnostic screen comparison.
    ///
    /// @param desktopFilter When > 0, only counts assignments whose window desktop
    ///   matches (or is 0 = pinned/all-desktops). Pass the current virtual desktop
    ///   for snap-assist / empty-zone queries so windows parked on other desktops
    ///   do not make zones appear occupied — this mirrors the filtering done by
    ///   SnapAssistHandler::buildCandidates() in the KWin effect, keeping the
    ///   "occupied" and "candidate" definitions symmetric.
    QSet<QUuid> buildOccupiedZoneSet(const QString& screenFilter = QString(), int desktopFilter = 0) const override;

    /**
     * @brief Current app class for a windowId, preferring the live registry.
     *
     * Equivalent to PhosphorIdentity::WindowId::extractAppId() when no registry is attached. With
     * a registry, returns the latest appId for the instance id — so snap rule
     * matching against a freshly-renamed window (Electron/CEF) sees the
     * current class.
     */
    QString currentAppIdFor(const QString& anyWindowId) const override;

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
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;
    PhosphorEngine::WindowPlacementStore m_placementStore;
    IGeometryResolver* m_geometryResolver;
    PlacementConfig m_config;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    // Shared registry for current-class queries and canonical key translation.
    // Not owned. Null in unit tests.
    PhosphorEngine::WindowRegistry* m_windowRegistry = nullptr;
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    AutotileModePredicate m_autotileModePredicate{};
    AutotileTiledPredicate m_autotileTiledPredicate{};

    // Floating windows: full windowId at runtime, appId for session-restored entries
    // Converted from windowId to appId on window close for persistence.
    //
    // LEGACY FALLBACK ONLY: used when no per-engine resolver/writer is wired
    // (unit tests / early init). In production the daemon injects
    // m_engineFloatResolver / m_engineFloatWriter and this set is never read or
    // written by isWindowFloating / setWindowFloating.
    QSet<QString> m_floatingWindows;

    // Daemon-injected per-engine float reader/writer/lister. See setEngineFloatResolver.
    EngineFloatResolver m_engineFloatResolver{};
    EngineFloatWriter m_engineFloatWriter{};
    EngineFloatLister m_engineFloatLister{};

    // Session persistence: consumption queue (appId -> list of pending restores, consumed FIFO)
    QHash<QString, QList<PendingRestore>> m_pendingRestoreQueues;

    // Optional daemon-injected gate consulted before recording a PendingRestore
    // on windowClosed. When unset (e.g. unit tests), every context is treated
    // as active and the historical write-everything behavior is preserved.
    ShouldTrackPredicate m_shouldTrackPredicate{};

    // Pre-float zone and screen state is owned by SnapState (authoritative store).
    // WTS preFloat getter methods add appId-fallback queries for session-restored
    // entries keyed by appId. SnapState itself uses windowId-only keys.

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

} // namespace PhosphorPlacement
