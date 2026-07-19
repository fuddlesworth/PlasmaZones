// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// toggleFloatForWindow, calculateUnfloatRestore, windowUnsnappedForFloat
// moved to SnapAdaptor (src/dbus/snapadaptor/commit.cpp).
//
// This file retains cross-mode float methods that stay on WTA:
//   notifyDragOutUnsnap, getPreFloatZone, clearPreFloatZone,
//   isWindowFloating, queryWindowFloating, setWindowFloating (WTS delegate),
//   getFloatingWindows, applyGeometryForFloat, setWindowFloatingForScreen.

#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/PlacementEngineBase.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::notifyDragOutUnsnap(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("drag-out unsnap"))) {
        return;
    }

    QString zoneId = m_service->zoneForWindow(windowId);
    if (zoneId.isEmpty()) {
        // Window was not snapped — nothing to do
        return;
    }

    QString screenId = m_service->screenForWindow(windowId, m_lastActiveScreenId);
    qCInfo(lcDbusWindow) << "Drag-out unsnap (no activation trigger) for" << windowId << "screen:" << screenId;

    // Delegate unsnap-for-float to the service directly (the SnapAdaptor
    // method windowUnsnappedForFloat does the same thing, but this path
    // doesn't need the SnapAdaptor detour for a WTS-level operation).
    m_service->unsnapForFloat(windowId);
    setWindowFloating(windowId, true);

    // Restore pre-snap size (not position — window stays where the user dropped it).
    // This mirrors the activated-drag path in WindowDragAdaptor::dragStopped. A
    // matched SetRestoreSizeOnUnsnap rule overrides the global setting per window.
    if (shouldRestoreSizeOnUnsnap(windowId)) {
        auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
        if (geo) {
            Q_EMIT applyGeometryRequested(windowId, 0, 0, geo->width(), geo->height(), QString(), screenId, true);
            // Single float-back store: clear the record's shared free geometry now
            // that we've consumed it for this drag-out restore.
            m_service->clearFreeGeometry(windowId);
            qCInfo(lcDbusWindow) << "Drag-out unsnap: restoring size" << geo->width() << "x" << geo->height();
        }
    }
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneId)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getPreFloatZone: empty windowId";
        zoneId.clear();
        return false;
    }
    // Delegate to service
    zoneId = m_service->preFloatZone(windowId);
    qCDebug(lcDbusWindow) << "getPreFloatZone for" << windowId << "-> found:" << !zoneId.isEmpty() << "zone:" << zoneId;
    return !zoneId.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Only log if there was something to clear
    bool hadPreFloatZone = !m_service->preFloatZone(windowId).isEmpty();
    // Delegate to service
    m_service->clearPreFloatZone(windowId);
    if (hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
    }
}

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    return isWindowFloating(windowId);
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float state"))) {
        return;
    }
    m_service->setWindowFloating(windowId, floating);
    // Gate the signal emissions on a real change in what we last BROADCAST, not
    // on a re-query of the service's float state. Under the per-engine float
    // model the owning engine flips its float bit BEFORE the daemon's sync slot
    // reaches this writer (e.g. AutotileEngine::performToggleFloat toggles then
    // emits windowFloatingChanged, which Daemon::syncAutotileFloatState relays
    // here), so m_service->isWindowFloating() already reports the post-transition
    // value — the old `floating == wasFloating` gate then suppressed every
    // autotile float broadcast, leaving subscribers (the effect's float cache,
    // which the autotile FFM float-pause depends on) permanently stale.
    // The gate itself lives in relayWindowFloatingChanged (the single owner of
    // the last-broadcast contract); the unified state change and placement
    // capture below ride the same edge.
    // Use the window's tracked screen if available, otherwise fall back to last
    // active screen.
    const QString screen = m_service->screenForWindow(windowId, m_lastActiveScreenId);
    if (!relayWindowFloatingChanged(windowId, floating, screen)) {
        return;
    }
    qCInfo(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");

    // Emit unified state change
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{
                                  windowId,
                                  m_service->zoneForWindow(windowId),
                                  screen,
                                  floating,
                                  floating ? QStringLiteral("floated") : QStringLiteral("unfloated"),
                                  QStringList{},
                                  false,
                              });

    // Unified model: refresh the live placement record on float toggle.
    captureWindowPlacement(windowId);
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // Delegate to service
    return m_service->floatingWindows();
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(lcDbusWindow) << "applyGeometryForFloat: windowId=" << windowId << "geo=" << *geo
                             << "screen=" << screenId;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }

    qCInfo(lcDbusWindow) << "applyGeometryForFloat: no geometry found for" << windowId;
    return false;
}

// WindowTrackingAdaptor::clearFloatingStateForSnap was removed — all
// snap-commit paths now route through PhosphorSnapEngine::SnapEngine::commitSnap
// which handles clearing the floating state internally and emits
// windowFloatingClearedForSnap, which this adaptor relays to its own
// windowFloatingChanged D-Bus signal in the constructor wiring.

