// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QStringList>

#include "../autotilehandler.h"
#include "../navigationhandler.h"
#include "../scrollhandler.h"
#include "../snapassisthandler.h"

#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
/// Fit a scroll-mode window into its tile slot. A window that cannot fill the
/// slot — fixed-size or max-constrained — keeps its constrained size, centred
/// in the slot: the Phase 2 MVP fallback for non-resizable windows. A normal
/// resizable window is returned the slot unchanged.
QRect constrainToScrollSlot(KWin::EffectWindow* window, const QRect& slot)
{
    KWin::Window* kw = window ? window->window() : nullptr;
    if (!kw) {
        return slot;
    }
    const QSizeF minS = kw->minSize();
    const QSizeF maxS = kw->maxSize();

    int w = slot.width();
    int h = slot.height();
    // maxSize() reports a large sentinel for unconstrained windows, so a
    // genuine maximum clamps here and an unconstrained one is left alone.
    if (maxS.width() > 0.0 && w > maxS.width()) {
        w = qFloor(maxS.width());
    }
    if (maxS.height() > 0.0 && h > maxS.height()) {
        h = qFloor(maxS.height());
    }
    // A minimum larger than the slot means the window will overflow — accepted
    // for the MVP; pin it to the minimum so the daemon's rect is honoured. The
    // min clamp runs after the max clamp, so a window with contradictory hints
    // (minSize > maxSize) resolves to its minimum.
    if (minS.width() > 0.0 && w < minS.width()) {
        w = qCeil(minS.width());
    }
    if (minS.height() > 0.0 && h < minS.height()) {
        h = qCeil(minS.height());
    }
    if (w == slot.width() && h == slot.height()) {
        return slot; // fills the slot exactly — nothing to centre
    }
    // Centre the constrained size; qMax keeps an over-min window pinned to the
    // slot's top-left (overflowing only right/bottom) rather than off both edges.
    return QRect(slot.x() + qMax(0, (slot.width() - w) / 2), slot.y() + qMax(0, (slot.height() - h) / 2), w, h);
}
} // namespace

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason,
                                               const QString& sourceZoneId, const QString& targetZoneId,
                                               const QString& screenId)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!isDaemonReady("report navigation feedback")) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("reportNavigationFeedback"),
                                                   {success, action, reason, sourceZoneId, targetZoneId, screenId});
}

void PlasmaZonesEffect::slotActivateWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        KWin::effects->activateWindow(w);
    } else {
        qCDebug(lcEffect) << "slotActivateWindowRequested: window not found" << windowId;
    }
}

