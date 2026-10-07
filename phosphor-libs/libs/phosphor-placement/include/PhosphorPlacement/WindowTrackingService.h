// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): WindowTrackingService is the engine-
// agnostic facade every placement engine and the daemon share, and this is an
// INSTALLED public header — splitting the class is an API break for the
// third-party consumers the LGPL boundary exists to serve. The implementation
// is already split by concern across src/*.cpp (snap, resnap, navigation,
// virtualscreenmigration, lifecycle), and the member ordering here encodes
// which of those owns what. Same rationale as
// PhosphorTileEngine/AutotileEngine.h.

#pragma once

#include <phosphorplacement_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorPlacement/IGeometryResolver.h>
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorPlacement/SnapStateResolver.h>
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
class Layout;
class LayoutRegistry;
class Zone;
}

namespace PhosphorSnapEngine {
class SnapState;
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
                                   PhosphorScreens::ScreenManager* screenManager,
                                   PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                   IGeometryResolver* geometryResolver = nullptr, PlacementConfig config = {},
                                   QObject* parent = nullptr);

    /// The placement config. The adaptor keeps it current with the settings
    /// (F327); every reader reads it at the time of use, none caches it.
    const PlacementConfig& config() const
    {
        return m_config;
    }
    void setPlacementConfig(const PlacementConfig& config)
    {
        m_config = config;
    }

    QObject* asQObject() override;
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
    void setWindowRegistry(PhosphorEngine::WindowRegistry* registry);

    /// The injectable SnapState-resolver seam. Defined in its own header so
    /// consumers can name the type without pulling in the whole service. The
    /// alias keeps the historical `WindowTrackingService::SnapStateResolver`
    /// spelling valid for existing call sites.
    using SnapStateResolver = PhosphorPlacement::SnapStateResolver;

    void setSnapStateResolver(SnapStateResolver resolver);

    /// Convenience wiring for a SINGLE snap store (unit tests / the collapsed
    /// Phase-2 store). Builds a resolver whose every arm routes to @p state, so the
    /// facade behaves exactly as the former single-SnapState pointer. Passing
    /// nullptr clears the resolver (the "no SnapState wired" no-op path).
    void setSnapState(PhosphorSnapEngine::SnapState* state);

    /// The unified, engine-agnostic placement store (one WindowPlacement record
    /// per window). Every engine reaches it via this service; the WTA persists it.
    PhosphorEngine::WindowPlacementStore& placementStore() override;
    const PhosphorEngine::WindowPlacementStore& placementStore() const;

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
    void setSnapEngine(PhosphorEngine::PlacementEngineBase* engine);

    PhosphorEngine::PlacementEngineBase* snapEngine() const;

    /**
     * @brief Predicate: is the window currently in Autotile mode?
     *
     * Injected by the daemon (engine-/settings-agnostic LGPL boundary). The
     * single owning-engine signal used by the capture funnel and float
     * routing (see isWindowInAutotileMode). When unset, every window is
     * treated as snap-mode.
     */
    using AutotileModePredicate = std::function<bool(const QString& windowId)>;
    void setAutotileModePredicate(AutotileModePredicate predicate);

    /// True if the window's CURRENT screen mode is autotile: the predicate the float
    /// resolver, the float writer, validatedUnmanagedGeometry and the capture funnel
    /// share. False when unwired (snap-only tests / early init).
    bool isWindowInAutotileMode(const QString& windowId) const;

    /**
     * @brief Predicate: is the window ACTIVELY TILED by a tiling-family engine
     * (autotile / scrolling) in the context in view? Injected by the daemon; the
     * wiring ORs both engines' isWindowTiled where each holds the window in view.
     * Distinct from the MODE predicate: a fresh spawn on a tiling screen is in
     * that mode but not tiled, and its frame is a genuine free geometry.
     */
    using EngineTiledPredicate = std::function<bool(const QString& windowId)>;
    void setEngineTiledPredicate(EngineTiledPredicate predicate);

    /// True if a tiling-family engine (autotile / scrolling) reports the window actively tiled.
    /// Returns false when the predicate is unwired (snap-only tests).
    bool isWindowEngineTiled(const QString& windowId) const;

    /**
     * @brief Resolver: which engine id owns @p windowId's mode on @p screenId?
     *
     * Injected by the daemon (engine-/settings-agnostic LGPL boundary), same
     * pattern as AutotileModePredicate but answering with the OWNING engine's
     * WindowPlacement engine id ("snap" / "autotile" / "scrolling") for an
     * explicit screen. Used wherever a slot must be synthesized for a window
     * no engine captured (recordFloatingClose, the minimize preserve): the
     * reopen accepts are keyed strictly per engine slot, so a synthesized
     * float slot filed under the wrong engine is invisible to the engine that
     * will actually field the reopen — the float-is-per-mode invariant read
     * from the store side.
     */
    using ModeEngineIdResolver = std::function<QString(const QString& windowId, const QString& screenId)>;
    void setModeEngineIdResolver(ModeEngineIdResolver resolver);

    /// The owning engine id for @p windowId on @p screenId per the resolver;
    /// snap's engine id when the resolver is unwired (snap-only tests) or
    /// answers empty. The production resolver's mode switch is total and
    /// never answers empty, so the empty arm is pure defence — but it is a
    /// REAL branch: a future resolver answering empty on a tiling-mode
    /// screen would file the synthesized close slot under snap, invisibly to
    /// that engine's reopen accept.
    QString owningModeEngineId(const QString& windowId, const QString& screenId) const;

    /// The reopen contract's claim (the store's screen-aware claimForOpen) for
    /// @p windowId opening on @p openingScreenId under @p openingEngineId: its
    /// own record, else the newest sibling record that engine can restore on
    /// this output (managed and in its mode, or floating with a free geometry
    /// on this very screen). An empty engine id applies no engine predicate.
    std::optional<PhosphorEngine::WindowPlacement>
    claimPlacementForOpen(const QString& windowId, const QString& openingScreenId, const QString& openingEngineId);

    /**
     * @brief Downgrade managed slots recorded against a different screen.
     *
     * When @p recordedScreenId names a screen that does not match
     * @p closeScreenId, every snapped/tiled slot in @p placement is flipped to
     * plain floating with its zone ids and order cleared: a managed slot
     * references THAT screen's zones / tile order, and restoring it under the
     * new screen would land the window in another screen's slots. Same-screen
     * (or unscreened) records pass through untouched. Shared by
     * recordFloatingClose and the adaptor's minimize preserve so the two
     * close-shaped writers cannot drift.
     */
    static void downgradeMismatchedManagedSlots(PhosphorEngine::WindowPlacement& placement,
                                                const QString& recordedScreenId, const QString& closeScreenId);

    /**
     * @brief Accessor for consumers that need direct access (effect, adaptor).
     */
    PhosphorEngine::WindowRegistry* windowRegistry() const;

    PhosphorScreens::ScreenManager* screenManager() const override;
    QRect screenAvailableGeometry(const QString& screenId) const override;

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
    /// assignment — the canonicalizing with-default variant.
    QString screenForWindow(const QString& windowId, const QString& defaultScreen) const override;

    /**
     * @brief Every window holding a zone in any context, each once
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
     *        WindowPlacementStore and validate it.
     *
     * PER-WINDOW. It does not fall back to a same-app sibling's record: that
     * was removed in discussion #1028, where a live window with no record of
     * its own inherited a dead instance's absolute coordinates and was moved to
     * whatever monitor that instance last occupied. A window with nothing on
     * record gets nullopt and should be left where it is.
     *
     * SCREEN-LOCAL when the caller names a screen: it will not search other
     * screens for one. When the caller passes an EMPTY screenId it resolves to
     * the record's OWN screen instead, which can therefore answer a rect for a
     * monitor the window has since left. That is deliberate — the alternative
     * is answering nothing at all for every window snap does not track — but a
     * caller that knows the window's live screen should pass it rather than
     * relying on the record.
     *
     * The rect is also checked against the screen it is filed under, because
     * the key alone cannot be trusted — see geometryBelongsToScreen, which the
     * other readers of this store share.
     *
     * @param windowId        Full window ID
     * @param screenId        Screen to resolve against; empty resolves to the
     *                        record's own screen
     */
    std::optional<QRect> validatedUnmanagedGeometry(const QString& windowId, const QString& screenId) const override;

    /// @copydoc PhosphorEngine::IWindowTrackingService::geometryBelongsToScreen
    ///
    /// Forwards to the private geometryOverlapsScreen, which is where the
    /// semantics (physical-output resolution, size-clamped thresholds,
    /// fail-open) are documented.
    bool geometryBelongsToScreen(const QRect& geometry, const QString& screenId) const override
    {
        return geometryOverlapsScreen(geometry, screenId);
    }

    /// Float-back writes. Three writers fill WindowPlacement::freeGeometryByScreen:
    /// recordFreeGeometry, recordFloatingClose and the adaptor's
    /// captureWindowPlacement. Each refuses (P) a rect off its screen key and (M) a
    /// managed frame (isManagedFrame); recordFreeGeometry and the capture also
    /// refuse (O) a window in a zone in view or tiled in view. (S) A minimized or
    /// output-filling window's frame is refused where a frame is SAMPLED (the
    /// capture, the close, the snap pre-snap capture), never for an explicit rect.
    void recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                            bool overwrite) override;
    /// The close-time capture of an engine-orphaned window: records the float
    /// geometry AND adopts @p screenId as the record's managed screen (KWin's word
    /// on where it closed), keeping the record's other slots and context.
    void recordFloatingClose(const QString& windowId, const QString& screenId, const QRect& geometry);
    /// (M): @p frame equals a rect a snap membership of the window resolves to (its
    /// zones and pre-float zones, each on its store's screen) or one the injected
    /// predicate names (the adaptor's settled frame, the tiling engines' last
    /// managed rect). Exact compare, never size-only.
    using ManagedFramePredicate = std::function<bool(const QString& windowId, const QRect& frame)>;
    void setManagedFramePredicate(ManagedFramePredicate predicate);
    bool isManagedFrame(const QString& windowId, const QRect& frame) const;
    /// (O): the window is snapped, not floating, in the store its screen shows, on a screen snap runs.
    bool occupiesZoneInView(const QString& windowId) const;

    /// Clear a window's shared free/float geometry from the record. See
    /// IWindowTrackingService::clearFreeGeometry.
    void clearFreeGeometry(const QString& windowId) override;
    void clearFreeGeometry(const QString& windowId, const QString& screenId) override;

    /// Downgrade @p engineId's slot to released on @p windowId's record(s) and
    /// mark the placements dirty — the dirty-marking wrapper the tiling
    /// engines' handoffRelease calls instead of reaching into the store, so
    /// the release survives to disk. See
    /// WindowPlacementStore::releaseEngineSlot for what the downgrade means
    /// and why it is not a removal.
    void releaseEngineSlot(const QString& windowId, const QString& engineId) override;
    /// Dirty-marking wrapper for WindowPlacementStore::forgetDesktopZones.
    void forgetDesktopZones(const QString& windowId, const QString& engineId, int desktop) override;
    /// Dirty-marking wrapper for WindowPlacementStore::renumberDesktopZones
    /// that also shifts the buffered resnap rows the same way: a row on the
    /// removed desktop maps to 0, the all-desktops sentinel, and a row past it
    /// moves down one.
    void renumberDesktopZones(int removedDesktop) override;

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

    void setEngineFloatResolver(EngineFloatResolver resolver);
    void setEngineFloatWriter(EngineFloatWriter writer);
    /// Aggregates every engine's floating windows for the engine-agnostic
    /// floatingWindows() enumeration. See setEngineFloatResolver rationale.
    void setEngineFloatLister(EngineFloatLister lister);

    /**
     * @brief Check if a window is floating (excluded from snapping)
     *
     * Delegates to the per-engine resolver when wired; otherwise falls back to
     * the legacy shared floating set.
     */
    bool isWindowFloating(const QString& windowId) const override;

    /**
     * @brief Suspension-float classification (minimize-floats).
     *
     * A float applied while the compositor reported the window MINIMIZED is a
     * suspension, not placement intent, and the classification must OUTLIVE
     * the live minimize bit: on the unminimize edge the effect's metadata push
     * flips isMinimized to false immediately, while the unfloat only commits
     * after the animation grace — a placement capture landing inside that
     * window would otherwise persist the suspension float as a genuine user
     * float. Marked by the adaptor at the float WRITE (where minimize state is
     * still fresh), and cleared BY THE ADAPTOR on unfloat and on windowClosed
     * (WindowTrackingService::windowClosed itself does not touch the set — a
     * direct WTS caller must clear it explicitly). The prune backstop sweeps
     * it for windows that die without a close signal.
     */
    bool isSuspensionFloat(const QString& windowId) const override;
    void markSuspensionFloat(const QString& windowId);
    void clearSuspensionFloat(const QString& windowId) override;

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
     *
     * Always clears the appId alias alongside the windowId key: the pre-float
     * readers fall back to the alias, so a windowId-only clear (the former
     * clearPreFloatZoneForWindow) still resolved the stale zone for the very
     * window it targeted. The alias is a single last-writer slot per app, so
     * "protecting sibling data" by keeping it only preserved whichever
     * instance unsnapped last.
     */
    void clearPreFloatZone(const QString& windowId) override;

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
     * @param screenId Screen identifier. With a screen manager wired this must
     *        name a resolvable output: an empty or unknown id answers an invalid
     *        rect rather than measuring the zone against the primary monitor
     *        (see frameScreenIdResolves in navigation.cpp). Without a screen
     *        manager there is nothing to resolve against and the primary screen
     *        is used.
     * @return PhosphorZones::Zone geometry in pixels, or invalid QRect if not found
     */
    QRect zoneGeometry(const QString& zoneId, const QString& screenId) const override;

    /**
     * @brief Get combined geometry for multiple zones on a specific screen
     * @param zoneIds List of zone UUID strings
     * @param screenId Screen identifier, resolved exactly as zoneGeometry() does.
     * @return Union of all zone geometries, or invalid QRect if none found
     */
    QRect multiZoneGeometry(const QStringList& zoneIds, const QString& screenId) const;

    /**
     * @brief Populate the resnap buffer: each window's primary zone position,
     * read from a map over every loaded layout. Every layout switch builds its
     * buffer here, and the result replaces the buffer even when it is empty.
     *
     * @param excludeScreens Screens to skip (e.g. autotile screens handled separately)
     * @param includeScreens When non-empty, only process windows on these
     *        screens. Only memberships of the activity in view are taken.
     * @param desktopFilter When > 0, only windows on their screen's current
     *        desktop (this value when the screen's is unknown); sticky windows
     *        (virtualDesktop==0) always pass.
     */
    void populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens = {},
                                           const QSet<QString>& includeScreens = {}, int desktopFilter = 0);

    /**
     * @brief Clear the resnap buffer
     *
     * Called when virtual screen configuration changes to prevent stale
     * resnap data from referencing old screen IDs.
     */
    void clearResnapBuffer();

    /// The windows snapped on @p screenId in the context in view, sorted by
    /// their primary zone's number in the screen's layout. Seeds the autotile
    /// order on a switch to tiling.
    QStringList buildZoneOrderedWindowList(const QString& screenId) const;

    /// Replace the resnap buffer with the windows in view on a snapping screen
    /// whose primary zone @p layout (being removed, still alive) holds, by
    /// zone position, so a resnap moves them to the layout their context runs.
    void bufferWindowsOfRemovedLayout(PhosphorZones::Layout* layout);

    // ═══════════════════════════════════════════════════════════════════════════
    // Virtual Screen Migration
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Migrate window screen assignments from physical to virtual screen IDs
     *
     * Windows snapped before virtual screens were configured have physical screen IDs
     * in the snap stores' screen assignments. When
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
     * assignments, pre-float assignments, placement records, pre-tile
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

    /// windowId -> its zone rect in the context in view (else its primary's), for
    /// the work-area re-apply. Empty while keepWindowsInZonesOnResolutionChange is off.
    QHash<QString, QRect> updatedWindowGeometries() const;

    /// A pre-computed snap restore target for the effect's instant-restore
    /// cache: zone geometry, the saved screen (so the effect only applies it on
    /// that screen's output) and the record's window id (an entry for a window
    /// the effect can still see is dropped).
    struct PendingRestoreTarget
    {
        QRect geometry;
        QString screenId;
        QString windowId;
    };

    /// appId -> list of {geometry, screenId, windowId}, newest first: the
    /// snapped records minus still-open windows' (#1106). A best-effort
    /// anti-flash hint the async resolve corrects; the effect applies the
    /// newest entry on the opener's own output. Skips saved screens in autotile
    /// mode, zones the record's context no longer runs (#1104) and desktop
    /// contexts the resolver would reject.
    QHash<QString, QList<PendingRestoreTarget>> pendingRestoreGeometries() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Clean up all tracking data for a closed window
     * @param windowId Full window ID
     * @param kind Structural kind of the closing window. Unused: it gated the
     *             pending-restore queue this close used to write, which was
     *             removed. Kept so the D-Bus close path keeps its signature.
     */
    void windowClosed(const QString& windowId, PhosphorEngine::WindowKind kind = PhosphorEngine::WindowKind::Unknown);

    /// @c unsnapped: windows left in no zone in view (windowId, targetScreenId).
    /// @c narrowed: spans that kept some zones, as a re-statement batch.
    struct ZonePruneResult
    {
        QVector<PhosphorEngine::ZoneAssignmentEntry> unsnapped;
        QVector<PhosphorEngine::ZoneAssignmentEntry> narrowed;
    };

    /// Drop stale zones in the store holding each: an id no layout holds, in
    /// every context, and for a context in view that runs snapping, a zone its
    /// layout lacks (None keeps them). A window left in no zone and not floating
    /// gives up its snap record slot. The adaptor relays the result.
    ZonePruneResult pruneStaleZoneAssignments();
    /// The prune alone; the adaptor calls pruneStaleZoneAssignments to relay.
    void onLayoutChanged();

    // ═══════════════════════════════════════════════════════════════════════════
    // State Access
    // ═══════════════════════════════════════════════════════════════════════════

    /// Live snap zones if present, else the durable placement-record snap slot.
    /// See IWindowTrackingService::recordedSnapZones. A multi-desktop window
    /// answers for the desktop its screen shows (its live store there, else
    /// the record's per-desktop map), never another desktop's zone.
    QStringList recordedSnapZones(const QString& windowId) const override;
    /// @p slot's zones for the desktop @p screenId shows: the per-desktop map's
    /// entry when the slot carries one (empty when it names no zone there),
    /// else the flat zoneIds.
    QStringList snapZonesOnDesktopInView(const PhosphorEngine::EngineSlot& slot, const QString& screenId) const;

    using ResnapEntry = PhosphorEngine::ResnapEntry;

    QVector<ResnapEntry> takeResnapBuffer() override;
    void markLastUsedZoneDirty() override;

    /// The user-snapped classes.
    const QSet<QString>& userSnappedClasses() const;

    /**
     * @brief Set user-snapped classes (loaded from the persisted state by the adaptor)
     *
     * FAILS SAFE, not lossy: called before a SnapState is wired (the adaptor's
     * constructor loads state before the daemon installs the resolver), the
     * classes are HELD and flushed by setSnapStateResolver / setSnapState.
     * Dropping them made recovery depend on an incidental second load, and a
     * save in between would have written the emptied set back over the user's
     * auto-snap-by-class list.
     */
    void setUserSnappedClasses(const QSet<QString>& classes);

    /**
     * @brief Set last used zone info (loaded from KConfig by adaptor)
     */
    void setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    /**
     * @brief Set floating windows (test / bulk-seed entry point)
     *
     * No production loader remains: floating state is ephemeral and the
     * adaptor's save path never writes it (the implementation documents
     * this). Kept as public API for the unit suites that seed float state
     * directly.
     */
    void setFloatingWindows(const QSet<QString>& windows);

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
    // NOTE: several bits below (DirtyZoneAssignments,
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
        // bit 2 reserved (was DirtyPendingRestores, removed with the pending-restore queue)
        DirtyPreTileGeometries = 1u << 3, // legacy save-trigger → DirtyWindowPlacements
        DirtyLastUsedZone = 1u << 4,
        DirtyPreFloatZones = 1u << 5, // legacy save-trigger → DirtyWindowPlacements
        DirtyPreFloatScreens = 1u << 6, // legacy save-trigger → DirtyWindowPlacements
        DirtyUserSnapped = 1u << 7,
        DirtyAutotileOrders = 1u << 8, // legacy save-trigger → DirtyWindowPlacements
        DirtyAutotilePending = 1u << 9, // legacy save-trigger → DirtyWindowPlacements
        // bit 10 reserved (was DirtyFloatRestores, removed with the FloatRestoreQueues key)
        DirtyWindowPlacements = 1u << 11, ///< unified WindowPlacementStore (sole per-window restore state)
        DirtyScrollStrips = 1u << 12, ///< scrolling strip-structure snapshots (WTA-provided blob)
        DirtyAll = 0x1FFFu, // covers bits 0-12 incl. the reserved bits 2 and 10
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
    DirtyMask peekDirty() const;

    /// Clear every dirty bit. Called from loadState's end after in-memory
    /// state mirrors the disk file — nothing is dirty until the next
    /// mutation lands.
    void clearDirty();

