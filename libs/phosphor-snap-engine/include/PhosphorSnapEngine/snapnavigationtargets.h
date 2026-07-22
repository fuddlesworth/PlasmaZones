// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorProtocol/NavigationTypes.h>

#include <QRect>
#include <QString>
#include <QStringList>

#include <functional>

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PhosphorEngine {
class ICrossSurfaceResolver;
}

namespace PhosphorSnapEngine {

class ISettings;
class IZoneAdjacencyResolver;

/// Result of a span (grow/shrink) target computation. Resolver-local rather
/// than a PhosphorProtocol type: span is driven only by the daemon's shortcut
/// path and never crosses D-Bus, so the zone list can stay a QStringList.
struct SpanTargetResult
{
    bool success = false;
    QString reason;
    /// The window's full NEW span set, primary zone first. On a grow this is
    /// the old set plus every zone the extension band swept up; on a shrink
    /// it is the old set minus the retracted edge band.
    QStringList zoneIds;
    /// Union rect of the new span's zone geometries.
    QRect geometry;
    /// Primary zone before the change (empty when the window was unsnapped).
    QString sourceZoneId;
    QString screenName;
    /// true when the span grew into new zone(s), false when it retracted.
    bool grew = false;
};

/// The edge a neighbour surface is entered from when crossing in @p direction:
/// crossing "right" lands on the neighbour's LEFT edge, "down" on its TOP, etc.
/// Empty for an unknown token. Shared by the resolver's cross-output entry/swap
/// targets and SnapEngine::entryZoneForCrossing so the mapping lives in one place.
inline QString oppositeCrossingDirection(const QString& direction)
{
    if (direction == QLatin1String("left")) {
        return QStringLiteral("right");
    }
    if (direction == QLatin1String("right")) {
        return QStringLiteral("left");
    }
    if (direction == QLatin1String("up")) {
        return QStringLiteral("down");
    }
    if (direction == QLatin1String("down")) {
        return QStringLiteral("up");
    }
    return {};
}

/**
 * @brief Pure snap-mode navigation target resolver.
 *
 * Computes geometry targets for snap-mode keyboard navigation (move,
 * focus, swap, push, snap-by-number, cycle, restore). Extracted from
 * WindowTrackingAdaptor so that the adaptor's D-Bus surface no longer
 * carries ~400 lines of snap-internal computation as member methods.
 *
 * Separation of concerns:
 * - This class is pure compute. It holds const* references to
 *   WindowTrackingService / PhosphorZones::LayoutRegistry / IZoneAdjacencyResolver and
 *   reads from them. It does not mutate their state.
 * - It never touches D-Bus or Qt signals directly — navigation
 *   feedback (OSD data) is emitted through a std::function callback
 *   wired at construction time. The adaptor forwards that callback
 *   to its own navigationFeedback signal.
 * - Validation failures (empty windowId, empty direction) are
 *   considered pre-call contract violations and return an early
 *   noSnap-equivalent result; the adaptor's dispatcher is expected
 *   to catch these before they reach the resolver.
 *
 * This class is deliberately not a QObject — it needs no signals of
 * its own, and the absence of QObject machinery makes it trivially
 * constructable for unit tests. The single dependency on Qt is the
 * shared result-struct types (PhosphorProtocol::MoveTargetResult etc. from PhosphorProtocol/NavigationTypes.h).
 *
 * All methods are intended to be called only for screens that the
 * router (ScreenModeRouter) has confirmed are in Snapping mode. The
 * resolver does not re-check mode — that's the dispatcher's job.
 */
class PHOSPHORSNAPENGINE_EXPORT SnapNavigationTargetResolver
{
public:
    /// Callback shape matching WindowTrackingAdaptor::navigationFeedback.
    /// Invoked by the resolver whenever a target computation succeeds or
    /// fails in a user-visible way. The adaptor wires this to its own
    /// navigationFeedback signal at construction time.
    using FeedbackFn =
        std::function<void(bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
                           const QString& targetZoneId, const QString& screenId)>;

    /**
     * @brief Construct the resolver with its pure dependencies.
     *
     * @param service            window tracking state store (non-owning)
     * @param layoutManager      layout / zone owner (non-owning)
     * @param zoneAdjacency      typed adjacency resolver (non-owning; may be nullptr)
     * @param feedback           OSD feedback callback; may be empty (suppresses feedback)
     */
    SnapNavigationTargetResolver(PhosphorEngine::IWindowTrackingService* service,
                                 PhosphorZones::LayoutRegistry* layoutManager, IZoneAdjacencyResolver* zoneAdjacency,
                                 FeedbackFn feedback);

    /// Late setter for the zone adjacency resolver — it may not be available at
    /// construction time, so we allow nullptr and bind the pointer when
    /// it becomes available.
    void setZoneAdjacencyResolver(IZoneAdjacencyResolver* resolver);

    /// Late setter for the cross-surface resolver (neighbour output / desktop
    /// lookup). When set, a navigation that finds no adjacent zone on the
    /// current output crosses to the entry zone of the adjacent output instead
    /// of failing. May be nullptr.
    void setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver);