void PlasmaZonesEffect::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x,
                                                              int y, int width, int height)
{
    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometry;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w) && getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && ::PhosphorIdentity::WindowId::extractAppId(getWindowId(w)) == appId) {
                targetWindow = w;
                break;
            }
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applySnapGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, getWindowId(targetWindow), targetWindow->frameGeometry());
    applySnapGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId = output ? resolveEffectiveScreenId(geoCenter, output) : getWindowScreenId(targetWindow);

    if (isDaemonReady("snap assist windowSnapped")) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("windowSnapped"),
                                                       {getWindowId(targetWindow), zoneId, screenId});
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("recordSnapIntent"),
                                                       {getWindowId(targetWindow), true});

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!m_autotileHandler->isAutotileScreen(screenId)) {
            m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

// slotToggleWindowFloatRequested removed — the daemon now handles float-toggle
// locally against its active-window + frame-geometry shadow and emits
// applyGeometryRequested directly. See WindowTrackingAdaptor::toggleWindowFloat.

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, int x, int y, int width, int height,
                                                   const QString& zoneId, const QString& screenId, bool sizeOnly)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }

    // Check for size-only restore (drag-out unsnap without activation trigger).
    // The daemon sets sizeOnly=true to restore pre-snap width/height while keeping
    // the window at its current drop position.
    if (sizeOnly) {
        if (width > 0 && height > 0) {
            QRectF currentFrame = w->frameGeometry();
            QRect sizeOnlyGeo(qRound(currentFrame.x()), qRound(currentFrame.y()), width, height);
            qCInfo(lcEffect) << "slotApplyGeometryRequested: size-only restore for" << windowId << width << "x"
                             << height;
            // Drag-out unsnap: the daemon kept us at the drop position but restored pre-snap
            // dimensions. Logically a snap-out (the window is leaving zone-managed sizing),
            // not an in-zone resize.
            applySnapGeometry(w, sizeOnlyGeo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                              PhosphorAnimation::ProfilePaths::WindowSnapOut);
        }
        return;
    }

    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometry;
        return;
    }
    // Skip float-restore geometry on minimized windows: when a snapped window is minimized
    // we float it (to free the zone slot), but applying the pre-tile geometry while minimized
    // would poison what KWin restores to on unminimize, causing a visible flash of the
    // pre-snap geometry before the unfloat re-snaps to the zone.
    if (w->isMinimized() && zoneId.isEmpty()) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: skipping float-restore geometry on minimized window:"
                          << windowId;
        return;
    }
    // Skip float-restore geometry for drag-to-float: when the user drags a window
    // off the autotile layout, the daemon restores pre-autotile geometry. But the
    // user expects the window to stay where they dropped it, not snap back.
    if (zoneId.isEmpty() && m_dragFloatedWindowIds.remove(windowId)) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:" << windowId;
        return;
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested:" << windowId << "geo:" << geometry << "zoneId:" << zoneId
                     << "screen:" << screenId << "floating:" << isWindowFloating(windowId)
                     << "currentFrame:" << w->frameGeometry();
    // Store pre-snap geometry before first snap (idempotent — skips if already stored).
    // The daemon handles windowSnapped/recordSnapIntent internally, but only the effect
    // knows the window's current frame geometry for pre-tile storage.
    if (!zoneId.isEmpty()) {
        // Capture frame geometry synchronously BEFORE applySnapGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        ensurePreSnapGeometryStored(w, getWindowId(w), w->frameGeometry());
    }

    // Empty zoneId = float-restore (daemon placing the window back at its pre-snap geometry, e.g.
    // autotile drag-to-float, drag-out unsnap). Non-empty zoneId = snap into a target zone. The
    // shader-tree path differs accordingly so users can give snap-in and snap-out distinct effects.
    applySnapGeometry(w, geometry, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                      zoneId.isEmpty() ? PhosphorAnimation::ProfilePaths::WindowSnapOut
                                       : PhosphorAnimation::ProfilePaths::WindowSnapIn);
    // Note: windowSnapped/recordSnapIntent are NOT called here. For daemon-driven
    // navigation, the daemon handles zone bookkeeping internally before emitting
    // applyGeometryRequested. For legacy callers (autotile float restore via
    // applyGeometryForFloat), zoneId is empty so no snap confirmation is needed.
}

