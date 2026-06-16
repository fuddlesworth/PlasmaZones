// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorProtocol/NavigationTypes.h>

#include <QString>

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

    PhosphorProtocol::MoveTargetResult getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                              const QString& screenId);

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
    PhosphorProtocol::MoveTargetResult crossOutputEntryTarget(const QString& currentZoneId, const QString& direction,
                                                              const QString& sourceScreenId) const;

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
};

} // namespace PhosphorSnapEngine