    /// Reports whether a neighbour OUTPUT is in autotile mode, evaluated in the
    /// engine's current (desktop, activity) context — which the resolver itself
    /// lacks. When set, the MOVE and SWAP cross-output paths skip an autotile
    /// neighbour (deferring to the engine's cross-mode handoff) instead of
    /// snapping the window onto a tiled screen. May be empty, in which case no
    /// gating happens and every neighbour is treated as snap-mode (the
    /// pre-provider behaviour). The FOCUS cross-output path is never gated — it
    /// may still cross to an autotile screen.
    using NeighbourAutotileFn = std::function<bool(const QString& screenId)>;
    void setNeighbourAutotileProvider(NeighbourAutotileFn fn);

    PhosphorProtocol::MoveTargetResult getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                              const QString& screenId);

    /**
     * @brief Compute the new zone span when the user grows/shrinks toward @p direction.
     *
     * One shortcut quad drives both operations: when zone(s) exist beyond the
     * span's @p direction edge, the span GROWS into them (the extension band
     * sweeps up every zone between the old edge and the picked neighbour's far
     * edge, so growing a full-height span into a column of stacked zones takes
     * the whole column). When nothing lies that way, the span SHRINKS instead
     * by dropping the member band on the opposite edge (pressing left after
     * growing right undoes the grow). An unsnapped window snaps into the edge
     * zone in @p direction, mirroring getMoveTargetForWindow. A single-zone
     * span with nothing to grow into fails with "no_adjacent_zone".
     *
     * Span never crosses outputs or desktops — a span is a set of zones on one
     * screen's layout, so the boundary is a hard stop, not a handoff.
     */
    SpanTargetResult getSpanTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    PhosphorProtocol::FocusTargetResult getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                                                const QString& screenId);

    PhosphorProtocol::RestoreTargetResult getRestoreForWindow(const QString& windowId, const QString& screenId);

    PhosphorProtocol::CycleTargetResult getCycleTargetForWindow(const QString& windowId, bool forward,
                                                                const QString& screenId);

    PhosphorProtocol::SwapTargetResult getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                              const QString& screenId);

    PhosphorProtocol::MoveTargetResult getPushTargetForWindow(const QString& windowId, const QString& screenId);

    PhosphorProtocol::MoveTargetResult getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                                   const QString& screenId);

private:
    /// Single emission point so call sites don't have to null-check the
    /// optional callback at every feedback opportunity. Inline + private
    /// keeps the cost identical to the previous EMIT_FEEDBACK macro
    /// without losing type checking.
    void emitFeedback(bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
                      const QString& targetZoneId, const QString& screenId) const
    {
        if (m_feedback) {
            m_feedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
        }
    }

    /// Cross-output entry target on a no-adjacent-zone boundary, or a
    /// non-success MoveTargetResult when there's no neighbour output / entry
    /// zone. Shared by the move and focus paths; the caller emits feedback so
    /// the move/focus tag stays correct.
    /// @param requireSnapNeighbour when true (move/swap), an autotile neighbour
    /// output yields a non-success result so the caller defers to the cross-mode
    /// handoff; when false (focus), the neighbour's mode is not gated.
    PhosphorProtocol::MoveTargetResult crossOutputEntryTarget(const QString& currentZoneId, const QString& direction,
                                                              const QString& sourceScreenId,
                                                              bool requireSnapNeighbour) const;

    /// Cross-output swap target on a no-adjacent-zone boundary: the focused
    /// window (@p windowId, in @p currentZoneId on @p sourceScreenId) crosses to
    /// the neighbour output's entry zone, trading places with that zone's
    /// occupant. window1 lands on the neighbour (screenName), window2 returns to
    /// the source output (screenName2). An empty entry zone degrades to a
    /// move-to-empty crossing (no window2). Returns a non-success result when
    /// there's no neighbour output / entry zone / valid geometry; the caller
    /// emits feedback so the swap tag stays correct.
    PhosphorProtocol::SwapTargetResult crossOutputSwapTarget(const QString& windowId, const QString& currentZoneId,
                                                             const QString& direction,
                                                             const QString& sourceScreenId) const;

    /// Windows snapped to @p zoneId whose stored screen is @p screenName, in
    /// windowsInZone() iteration order.
    /// windowsInZone() is screen-agnostic — the same zone UUID is shared by
    /// every output the layout is assigned to (zones resolve by UUID across all
    /// layouts), so a bare windowsInZone(zoneId) can include windows on the
    /// WRONG output when one layout drives multiple monitors. Filtering by the
    /// daemon's screen assignment pins the set to the intended output.
    QStringList windowsInZoneOnScreen(const QString& zoneId, const QString& screenName) const;

    /// A window snapped to @p zoneId whose stored screen is @p screenName, or
    /// empty if none (first in windowsInZoneOnScreen() order — deterministic per
    /// process, not a visual ordering).
    QString firstWindowInZoneOnScreen(const QString& zoneId, const QString& screenName) const;

    PhosphorEngine::IWindowTrackingService* m_service = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    IZoneAdjacencyResolver* m_zoneAdjacency = nullptr;
    PhosphorEngine::ICrossSurfaceResolver* m_crossSurface = nullptr;
    FeedbackFn m_feedback;
    NeighbourAutotileFn m_neighbourIsAutotile;
};

} // namespace PhosphorSnapEngine