void PlasmaZonesEffect::slotApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries,
                                                 const QString& action)
{
    qCInfo(lcEffect) << "applyGeometriesBatch:" << action;

    if (geometries.isEmpty()) {
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap();

    struct PendingApply
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString screenId; ///< daemon-authoritative target screen (empty = no override)
    };
    QVector<PendingApply> pending;

    for (const auto& entry : geometries) {
        if (entry.windowId.isEmpty() || entry.width <= 0 || entry.height <= 0) {
            continue;
        }

        // Exact match first, appId fallback for single-instance apps
        KWin::EffectWindow* window = windowMap.value(entry.windowId);
        if (!window) {
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(entry.windowId);
            KWin::EffectWindow* candidate = nullptr;
            int matchCount = 0;
            for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
                if (::PhosphorIdentity::WindowId::extractAppId(it.key()) == appId) {
                    candidate = it.value();
                    if (++matchCount > 1)
                        break;
                }
            }
            if (matchCount == 1) {
                window = candidate;
            }
        }

        if (!window) {
            continue;
        }

        PendingApply p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = entry.toRect();
        // Scroll-mode windows that cannot fill their tile slot (fixed-size or
        // max-constrained) are centred within it; record the resolved rect so
        // ScrollHandler can detect and correct an app-initiated resize.
        if (action == QLatin1String("scroll")) {
            p.geometry = constrainToScrollSlot(window, p.geometry);
            m_scrollHandler->recordAppliedGeometry(getWindowId(window), p.geometry);
        }
        p.screenId = entry.screenId;
        pending.append(p);
    }

    if (pending.isEmpty()) {
        return;
    }

    // Note: ensurePreSnapGeometryStored is NOT called here. Batch operations (rotate, resnap)
    // move windows between zones — their pre-tile geometry is already stored from the original
    // snap. The daemon's processBatchEntries calls clearPreTileGeometry only for __restore__
    // entries (overflow windows). Calling ensurePreSnapGeometryStored here would race with
    // the daemon's clearPreTileGeometry and store the zone geometry as pre-tile, corrupting
    // the restore path on subsequent mode transitions.

    // Capture stacking order before applying geometries (moveResize raises on Wayland)
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedStack.append(QPointer<KWin::EffectWindow>(w));
    }

    // Map the daemon's action string to a shader-tree ProfilePath. "resnap" / "retile" are layout
    // changes (different layout or autotile recompute) — semantically a layout switch. "scroll" is
    // a niri-style strip translation — its own path so the viewport-pan feel tunes independently.
    // "rotate" moves windows between existing zones in the same layout — a snap-in. Default to
    // WindowSnapIn for unknown actions (forward-compat with future daemon-emitted strings).
    QString batchProfilePath = PhosphorAnimation::ProfilePaths::WindowSnapIn;
    if (action == QLatin1String("resnap") || action == QLatin1String("retile")) {
        batchProfilePath = PhosphorAnimation::ProfilePaths::WindowLayoutSwitch;
    } else if (action == QLatin1String("scroll")) {
        batchProfilePath = PhosphorAnimation::ProfilePaths::WindowScroll;
    }

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending, batchProfilePath](int i) {
            const auto& p = pending[i];
            if (!p.window) {
                return;
            }
            // Seed the tracked-screen cache from the daemon's authoritative answer for
            // this batch BEFORE applySnapGeometry, not after. Empty screenId means the
            // daemon didn't supply an authoritative answer (e.g. autotile float-restore
            // path) — fall through to the existing geometry-based behavior in that case.
            // The pre-seed handles async follow-up frame changes; m_inDaemonGeometryApply
            // (set below) handles the synchronous frame change emitted from inside
            // applySnapGeometry, which would otherwise resolve the new position against
            // pre-rotation m_virtualScreenDefs and report a phantom cross-VS unsnap.
            if (!p.screenId.isEmpty()) {
                m_trackedScreenPerWindow[p.window] = p.screenId;
                m_autotileHandler->updateNotifiedScreen(getWindowId(p.window), p.screenId);
            }
            m_inDaemonGeometryApply = true;
            const auto guard = qScopeGuard([this] {
                m_inDaemonGeometryApply = false;
            });
            applySnapGeometry(p.window, p.geometry, /*allowDuringDrag=*/false,
                              /*skipAnimation=*/false, batchProfilePath);
        },
        [this, savedStack, action]() {
            // Restore z-order after all geometries applied
            auto* ws = KWin::Workspace::self();
            if (ws) {
                for (const auto& wPtr : savedStack) {
                    if (wPtr && !wPtr->isDeleted()) {
                        KWin::Window* kw = wPtr->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
            }
            // Show snap assist after resnap if applicable
            if (action == QLatin1String("resnap") && m_snapAssistHandler->isEnabled()) {
                KWin::EffectWindow* activeWin = getActiveWindow();
                QString activeScreenId = activeWin ? getWindowScreenId(activeWin) : QString();
                if (!activeScreenId.isEmpty() && !m_autotileHandler->isAutotileScreen(activeScreenId)) {
                    m_snapAssistHandler->showContinuationIfNeeded(activeScreenId);
                }
            }
        });
}

void PlasmaZonesEffect::slotRaiseWindowsRequested(const QStringList& windowIds)
{
    auto* ws = KWin::Workspace::self();
    if (!ws) {
        return;
    }

    for (const QString& windowId : windowIds) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (w && !w->isDeleted()) {
            KWin::Window* kw = w->window();
            if (kw) {
                ws->raiseWindow(kw);
            }
        }
    }
}

