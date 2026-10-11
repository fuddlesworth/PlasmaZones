// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): WindowTrackingService is the engine-
// agnostic facade every placement engine and the daemon share, and this is an
// INSTALLED public header — splitting the class is an API break for the
// third-party consumers the LGPL boundary exists to serve. The implementation
// is already split by concern across src/ (WindowTrackingService.cpp and its
// _sticky part, snap, resnap, navigation, virtualscreenmigration, lifecycle),
// and the member ordering here encodes which of those owns what. Same
// rationale as PhosphorTileEngine/AutotileEngine.h.

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
 * The engine-agnostic facade over the snap stores and the unified
 * WindowPlacementStore:
 *
 * - zone assignments (which window is in which zone), read through SnapState
 * - free (float-back) geometry per screen, held in the placement records
 * - float routing to the owning engine, and the suspension-float and sticky bits
 * - the resnap buffer, the dirty mask and the last-used zone companions
 *
 * The WindowTrackingAdaptor persists it through the dirty mask. The auto-snap
 * and rotation decisions are SnapEngine's (calculateSnapToLastZone, calculateRotation).
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
     * @brief Wire the shared WindowRegistry. Not owned; the adaptor forwards its
     * own setter, so it can be swapped or cleared at any time.
     *
     * Feeds currentAppIdFor, canonicalizeForLookup, the placement store's
     * live-instance probe, the durable resnap arm's desktop context, and
     * recordFloatingClose's minimized and output-filling refusals. Without one
     * (unit tests) each is permissive: the class parsed from the id, a
     * passthrough key, no live instance, no context, no refusal.
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
     * @brief Wire the snap-mode placement engine. Not owned.
     *
     * Not the float-back store (that is the shared WindowPlacementStore). Read
     * for the stale-window prune and for isActiveOnScreen, the "snapping runs
     * here live" test of the resnap, prune and occupancy paths; unset (unit
     * tests), those read the configured mode or pass.
     */
    void setSnapEngine(PhosphorEngine::PlacementEngineBase* engine);

    PhosphorEngine::PlacementEngineBase* snapEngine() const;

    /**
     * @brief Predicate: is the window's CURRENT screen in Autotile mode?
     *
     * Injected by the daemon (engine-/settings-agnostic LGPL boundary). Its one
     * reader is the adaptor's capture funnel, which tries the autotile capture
     * first when it answers true. When unset, every window reads as not autotile.
     */
    using AutotileModePredicate = std::function<bool(const QString& windowId)>;
    void setAutotileModePredicate(AutotileModePredicate predicate);

    /// The AutotileModePredicate's answer for @p windowId; false when unwired
    /// (snap-only tests / early init).
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

    /// Downgrade managed slots recorded against a different screen: when @p recordedScreenId does not match
    /// @p closeScreenId, every snapped/tiled slot in @p placement becomes plain floating with its zone ids, order
    /// and per-desktop zones cleared, since they name THAT screen's zones and tile order. Same-screen (or
    /// unscreened) records pass through. Shared by recordFloatingClose and the adaptor's minimize preserve so
    /// the two close-shaped writers cannot drift.
    static void downgradeMismatchedManagedSlots(PhosphorEngine::WindowPlacement& placement,
                                                const QString& recordedScreenId, const QString& closeScreenId);
    /// The (engine id, desktop) entries that downgrade clears from the per-desktop maps, read before it runs:
    /// record() merges the stored map back in, so a caller forgets these after recording (F282).
    static QList<QPair<QString, int>> mismatchedDesktopZones(const PhosphorEngine::WindowPlacement& placement,
                                                             const QString& recordedScreenId,
                                                             const QString& closeScreenId);

    /// The wired registry, or null. Read by the daemon's engine seed, release and
    /// minimize filters and by the drag adaptor.
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

    /// Drop per-window state for windows not in the alive set: every snap store's
    /// assignments, the sticky map, the legacy float set, the suspension floats and
    /// SnapEngine's state-index memberships (SnapEngine::pruneStaleWindows).
    /// Refuses an empty set. Returns the number of pruned entries.
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
     * When @p savedScreen and @p currentScreenId name different screens the rect
     * keeps its size (clamped) and is centred on the current screen's available
     * area; otherwise an on-screen rect comes back as is and an off-screen one is
     * clamped onto the nearest screen. Reads the screen manager. Its one caller,
     * validatedUnmanagedGeometry, passes the same screen twice, so only the
     * on-screen check runs there.
     *
     * @param geo             Saved geometry (e.g. a window's recorded free geometry)
     * @param savedScreen     Screen id at capture time (may be empty)
     * @param currentScreenId Screen id the window is on now
     * @return Adjusted geometry, or nullopt if @p geo is invalid
     */
    std::optional<QRect> validateGeometryForScreen(const QRect& geo, const QString& savedScreen,
                                                   const QString& currentScreenId) const;

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
    /// The close-time capture of an engine-orphaned window: the float geometry
    /// (P, M, S as above) and @p screenId as the record's managed screen. A managed
    /// slot recorded on another screen is downgraded to a float and forgets its
    /// per-desktop zones, the owning engine gets a floating slot if it lacks one,
    /// and the app's pure-float siblings on the screen collapse into this record.
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
     * in each engine (SnapState, TilingState, the scroll engine's float set), keyed by
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
     * @brief Whether the engine running the window's current screen mode floats it
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
     * still fresh). Cleared by the adaptor on unfloat and on windowClosed, and
     * by SnapEngine's Meta+F unfloat, which never crosses the adaptor
     * (WindowTrackingService::windowClosed itself does not touch the set). The
     * prune backstop sweeps it for windows that die without a close signal.
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
     * @brief Get primary zone to restore to when unfloating, from the window's
     *        own entry in its primary snap store
     * @param windowId Full window ID
     * @return PhosphorZones::Zone ID or empty string if none
     */
    QString preFloatZone(const QString& windowId) const;

    /**
     * @brief Get all zones to restore to when unfloating (multi-zone support),
     *        from the window's primary snap store
     * @param windowId Full window ID
     * @return List of zone IDs (empty if none)
     */
    QStringList preFloatZones(const QString& windowId) const override;

    /**
     * @brief Get the screen name where the window was snapped before floating,
     *        from the window's primary snap store
     * @param windowId Full window ID
     * @return Screen name or empty string if unknown
     */
    QString preFloatScreen(const QString& windowId) const override;

    /// Clear the window's pre-float zone in every snap store.
    void clearPreFloatZone(const QString& windowId) override;

    /**
     * @brief Clear the float when a floating window is snapped
     *
     * Unfloats through the routed writer and drops the pre-float zone in the
     * window's primary store. Called by SnapEngine's commit, which emits
     * windowFloatingClearedForSnap when this or snap's own float bit cleared.
     *
     * @param windowId Window identifier
     * @return true if the routed float read answered floating
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
     * Called by the reactive metadata handler when the renamed window was the
     * tracked class's only instance. The tag is informational and rides with the
     * last-used companions: auto-snap by class reads userSnappedClasses instead.
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
     * @param excludeScreens Screens to skip (the screens a tiling engine runs, autotile or scrolling)
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
     * Sweeps every state that holds a screen id (each snap store's screen and
     * pre-float screen assignments, every placement record's managed screen and
     * free-geometry keys) and returns the set of physical screen ids
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
     * @brief Set last used zone info (the adaptor's session load; held until a store is wired)
     */
    void setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    /**
     * @brief Set floating windows (test / bulk-seed entry point)
     *
     * Seeds only the legacy fallback set, which nothing persists and which is
     * unread once the engine float resolver is wired. A snap float itself does
     * persist, as the record's floating snap slot. Kept for the unit suites.
     */
    void setFloatingWindows(const QSet<QString>& windows);

    // ═══════════════════════════════════════════════════════════════════════
    // Dirty field tracking
    //
    // A bitfield of which persisted state changed. WindowTrackingAdaptor::
    // saveState() snapshots and clears it in one step (takeDirty) and writes
    // only the keys its bits name; a write that fails re-marks the bits it
    // carried, so the next tick retries them. The mask starts at DirtyAll, but
    // the adaptor's constructor load ends with clearDirty(), so the first save
    // writes only what changed after the load.
    // ═══════════════════════════════════════════════════════════════════════
    // NOTE: several bits below (DirtyZoneAssignments,
    // DirtyPreTileGeometries, DirtyPreFloatZones, DirtyPreFloatScreens,
    // DirtyAutotileOrders, DirtyAutotilePending) no longer have a dedicated save
    // block — their legacy keys were collapsed into DirtyWindowPlacements. The
    // runtime mutators that still OR them in are retained because the act of
    // marking ANY bit schedules a save, and saveState()'s refreshOpenWindowPlacements()
    // re-derives the affected per-window state into the placement record. They are
    // therefore "schedule a save" triggers, not independent persisted fields; only
    // DirtyActiveLayoutId / DirtyLastUsedZone / DirtyUserSnapped / DirtyWindowPlacements / DirtyScrollStrips
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

    /// OR the given fields into the dirty mask AND emit stateChanged. The
    /// mutators' entry point (scheduleSaveState is its DirtyAll wrapper).
    /// Public because the adaptor also marks from outside: an active-layout
    /// change it observes, and a failed write's bits re-marked for retry.
    /// Idempotent (OR) and cheap.
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
    /// The window's primary zone changed (empty: it left every zone). Relayed by
    /// WindowTrackingAdaptor::windowZoneChanged; the daemon dismisses Snap Assist on it.
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /// Emitted when state needs to be saved
    void stateChanged();

private:
    // Minimum visible area for geometry validation
    static constexpr int MinVisibleWidth = 100;
    static constexpr int MinVisibleHeight = 100;

    // Helpers
    //
    // scheduleSaveState() wraps markDirty(DirtyAll), for a mutator that does
    // not name the fields it touched (windowClosed, the VS migrations). The
    // rest call markDirty with a narrow mask.
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

    /// The window's own pre-float entry in its primary snap store; the getter takes (store, id).
    template<typename Func>
    auto preFloatLookup(const QString& windowId, Func&& getter) const
        -> decltype(getter(std::declval<PhosphorSnapEngine::SnapState*>(), windowId));

    /// Clear the last-used zone on every snap store pointing at @p targetScreen whose zone is gone from
    /// that store's own layout (lastUsedLayoutFor; a store with none is left alone, as nothing can prove
    /// it stale). Marks DirtyLastUsedZone itself, so a caller that schedules no save still persists it.
    void validateLastUsedZone(const QString& targetScreen);
    /// The layout @p state's last-used zone belongs to on @p screenId: the store's own desktop and
    /// activity (F162). Null for a context that does not snap or has no layout.
    PhosphorZones::Layout* lastUsedLayoutFor(const PhosphorSnapEngine::SnapState* state, const QString& screenId) const;

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
    /// handled). Returns true if it cleared. Shared by unassignFromStore, unsnapForFloat
    /// and pruneMigratedWindows, which each drop a store's zones and must scrub the
    /// disk-restored representative that lives on the global holder.
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
    /// True while the resolver's globals store resolves: wired, and (for the engine
    /// resolver, whose arms hold a QPointer) while the snap engine is alive.
    bool hasSnapState() const;
    /// Invoke @p fn once per (window, store) zone assignment the window is a
    /// MEMBER of (a leftover in another store is skipped), with its zones and
    /// screen, the store's context and the store itself. @p desktop is 0 for a
    /// sticky window, else the store key's desktop; @p activity is the key's.
    /// Keyless (an unset keyFor, the single-store convenience) passes the
    /// window's recorded desktop and an empty activity, which every filter
    /// reads as current. @p fn must not mutate the stores: collect, then
    /// mutate the visited @p store. SnapEngine::forEachSnapAssignment is not its
    /// twin: it has no membership guard and visits leftovers too.
    void forEachZoneAssignedWindow(
        const std::function<void(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                 int desktop, const QString& activity, PhosphorSnapEngine::SnapState* store)>& fn)
        const;

    /// Push a load that arrived before any SnapState was wired into the store once one exists (no-op when
    /// nothing is held; see setUserSnappedClasses). A detach DISCARDS the hold instead of carrying it on.
    void flushPendingUserSnappedClasses();

    // Dependencies
    PhosphorZones::LayoutRegistry* m_layoutManager;
    SnapStateResolver m_snapResolver;
    /// User-snapped classes loaded before a SnapState existed, awaiting the flush above.
    std::optional<QSet<QString>> m_pendingUserSnappedClasses;
    PhosphorEngine::WindowPlacementStore m_placementStore;
    IGeometryResolver* m_geometryResolver;
    PlacementConfig m_config;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    // Shared registry for current-class queries and canonical key translation; not owned, null in unit tests.
    // Daemon::stop() clears it, and the QPointer (as for the screen manager) covers the destruction order: both
    // are Daemon unique_ptr members, destroyed before ~QObject deletes the adaptor and this service.
    QPointer<PhosphorEngine::WindowRegistry> m_windowRegistry;
    QPointer<PhosphorScreens::ScreenManager> m_screenManager;
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    AutotileModePredicate m_autotileModePredicate{};
    EngineTiledPredicate m_engineTiledPredicate{};
    ModeEngineIdResolver m_modeEngineIdResolver{};

    // Floating windows, read and written only while no per-engine float
    // resolver/writer is wired (unit tests, early init).
    QSet<QString> m_floatingWindows;

    // Suspension-float classification (see isSuspensionFloat), canonical-keyed, never persisted. After a
    // restart only a tiling screen's minimize floats are re-marked (the effect re-asserts them); a snap
    // screen's are not, so that window's unminimize unfloat runs as a user toggle.
    QSet<QString> m_suspensionFloats;

    // Daemon-injected per-engine float reader/writer/lister. See setEngineFloatResolver.
    EngineFloatResolver m_engineFloatResolver{};
    EngineFloatWriter m_engineFloatWriter{};
    EngineFloatLister m_engineFloatLister{};

    // Sticky window states
    QHash<QString, bool> m_windowStickyStates;

    QVector<ResnapEntry> m_resnapBuffer;

    // Delta-persistence dirty mask (see markDirty). Starts at DirtyAll; the adaptor's constructor load clears it.
    DirtyMask m_dirtyMask = DirtyAll;

    // Appended last (installed class).
    ManagedFramePredicate m_managedFramePredicate{};
    /// A last-used zone restored before any SnapState was wired, held and flushed like the classes (F236).
    struct PendingLastUsedZone
    {
        QString zoneId, screenId, zoneClass;
        int desktop = 0;
    };
    std::optional<PendingLastUsedZone> m_pendingLastUsedZone{};
    void flushPendingLastUsedZone();
    // No save timer: persistence is the WindowTrackingAdaptor's debounced save, driven by stateChanged().
};

} // namespace PhosphorPlacement