bool WindowTrackingAdaptor::relayWindowFloatingChanged(const QString& windowId, bool floating, const QString& screenId)
{
    // The single owner of the last-broadcast dedup contract — see the header
    // doc: every emission channel must keep m_broadcastFloating equal to what
    // subscribers last heard, or the gate turns from a dedup into a
    // suppressor of genuine changes. setWindowFloating delegates here too.
    if (m_broadcastFloating.value(windowId, false) == floating) {
        return false;
    }
    m_broadcastFloating[windowId] = floating;
    Q_EMIT windowFloatingChanged(windowId, floating, screenId);
    return true;
}

void WindowTrackingAdaptor::setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float for screen"))) {
        return;
    }

    qCInfo(lcDbusWindow) << "setWindowFloatingForScreen: windowId=" << windowId << "floating=" << floating
                         << "screen=" << screenId;

    // Route to the correct engine based on screen mode. Both directions go
    // through the explicit cross-engine handoff contract when the window
    // isn't yet tracked by the destination engine.
    PhosphorEngine::PlacementEngineBase* dest = nullptr;
    PhosphorEngine::PlacementEngineBase* source = nullptr;
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        dest = m_autotileEngine.data();
        source = m_snapEngine.data();
    } else if (m_snapEngine) {
        dest = m_snapEngine.data();
        source = m_autotileEngine.data();
    }

    // Float: adopt an untracked window unconditionally (a brand-new floating
    // dialog is a valid target — fromEngineId stays empty when neither side
    // tracks it, so receive-side reasoning that depends on the source mode
    // correctly degrades).
    // Unfloat of a window whose float bit lives in the OTHER engine: without
    // handling, the unfloat dead-ends — setWindowFloat below targets the
    // destination engine, which doesn't track the window and early-returns,
    // while the source engine's float bit stays set with no broadcast, so
    // the window reads floating through its own context's mode lens forever.
    // The two destinations need DIFFERENT handling:
    //   - Autotile dest: adopt via the handoff (release source, receive with
    //     wasFloating=false tiles the arrival and announces it on the
    //     passive sync channel); the trailing relay dedups against that.
    //   - Snap dest: NO adoption. Snap's handoffReceive floats an arrival
    //     with no sourceZoneIds unconditionally (ignoring wasFloating) and
    //     emits floating=true, and the setWindowFloat(false) below fails
    //     open when no rule/pre-float zone resolves ("keeping floating") —
    //     together that strands the snap store floating against a false
    //     broadcast. Instead just release the source's bit and broadcast
    //     the unfloat; the setWindowFloat below still gives snap its
    //     rule/pre-float re-snap chance, and a window it cannot re-snap
    //     stays a free (unmanaged) window, which is what unfloating a
    //     window snap never tracked means.
    bool recaptureAfterFloatWrite = false;
    if (dest && !dest->isWindowTracked(windowId)) {
        const bool sourceTracked = source && source->isWindowTracked(windowId);
        const bool destIsAutotile = m_autotileEngine && dest == m_autotileEngine.data();
        if (floating || (sourceTracked && destIsAutotile)) {
            PhosphorEngine::IPlacementEngine::HandoffContext ctx;
            ctx.windowId = windowId;
            ctx.toScreenId = screenId;
            ctx.wasFloating = floating;
            if (sourceTracked) {
                ctx.fromEngineId = source->engineId();
                ctx.sourceGeometry = m_frameGeometry.value(windowId);
                source->handoffRelease(windowId);
            }
            dest->handoffReceive(ctx);
            if (!floating) {
                relayWindowFloatingChanged(windowId, false, screenId);
            }
        } else if (sourceTracked) {
            source->handoffRelease(windowId);
            relayWindowFloatingChanged(windowId, false, screenId);
            recaptureAfterFloatWrite = true;
        }
    }

    if (dest) {
        // Thread the effect's authoritative live screen so the engine resolves
        // this float/unfloat against the window's REAL current monitor, not its
        // (possibly stale) tracked association. Without this, unfloating a window
        // that drifted to another monitor while floating non-deterministically
        // teleports it back to its source-monitor zone (Discussion #724).
        dest->setWindowFloat(windowId, floating, screenId);
    }
    if (recaptureAfterFloatWrite) {
        // Snap-dest unfloat: when unfloatToZone above re-snapped the window,
        // the commitSnap wiring already refreshed the live placement record —
        // this recapture is then an idempotent repeat. When it FAILED OPEN
        // (no rule/pre-float zone) nothing else refreshes the record, and it
        // would keep its prior floating slot while the broadcast above said
        // not-floating; capture after the write so the persisted record
        // matches what subscribers were told.
        captureWindowPlacement(windowId);
    }
}

} // namespace PlasmaZones