void PlasmaZonesEffect::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(::PhosphorIdentity::WindowId::extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (!w || !shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = getWindowId(w);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            // Always use EDID-based screen ID for comparison
            QString winScreen = getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"), QString(),
                                   QString(), screenId);
            return;
        }

        if (!isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = PhosphorProtocol::ClientHelpers::asyncCall(
            PhosphorProtocol::Service::Interface::Snap, QStringLiteral("calculateSnapAllWindows"),
            {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<PhosphorProtocol::SnapAllResultList> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                       QString(), QString(), screenId);
                return;
            }

            PhosphorProtocol::SnapAllResultList snapResults = calcReply.value();

            // Build WindowGeometryList for the batch geometry path
            PhosphorProtocol::WindowGeometryList snapGeometries;
            snapGeometries.reserve(snapResults.size());
            for (const auto& r : snapResults) {
                snapGeometries.append(r.toGeometryEntry());
            }
            slotApplyGeometriesBatch(snapGeometries, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon
            if (isDaemonReady("snap-all confirmation")) {
                PhosphorProtocol::SnapConfirmationList confirmEntries;
                for (const auto& r : snapResults) {
                    PhosphorProtocol::SnapConfirmationEntry entry;
                    entry.windowId = r.windowId;
                    entry.zoneId = r.targetZoneId;
                    entry.screenId = screenId;
                    entry.isRestore = false;
                    confirmEntries.append(entry);
                }
                if (!confirmEntries.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::Snap, QStringLiteral("windowsSnappedBatch"),
                        {QVariant::fromValue(confirmEntries)}, QStringLiteral("windowsSnappedBatch"));
                }
            }
        });
    });
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_daemonReadyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
                if (!appId.isEmpty()) {
                    trackedAppIds.insert(appId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedAppIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            if (!window || !shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if this window is already tracked using local set lookup (O(1))
            QString windowId = getWindowId(window);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    Q_UNUSED(screenId)
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(windowId, isFloating);
    // When a window is unfloated (tiled/snapped), clear the drag-float skip flag.
    // Without this, a subsequent float toggle's geometry restore would be skipped
    // because m_dragFloatedWindowIds still has the entry from the original drag.
    if (!isFloating) {
        m_dragFloatedWindowIds.remove(windowId);
    }
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    const QString windowId = getWindowId(w);
    const QString screenId = getWindowScreenId(w);

    // Autotile handler handles its own screens — only handle snap-mode here
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    // window.minimize shader transition. We only fire on UN-minimize
    // (forward 0→1, "appear"). The going-to-minimized direction is
    // intentionally not a shader event on the kwin-effect path: KWin
    // pulls the surface (collapses frame geometry to 0×0 / sets
    // isMinimized=true) BEFORE this signal fires, and
    // beginShaderTransition's collapsed-surface guard rejects the
    // install — the FBO allocation aborts on a 0×0 redirect target.
    // A genuine "going away" minimise animation would need an
    // unredirect-time hook that captures the last live frame before
    // KWin tears the surface down; that's out of scope for this layer.
    if (!minimized) {
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                               /*reverse=*/false);
    }

    if (minimized) {
        if (isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
            return;
        }
    }

    qCInfo(lcEffect) << "Snap: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:") << windowId
                     << "on" << screenId;

    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
            {windowId, screenId, minimized}, QStringLiteral("setWindowFloatingForScreen"));
    }
}

void PlasmaZonesEffect::slotRunningWindowsRequested()
{
    qCInfo(lcEffect) << "Running windows requested by KCM";

    QJsonArray windowArray;
    QSet<QString> seenClasses;

    // Iterate in reverse (top-to-bottom) so deduplication keeps the topmost
    // window's caption per class, which is more useful to the user
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (!w) {
            continue;
        }

        // Include all normal, non-special windows (relaxed filter for the picker)
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher() || w->isNotification()
            || w->isOnScreenDisplay() || w->isPopupWindow()) {
            continue;
        }

        QString windowClass = w->windowClass();
        if (windowClass.isEmpty()) {
            continue;
        }

        // Normalize X11 "resourceName resourceClass" to just resourceClass,
        // matching the format used by getWindowId() for app rule matching.
        int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0) {
            windowClass = windowClass.mid(spaceIdx + 1);
        }

        // Deduplicate by windowClass (first seen = topmost due to reverse iteration)
        if (seenClasses.contains(windowClass)) {
            continue;
        }
        seenClasses.insert(windowClass);

        QString appName = ::PhosphorIdentity::WindowId::deriveShortName(windowClass);
        if (appName.isEmpty()) {
            appName = windowClass;
        }

        QJsonObject obj;
        obj[QLatin1String("windowClass")] = windowClass;
        obj[QLatin1String("appName")] = appName;
        obj[QLatin1String("caption")] = w->caption();
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Settings,
                                                       QStringLiteral("provideRunningWindows"), {jsonString},
                                                       QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

} // namespace PlasmaZones