Q_SIGNALS:
    // WARNING: AutotileEngine connects to this signal via string-based SIGNAL/SLOT
    // (it holds IWindowTrackingService*, not WindowTrackingService*, so PMF connect
    // is unavailable). Renaming this signal will silently break autotile zone tracking.
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /// Emitted when state needs to be saved
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

    /// Does @p geometry meaningfully overlap the screen named @p screenId?
    ///
    /// isGeometryOnScreen asks whether a rect is on ANY screen, which cannot
    /// tell a rect filed under the right screen from one filed under the wrong
    /// one — both are "on a screen". This asks the question the per-screen free
    /// geometry map is keyed on.
    ///
    /// The two deliberately DIVERGE in three ways, so do not assume one can
    /// stand in for the other. This one resolves a virtual screen id to its
    /// physical output (a floating rect is not confined to one subdivision),
    /// where isGeometryOnScreen iterates effective ids including the virtual
    /// sub-screens. This one clamps its thresholds to the window's own size, so
    /// a window smaller than the 100px floor still passes when it lies wholly
    /// on its screen; isGeometryOnScreen keeps the flat floor its rescue
    /// question wants. And this one has no QGuiApplication fallback: with no
    /// ScreenManager it fails open rather than answering from QScreen.
    ///
    /// Fails OPEN (true) when the screen cannot be resolved: an unknown or
    /// not-yet-configured screen must not silently discard a capture, and an
    /// embedder with no ScreenManager wired has no opinion to enforce.
    bool geometryOverlapsScreen(const QRect& geometry, const QString& screenId) const;
    QRect adjustGeometryToScreen(const QRect& geometry) const;
    PhosphorZones::Zone* findZoneById(const QString& zoneId) const;

    /// windowId-then-appId fallback lookup across every snap store. The getter
    /// takes (store, id) so the lookup can scan each per-screen store for both keys.
    template<typename Func>
    auto preFloatLookup(const QString& windowId, Func&& getter) const
        -> decltype(getter(std::declval<PhosphorSnapEngine::SnapState*>(), windowId));

    /// Clear the last-used zone on EVERY snap store that points at
    /// @p targetScreen but whose zone no longer exists in that screen's layout
    /// (last-used is per-key, so this sweeps all of them, not one member).
    /// Marks DirtyLastUsedZone itself when it clears any, so a caller that
    /// schedules no save of its own still persists the clear. A screen whose
    /// layout does not resolve is left alone: without a layout nothing can
    /// prove the zone is stale.
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
    ///   matches (or is 0 = pinned/all-desktops), so windows parked on other
    ///   desktops do not make zones appear occupied. A store of another
    ///   activity never counts.
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
    // ── Snap-state resolution helpers (wrap the injected resolver) ───────────
    // Each returns nullptr / empty when the resolver is unwired, preserving the
    // historical "no SnapState" no-op guards at the call sites.
    PhosphorSnapEngine::SnapState* snapForWindow(const QString& windowId) const;
    /// Whether @p state holds a membership for the window (see
    /// SnapStateResolver::holdsWindow, daemon-wired). Unwired: primary only.
    bool snapHoldsWindow(const QString& windowId, const PhosphorSnapEngine::SnapState* state) const;
    PhosphorSnapEngine::SnapState* snapForWindowOnScreen(const QString& windowId, const QString& screenId,
                                                         int desktop = 0);
    PhosphorSnapEngine::SnapState* snapForScreen(const QString& screenId) const;
    /// The store holding the single representative last-used zone: the one with the
    /// highest lastUsedSeq() among all stores that have a non-empty last-used zone,
    /// else the global holder. This is what the facade's screen-agnostic lastUsed*
    /// getters and the persistence layer read. (Defined out-of-line: SnapState is
    /// only forward-declared here.)
    PhosphorSnapEngine::SnapState* snapRepresentativeLastUsed() const;
    /// Clear the GLOBAL holder's last-used zone if it names a zone in @p removedZones
    /// and the holder is not @p owningStore (whose own last-used the caller already
    /// handled). Returns true if it cleared. Shared by the unassign / unsnap-for-float
    /// paths, which both drop a store's zones and must scrub the disk-restored
    /// representative that lives on the global holder.
    bool clearGlobalLastUsedIfRemoved(const QStringList& removedZones,
                                      const PhosphorSnapEngine::SnapState* owningStore);
    /// unassignWindow's body on @p store, which need not be the primary.
    bool unassignFromStore(const QString& windowId, PhosphorSnapEngine::SnapState* store);

    /// Unassign every window in @p windowsToRemove (live windows whose zones
    /// did not survive the change) and drop its pre-float zone, keeping its
    /// free geometry and sticky flag, then emit windowZoneChanged for each
    /// window that actually held a zone, as the interactive unassign path
    /// does. The emit is per-window and comes after that window's clears.
    /// Shared by the two VS-migration prune loops
    /// (migrateScreenAssignmentsToVirtual / ...FromVirtual). Marks
    /// DirtyLastUsedZone when any last-used cleared; returns true when given a
    /// non-empty list (the callers only build it from zone-assigned windows,
    /// so non-empty means state changed and a save is due).
    bool pruneMigratedWindows(const QStringList& windowsToRemove);
    PhosphorSnapEngine::SnapState* snapGlobals() const;
    /// Every store, for aggregate iteration. Elements are never null (see the
    /// resolver's `allStates` contract), so callers deref them directly.
    QList<PhosphorSnapEngine::SnapState*> snapAllStates() const;
    /// True once the resolver is wired — the drop-in replacement for the former
    /// `m_snapState != nullptr` guards.
    bool hasSnapState() const;
    /// Invoke @p fn once per (window, store) zone assignment the window is a
    /// MEMBER of (a leftover in another store is skipped), with its zones and
    /// screen, the store's context and the store itself. @p desktop is 0 for a
    /// sticky window, else the store key's desktop; @p activity is the key's.
    /// Keyless (an unset keyFor, the single-store convenience) passes the
    /// window's recorded desktop and an empty activity, which every filter
    /// reads as current. @p fn must not mutate the stores: collect, then
    /// mutate the visited @p store. Kept in lockstep with
    /// SnapEngine::forEachSnapAssignment.
    void forEachZoneAssignedWindow(
        const std::function<void(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                 int desktop, const QString& activity, PhosphorSnapEngine::SnapState* store)>& fn)
        const;

    /// Push a load that arrived before any SnapState was wired into the store
    /// once one exists. No-op when nothing is held. See setUserSnappedClasses.
    /// A detach (`setSnapState(nullptr)`) DISCARDS the hold rather than
    /// carrying it to the next store.
    void flushPendingUserSnappedClasses();

    // Dependencies
    PhosphorZones::LayoutRegistry* m_layoutManager;
    SnapStateResolver m_snapResolver;
    /// User-snapped classes loaded before a SnapState existed, awaiting the
    /// flush above. Engaged only across that startup window.
    std::optional<QSet<QString>> m_pendingUserSnappedClasses;
    PhosphorEngine::WindowPlacementStore m_placementStore;
    IGeometryResolver* m_geometryResolver;
    PlacementConfig m_config;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    // Shared registry for current-class queries and canonical key translation.
    // Not owned. Null in unit tests. QPointer (not raw): both are Daemon
    // children whose destruction order relative to this service is not
    // contractual — a raw borrow dangled during Daemon child teardown, while
    // the QPointer auto-nulls and every use site already null-guards.
    QPointer<PhosphorEngine::WindowRegistry> m_windowRegistry;
    QPointer<PhosphorScreens::ScreenManager> m_screenManager;
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    AutotileModePredicate m_autotileModePredicate{};
    EngineTiledPredicate m_engineTiledPredicate{};
    ModeEngineIdResolver m_modeEngineIdResolver{};

    // Floating windows, read and written only while no per-engine float
    // resolver/writer is wired (unit tests, early init).
    QSet<QString> m_floatingWindows;

    // Suspension-float classification — see isSuspensionFloat(). Canonical-
    // keyed. Session-transient (never persisted): a restart's restored floats
    // are re-classified when their windows re-report minimize state.
    QSet<QString> m_suspensionFloats;

    // Daemon-injected per-engine float reader/writer/lister. See setEngineFloatResolver.
    EngineFloatResolver m_engineFloatResolver{};
    EngineFloatWriter m_engineFloatWriter{};
    EngineFloatLister m_engineFloatLister{};

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

    // Appended last (installed class).
    ManagedFramePredicate m_managedFramePredicate{};

    // Note: No save timer - persistence is the WindowTrackingAdaptor's debounced
    // JSON save. Service emits stateChanged() signal when state needs saving
};

} // namespace PhosphorPlacement
