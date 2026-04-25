// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngineApi/IWindowTrackingService.h>
#include <PhosphorProtocol/WireTypes.h>

#include <QString>

#include <functional>

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

using PhosphorProtocol::CycleTargetResult;
using PhosphorProtocol::FocusTargetResult;
using PhosphorProtocol::MoveTargetResult;
using PhosphorProtocol::RestoreTargetResult;
using PhosphorProtocol::SwapTargetResult;

class ISettings;

class ZoneDetectionAdaptor;

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
 *   WindowTrackingService / PhosphorZones::LayoutRegistry / ZoneDetectionAdaptor and
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
 * shared result-struct types (MoveTargetResult etc. from PhosphorProtocol/WireTypes.h).
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
     * @param zoneDetector       adjacent/first-in-direction query helper (non-owning)
     * @param feedback           OSD feedback callback; may be empty (suppresses feedback)
     */
    SnapNavigationTargetResolver(PhosphorEngineApi::IWindowTrackingService* service,
                                 PhosphorZones::LayoutRegistry* layoutManager, QObject* zoneDetector,
                                 FeedbackFn feedback);

    /// Late setter for the zone detector — it may not be available at
    /// construction time, so we allow nullptr and bind the pointer when
    /// it becomes available.
    void setZoneDetector(QObject* zoneDetector);

    MoveTargetResult getMoveTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    FocusTargetResult getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                              const QString& screenId);

    RestoreTargetResult getRestoreForWindow(const QString& windowId, const QString& screenId);

    CycleTargetResult getCycleTargetForWindow(const QString& windowId, bool forward, const QString& screenId);

    SwapTargetResult getSwapTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    MoveTargetResult getPushTargetForWindow(const QString& windowId, const QString& screenId);

    MoveTargetResult getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber, const QString& screenId);

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

    PhosphorEngineApi::IWindowTrackingService* m_service = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    QObject* m_zoneDetector = nullptr;
    FeedbackFn m_feedback;
};

} // namespace PlasmaZones
