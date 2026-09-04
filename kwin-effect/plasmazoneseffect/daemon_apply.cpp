// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "shader_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
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

#include "tilinghandler/tilinghandler.h"
#include "handlers/navigationhandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"

#include <cstdlib>

namespace PlasmaZones {

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
    // Showing-desktop guard: this activation is daemon-relayed (snap engine
    // navigation/commit) and Workspace::activateWindow on a hidden window
    // synchronously cancels a peek — see isShowingDesktop's doc for the policy.
    if (PlasmaZonesEffect::isShowingDesktop()) {
        qCDebug(lcEffect) << "slotActivateWindowRequested: dropped during show desktop" << windowId;
        return;
    }
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        KWin::effects->activateWindow(w);
    } else {
        qCDebug(lcEffect) << "slotActivateWindowRequested: window not found" << windowId;
    }
}

namespace {
/// Resolve a 1-based desktop NUMBER to its VirtualDesktop by matching
/// x11DesktopNumber(), not by position in effects->desktops().
///
/// The daemon derives every desktop number it sends from x11DesktopNumber()
/// (the bringup re-sync and the per-screen desktop reports both read it), so a
/// positional `at(desktop - 1)` is only equivalent while the list order and the
/// numbering agree. Matching the number the sender actually meant is correct
/// either way and costs one short walk. Returns nullptr when no desktop carries
/// that number.
KWin::VirtualDesktop* desktopByNumber(int desktop)
{
    if (desktop < 1 || !KWin::effects) {
        return nullptr;
    }
    const QList<KWin::VirtualDesktop*> all = KWin::effects->desktops();
    for (KWin::VirtualDesktop* vd : all) {
        if (vd && static_cast<int>(vd->x11DesktopNumber()) == desktop) {
            return vd;
        }
    }
    return nullptr;
}
} // namespace

void PlasmaZonesEffect::placeWindowWhereItIs(KWin::EffectWindow* w)
{
    // Recovery for a desktop move that cannot happen. The daemon emitted the
    // move INSTEAD of placing the window and is waiting for the re-announce the
    // arrival would have produced, so a bare refusal strands it unplaced for
    // the rest of the session. Placing it on the desktop it is already on is
    // the honest answer: the rule named somewhere that does not exist.
    if (!w || !m_snapHandler) {
        return;
    }
    m_snapHandler->armDesktopArrivalRestore(getWindowId(w));
    m_snapHandler->slotDesktopChangedRestoreArrivals();
}

void PlasmaZonesEffect::slotWindowDesktopMoveRequested(const QString& windowId, int desktop)
{
    if (desktop < 1) {
        return;
    }
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotWindowDesktopMoveRequested: window not found" << windowId;
        return;
    }
    KWin::VirtualDesktop* target = desktopByNumber(desktop);
    if (!target) {
        // Refusing the move is not the end of it. On the open path a
        // RouteToDesktop rule emits this and places nothing, expecting the
        // window to be re-announced when it lands. If the move never happens,
        // nothing re-announces it and the window stays unplaced for the rest of
        // the session. So hand it straight to the arrival restore instead,
        // which places it on the desktop it is already on. The concrete case is
        // a user who removed a virtual desktop that a rule still names.
        qCDebug(lcEffect) << "slotWindowDesktopMoveRequested: no desktop numbered" << desktop << "— placing" << windowId
                          << "where it is instead";
        placeWindowWhereItIs(w);
        return;
    }
    applyDesktopMove(w, target, windowId);
}

void PlasmaZonesEffect::slotWindowDesktopMoveByIdRequested(const QString& windowId, const QString& desktopId)
{
    if (windowId.isEmpty() || desktopId.isEmpty() || !KWin::effects) {
        return;
    }
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotWindowDesktopMoveByIdRequested: window not found" << windowId;
        return;
    }
    // Matched on the STABLE id, which is the whole point of this variant: a
    // position resolved before the D-Bus hop can name a different desktop by
    // the time it arrives, because the workspace feature renumbers alongside
    // its own moves. An id that is gone means the desktop was reaped in the
    // meantime, and dropping the move is right — there is nowhere to put it.
    KWin::VirtualDesktop* target = nullptr;
    const auto desktops = KWin::effects->desktops();
    for (KWin::VirtualDesktop* d : desktops) {
        if (d && d->id() == desktopId) {
            target = d;
            break;
        }
    }
    if (!target) {
        // Same recovery as the numbered variant: the daemon placed nothing and
        // is waiting for the re-announce this move was supposed to cause.
        qCDebug(lcEffect) << "slotWindowDesktopMoveByIdRequested: no desktop with id" << desktopId << "— placing"
                          << windowId << "where it is instead";
        placeWindowWhereItIs(w);
        return;
    }
    applyDesktopMove(w, target, windowId);
}

void PlasmaZonesEffect::applyDesktopMove(KWin::EffectWindow* w, KWin::VirtualDesktop* target, const QString& windowId)
{
    if (!w || !target || !KWin::effects) {
        return;
    }
    // A sticky (on-all-desktops) window is already present on the target; pinning
    // it to a single desktop here would silently un-sticky it. Directional
    // cross-desktop move is meaningless for an everywhere window — leave it.
    if (w->isOnAllDesktops()) {
        // No recovery call here, unlike the no-such-desktop branches above. A
        // sticky window is present on every desktop, so it was never displaced
        // and is not waiting to be placed — driving a restore at it would
        // re-place a window that went nowhere, which is the same unsolicited
        // re-placement the arrival arm in window_desktop_connections.cpp goes
        // to some length to avoid for the grew / un-stuck cases.
        qCDebug(lcEffect) << "desktop move: window is on all desktops, ignoring" << windowId;
        return;
    }
    // Single-desktop membership (not on-all-desktops) so the window genuinely
    // moves to the target.
    KWin::effects->windowToDesktops(w, {target});

    // The window has just left the desktop on screen, so nothing will place it
    // until the user goes to where it went. On a tiling or scrolling screen that
    // handler's desktop-return catch-scan re-announces it; snapping has no such
    // sweep, so park it for SnapHandler to re-drive on arrival.
    //
    // Keyed on the target not being in view rather than on WHY the move
    // happened: a RouteToDesktop rule on the open path and a cross-mode handoff
    // both land here, and the rule case leaves the window unplaced. Parking is a
    // no-op for a window that turns out to need nothing (the resolve answers
    // no-snap), so covering both is cheaper than distinguishing them.
    //
    // That breadth is safe for the move-to-next/prev-desktop shortcut too, which
    // is the other producer of this signal. Its re-snap branch emits
    // applyGeometryRequested with a zone immediately after this move, and that
    // slot calls markWindowSnapped, which cancels the park — signals on one
    // D-Bus connection keep their order, so the cancel always follows this arm.
    // Its no-equivalent-zone fallback branch emits no geometry and leaves the
    // window genuinely unplaced, which is precisely the case the park is for.
    //
    // Measured against the window's OWN output, not the global current desktop.
    // Under per-output virtual desktops (Plasma 6.7) those differ, and the
    // global reading both over-parks (the target is current somewhere else, so
    // the window really is out of view on its own output) and under-parks (the
    // target is globally current while this output shows something else).
    // Falls back to the global reading when the window has no output.
    if (m_snapHandler) {
        KWin::LogicalOutput* const out = w->screen();
        KWin::VirtualDesktop* const shownHere =
            out ? KWin::effects->currentDesktop(out) : KWin::effects->currentDesktop();
        if (shownHere != target) {
            m_snapHandler->armDesktopArrivalRestore(getWindowId(w));
        }
    }
}

void PlasmaZonesEffect::slotWindowOutputMoveRequested(const QString& windowId, const QString& targetScreenId)
{
    if (windowId.isEmpty() || targetScreenId.isEmpty()) {
        return;
    }
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotWindowOutputMoveRequested: window not found" << windowId;
        return;
    }
    KWin::LogicalOutput* output = outputForScreenId(targetScreenId);
    if (!output) {
        qCDebug(lcEffect) << "slotWindowOutputMoveRequested: unknown screen" << targetScreenId;
        return;
    }
    // windowOutput, not w->screen(): KWin can name the WRONG one of two
    // identical-model outputs (Discussion #724, see windowOutput's comment),
    // and the rest of the effect resolves a window's monitor by frame-centre
    // containment. Deciding "already there" from w->screen() early-returns on a
    // genuinely needed move, and the daemon's floating-rider leg that follows
    // this call then silently never happens.
    if (windowOutput(w) == output) {
        return; // already there (the common tracked-window case never gets here)
    }
    KWin::effects->windowToScreen(w, output);
}

void PlasmaZonesEffect::slotSetScreenDesktopRequested(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1) {
        return;
    }
    KWin::LogicalOutput* output = outputForScreenId(screenId);
    if (!output) {
        qCDebug(lcEffect) << "slotSetScreenDesktopRequested: unknown screen" << screenId;
        return;
    }
    KWin::VirtualDesktop* target = desktopByNumber(desktop);
    if (!target) {
        qCDebug(lcEffect) << "slotSetScreenDesktopRequested: no desktop numbered" << desktop;
        return;
    }
    // Per-output switch (Plasma 6.7): only THIS output changes desktop.
    //
    // Suppress the full-screen desktop.switch blend for the duration of the
    // call: this is a corrective bounce the daemon asked for (owner-wins
    // snap-back), not a switch the user made, and blending it costs two
    // output-sized GLTexture captures per output. KWin emits desktopChanged
    // from inside setCurrentDesktop on the paths that emit it at all, so the
    // scope guard drops the flag the instant the call returns; if the signal
    // were ever queued instead, the flag is already down and the blend runs as
    // it does today. EffectsHandler::setCurrentDesktop returns void, so there
    // is no status to inspect here — the report below is what makes the
    // outcome observable.
    m_programmaticDesktopSwitch = true;
    {
        const auto restoreSwitchFlag = qScopeGuard([this] {
            m_programmaticDesktopSwitch = false;
        });
        KWin::effects->setCurrentDesktop(target, output);
    }

    // FORCED report, and unconditional. The daemon's reconciler retires this
    // screen's SetCurrent ledger entry on the next screenDesktopChanged for it
    // and allows only one SetCurrent per screen in flight, so an ALREADY
    // SATISFIED request — which produces no desktopChanged at all — would
    // otherwise leave the entry pending until the daemon-side timeout, blocking
    // that screen's workspace switching and suppressing evaluateForeign for the
    // same window. Read the live value back rather than echoing `desktop`: a
    // switch KWin declined must report what actually happened, and that is the
    // only diagnosis anyone gets for the daemon's expiry warning.
    if (auto* live = KWin::effects->currentDesktop(output)) {
        const int liveDesktop = static_cast<int>(live->x11DesktopNumber());
        if (liveDesktop != desktop) {
            qCWarning(lcEffect) << "slotSetScreenDesktopRequested: setCurrentDesktop to" << desktop << "on" << screenId
                                << "left the output on" << liveDesktop;
        }
        reportScreenDesktop(output, liveDesktop, /*force=*/true);
    }
}

void PlasmaZonesEffect::slotWindowOutputMoveExpected(const QString& windowId, const QString& targetScreenId,
                                                     const QString& sourceScreenId)
{
    if (windowId.isEmpty() || targetScreenId.isEmpty()) {
        return;
    }
    // Hand the one-shot to the autotile handler: it owns the cross-output
    // outputChanged transfer path that would otherwise re-issue close/open.
    if (TilingHandler* handler = m_tilingHandler.get()) {
        handler->markExpectedOutputMove(windowId, targetScreenId, sourceScreenId);
    }
}

// slotToggleWindowFloatRequested removed — the daemon now handles float-toggle
// locally against its active-window + frame-geometry shadow and emits
// applyGeometryRequested directly. See SnapAdaptor::toggleFloatForWindow.

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, int x, int y, int width, int height,
                                                   const QString& zoneId, const QString& screenId, bool sizeOnly)
{
    // Magnitude bound BEFORE either QRect construction below. QRect's
    // x/y/w/h constructor computes `x + w - 1`, which is signed overflow and
    // undefined for a garbled payload — and it happens inside the constructor,
    // so the `isValid()` and `width > 0` guards further down run too late to be
    // a defence. Same ceilings the tile wire applies, shared from WindowTypes.h.
    // Deliberately magnitude only, not a screen-bounds test: legitimate parked
    // columns sit far outside every output.
    // Widened before the absolute value: qAbs(INT_MIN) is itself undefined in
    // int and in practice yields INT_MIN back, which is negative, so every
    // comparison here would be false and the guard would fail OPEN on exactly
    // the payload it exists to stop. Mirrors WindowGeometryEntry's validator.
    if (std::abs(static_cast<qint64>(width)) > PhosphorProtocol::MaxWireExtent
        || std::abs(static_cast<qint64>(height)) > PhosphorProtocol::MaxWireExtent
        || std::abs(static_cast<qint64>(x)) > PhosphorProtocol::MaxWireOrigin
        || std::abs(static_cast<qint64>(y)) > PhosphorProtocol::MaxWireOrigin) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: implausible geometry for" << windowId << x << y << width
                            << height << "— dropping";
        return;
    }

    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }
    // Key ALL tracking by the window's LIVE id, not the daemon-supplied one:
    // findWindowById's appId fuzzy fallback (cross-session restore where the
    // uuid changed) can resolve a window whose current id differs. Tracking
    // recorded under the stale id would never be cleared — every later
    // drag-out/float/close path uses the live id — leaving a stale tiled
    // entry and a permanently hidden title bar. Every other commit path
    // (batch, drag, snap assist) already keys by the live id.
    const QString liveWindowId = getWindowId(w);

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
            applyWindowGeometry(w, sizeOnlyGeo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                PhosphorAnimation::ProfilePaths::WindowPlaceOut);
            // Drag-out unsnap: the window left zone-managed sizing.
            m_snapHandler->clearWindowSnapped(liveWindowId);
        } else {
            // Symmetric with the non-sizeOnly invalid-geometry path below: a
            // garbled size-only payload is dropped, but log it rather than
            // failing silently.
            qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid size-only dimensions for" << windowId << width
                                << "x" << height << "— dropping";
        }
        return;
    }

    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometry;
        return;
    }
    // Consume the drag-to-float marker FIRST, before the minimized early-return can bypass
    // it. The marker is a one-shot: this float-restore event is exactly the one it was
    // waiting for, so it must be cleared whether or not we go on to apply the geometry.
    // Consuming it only on the non-minimized path left it armed when a drag-floated window
    // was minimized, and the next legitimate float-restore for that window was then wrongly
    // skipped by the stale marker.
    const bool wasDragFloated = zoneId.isEmpty() && m_dragActivation.floatedWindowIds.remove(liveWindowId);

    // Skip float-restore GEOMETRY on minimized windows: when a snapped window is minimized
    // we float it (to free the zone slot), but applying the pre-tile geometry while minimized
    // would poison what KWin restores to on unminimize, causing a visible flash of the
    // pre-snap geometry before the unfloat re-snaps to the zone.
    // A flag rather than a return, mirroring the batch twin's
    // skipMinimizedRestore: the snap-tracking discriminator below still runs
    // — the entry genuinely un-snaps the window regardless of visibility,
    // and a return here left the border set and the ZoneCache holding a
    // window that was no longer snapped (stale snapped-scoped chrome and
    // rule verdicts until an unrelated authoritative edge).
    bool skipGeometry = false;
    if (w->isMinimized() && zoneId.isEmpty()) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: skipping float-restore geometry on minimized window:"
                          << windowId;
        skipGeometry = true;
    }
    // Skip float-restore geometry for drag-to-float: when the user drags a window
    // off the autotile layout, the daemon restores pre-autotile geometry. But the
    // user expects the window to stay where they dropped it, not snap back.
    // Same flag, same reason: the un-snap bookkeeping below must still run.
    if (wasDragFloated) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:"
                         << liveWindowId;
        skipGeometry = true;
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested:" << windowId << "(live:" << liveWindowId << ") geo:" << geometry
                     << "zoneId:" << zoneId << "screen:" << screenId << "floating:" << isWindowFloating(liveWindowId)
                     << "currentFrame:" << w->frameGeometry();
    // Store pre-snap geometry before first snap (idempotent — skips if already stored).
    // The daemon handles windowSnapped/recordSnapIntent internally, but only the effect
    // knows the window's current frame geometry for pre-tile storage.
    //
    // ONLY when the window is actually MOVING into the zone: a window already at
    // the target geometry has no meaningful pre-snap rect to capture (its current
    // frame IS the zone). Without this guard, a re-apply of the zone geometry for
    // an already-snapped window — e.g. reapplyWindowAppearance() re-emitting each
    // snapped window's geometry on daemon reconnect — would store the ZONE rect as
    // the pre-tile geometry, clobbering the real pre-snap position the window
    // floats back to (Meta+F then teleports it to the zone instead of its float
    // spot). The idempotent daemon-side check normally protects this, but on a
    // daemon restart the reapply can race ahead of the disk-persisted pre-tile
    // load; the move-check makes it robust regardless of ordering.
    if (!skipGeometry && !zoneId.isEmpty() && w->frameGeometry().toRect() != geometry) {
        // Capture frame geometry synchronously BEFORE applyWindowGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        m_snapHandler->ensurePreSnapGeometryStored(w, liveWindowId, w->frameGeometry());
    }

    // Empty zoneId = float-restore (daemon placing the window back at its pre-snap geometry, e.g.
    // autotile drag-to-float, drag-out unsnap). Non-empty zoneId = snap into a target zone. The
    // shader-tree path differs accordingly so users can give snap-in and snap-out distinct effects.
    if (!skipGeometry) {
        // Same defensive pair as the batch path below and drag_end's
        // ApplySnap: pre-seed the tracked screen from the daemon's
        // authoritative answer (async follow-up frame changes), bracket the
        // apply (the synchronous one) — an ungated fire here let the
        // VS-crossing handler resolve a cross-screen apply against stale
        // state and report a phantom crossing.
        if (!screenId.isEmpty()) {
            m_trackedScreenPerWindow[w] = screenId;
            m_tilingHandler->updateNotifiedScreen(liveWindowId, screenId);
        }
        // Genuine snap commit only (same trio the tracking discriminator
        // below tests): a float-restore places FREE geometry, where KWin's
        // maximize is the user's business, and an autotile-managed screen's
        // maximize belongs to TilingHandler's own ledgers. After the
        // pre-seed above, whose coverage the demote's committed configure
        // rides; before the bracketed apply.
        const bool demoteForSnap =
            !zoneId.isEmpty() && !screenId.isEmpty() && !m_tilingHandler->isManagedScreen(screenId);
        if (demoteForSnap) {
            m_tilingHandler->demoteMaximizeForSnapPlacement(w, geometry);
        }
        // Save/restore, not set/clear (nesting-safe).
        const bool prevInApply = m_daemonGate.inGeometryApply;
        m_daemonGate.inGeometryApply = true;
        const auto applyGuard = qScopeGuard([this, prevInApply] {
            m_daemonGate.inGeometryApply = prevInApply;
        });
        applyWindowGeometry(w, geometry, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                            zoneId.isEmpty() ? PhosphorAnimation::ProfilePaths::WindowPlaceOut
                                             : PhosphorAnimation::ProfilePaths::WindowPlaceIn,
                            QRectF(), QRectF(), /*demoteMaximizeOnDeferredReplay=*/demoteForSnap);
    }
    // Track snapping's own border set (mirrors how autotile records at its
    // tile-apply) using a discriminator analogous to the batch path
    // (slotApplyGeometriesBatch). The batch path discriminates on screenId (empty =
    // float/restore) and clears any stale float marker before marking snapped; this
    // single-window path uses the empty zoneId as the float discriminator, since it
    // is only reached for explicit snap commits (which legitimately un-float) and
    // float-restores (which arrive with an empty zoneId). A window can never land in
    // both the snap and autotile border sets:
    //   - empty zoneId         → float-restore: leave snapping's set
    //   - empty/autotile screen → autotile-managed or unresolved: leave the set
    //                             (TilingHandler tracks autotile-screen windows)
    //   - snap-mode screen      → snap commit
    if (zoneId.isEmpty() || screenId.isEmpty() || m_tilingHandler->isManagedScreen(screenId)) {
        m_snapHandler->clearWindowSnapped(liveWindowId);
    } else {
        // Clear any stale float marker before marking snapped (mirrors the
        // batch path): a surviving float flag poisons the next pre-tile /
        // float-back capture and wrongly exempts the window from the
        // drain-time restore veto. Idempotent local FloatingCache write.
        m_navigationHandler->setWindowFloating(liveWindowId, false);
        m_snapHandler->markWindowSnapped(liveWindowId, screenId);
    }
    // Note: windowSnapped/recordSnapIntent are NOT called here. For daemon-driven
    // navigation, the daemon handles zone bookkeeping internally before emitting
    // applyGeometryRequested. For legacy callers (autotile float restore via
    // applyGeometryForFloat), zoneId is empty so no snap confirmation is needed.
}

void PlasmaZonesEffect::slotApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries,
                                                 const QString& action)
{
    if (geometries.isEmpty()) {
        return;
    }
    qCInfo(lcEffect) << "applyGeometriesBatch:" << action;

    QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap();

    struct PendingApply
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString screenId; ///< daemon-authoritative target screen (empty = no override)
    };
    QVector<PendingApply> pending;

    // Two-pass resolution with a claim set. Exact uuid matches claim their
    // window first; the appId fallback then resolves only entries whose
    // window no exact entry claimed, and skips already-claimed windows.
    // Without the claim set, a batch carrying BOTH a stale-uuid entry and
    // the live-uuid entry for the same app resolved both onto the one live
    // window — a double apply where the losing entry's (often empty)
    // screenId then wiped the snap tracking the winner just marked.
    QSet<KWin::EffectWindow*> claimedWindows;
    QVector<const PhosphorProtocol::WindowGeometryEntry*> needFallback;
    const auto appendPending = [&pending, &claimedWindows](const auto& entry, KWin::EffectWindow* window) {
        claimedWindows.insert(window);
        PendingApply p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = entry.toRect();
        p.screenId = entry.screenId;
        pending.append(p);
    };
    for (const auto& entry : geometries) {
        // validationError() BEFORE the size test and before any toRect(): it
        // bounds the magnitudes, and the QRect construction inside toRect()
        // overflows before a `<= 0` check can see anything. The size test stays
        // as this consumer's own policy (a zero extent is meaningless here),
        // which the shared validator deliberately leaves to the caller.
        if (const QString invalid = entry.validationError(); !invalid.isEmpty()) {
            qCWarning(lcEffect) << "slotApplyGeometriesBatch: dropping entry —" << invalid;
            continue;
        }
        if (entry.width <= 0 || entry.height <= 0) {
            qCWarning(lcEffect) << "slotApplyGeometriesBatch: dropping" << entry.windowId << "with non-positive size"
                                << entry.width << "x" << entry.height;
            continue;
        }
        if (KWin::EffectWindow* window = windowMap.value(entry.windowId)) {
            appendPending(entry, window);
        } else {
            needFallback.append(&entry);
        }
    }
    for (const auto* entryPtr : needFallback) {
        const auto& entry = *entryPtr;
        // appId fallback for single-instance apps (uuid drift across a KWin
        // restart), counting only UNCLAIMED windows so a stale sibling entry
        // can neither double-apply onto a claimed window nor trip the
        // ambiguity bail against it.
        const QString appId = ::PhosphorIdentity::WindowId::extractAppId(entry.windowId);
        KWin::EffectWindow* candidate = nullptr;
        int matchCount = 0;
        for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
            if (claimedWindows.contains(it.value())) {
                continue;
            }
            if (::PhosphorIdentity::WindowId::extractAppId(it.key()) == appId) {
                candidate = it.value();
                if (++matchCount > 1)
                    break;
            }
        }
        if (matchCount == 1) {
            appendPending(entry, candidate);
        }
    }

    if (pending.isEmpty()) {
        return;
    }

    // Note: ensurePreSnapGeometryStored is NOT called here. Rotate/resnap/
    // vs_reconfigure batches move windows between zones — their pre-tile
    // geometry is already stored from the original snap. snap_all batches
    // carry previously-UNSNAPPED windows with no per-snap capture on this
    // path; those rely on the unified placement store's open-time /
    // free-geometry capture for their float-back instead. The daemon's
    // processBatchEntries calls clearPreTileGeometry only for __restore__
    // entries (overflow windows); calling ensurePreSnapGeometryStored here
    // would race that clear and store the zone geometry as pre-tile,
    // corrupting the restore path on subsequent mode transitions.

    // Capture stacking order before applying geometries (moveResize raises on Wayland)
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedStack.append(QPointer<KWin::EffectWindow>(w));
    }

    // Map the daemon's action string to a shader-tree ProfilePath. "resnap" is a layout
    // change (different layout or autotile recompute) — semantically a layout switch. "rotate"
    // moves windows between existing zones in the same layout — a snap-in. Everything else
    // ("vs_reconfigure" via the adaptor relay, "snap_all" via the effect-local path, and any
    // future daemon-emitted string) defaults to WindowPlaceIn.
    const QString batchProfilePath = (action == QLatin1String("resnap"))
        ? PhosphorAnimation::ProfilePaths::WindowLayoutSwitch
        : PhosphorAnimation::ProfilePaths::WindowPlaceIn;

    // Per-screen supersession epoch (see m_daemonGate.batchGenByScreen): bump and
    // snapshot each target screen's counter so this cascade's still-queued
    // ticks self-cancel if a newer batch lands on the same screen. Float/
    // restore entries (empty screenId) are independent and never guarded.
    QHash<QString, uint64_t> genByScreen;
    for (const auto& p : pending) {
        if (p.screenId.isEmpty() || genByScreen.contains(p.screenId)) {
            continue;
        }
        genByScreen.insert(p.screenId, ++m_daemonGate.batchGenByScreen[p.screenId]);
    }

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending, batchProfilePath, genByScreen](int i) {
            const auto& p = pending[i];
            // Drop this apply if a newer daemon batch has superseded this
            // window's screen: a later-firing tick of this (older) cascade would
            // otherwise clobber the newer batch's position and strand the window
            // in a stale zone. Only observable with cascade stagger enabled,
            // where the per-window moves spread across timer ticks. Empty
            // screenId (float/restore) is independent and always applies.
            if (!p.screenId.isEmpty()
                && m_daemonGate.batchGenByScreen.value(p.screenId) != genByScreen.value(p.screenId)) {
                return;
            }
            // isDeleted too, not just destruction: close-shader grabs (which
            // this effect takes) keep deleted windows alive in the stacking
            // order for the close-animation duration, and the stagger delay
            // widens the race window — moving/animating a dying window would
            // also re-pollute the just-scrubbed id caches via getWindowId.
            if (!p.window || p.window->isDeleted()) {
                return;
            }
            // Seed the tracked-screen cache from the daemon's authoritative answer for
            // this batch BEFORE applyWindowGeometry, not after. Empty screenId means the
            // daemon didn't supply an authoritative answer (e.g. autotile float-restore
            // path) — fall through to the existing geometry-based behavior in that case.
            // The pre-seed handles async follow-up frame changes; m_daemonGate.inGeometryApply
            // (set below) handles the synchronous frame change emitted from inside
            // applyWindowGeometry, which would otherwise resolve the new position against
            // pre-rotation m_virtualScreenDefs and report a phantom cross-VS unsnap.
            if (!p.screenId.isEmpty()) {
                m_trackedScreenPerWindow[p.window] = p.screenId;
                m_tilingHandler->updateNotifiedScreen(getWindowId(p.window), p.screenId);
            }
            // Save/restore, not set/clear (nesting-safe).
            const bool prevInApply = m_daemonGate.inGeometryApply;
            m_daemonGate.inGeometryApply = true;
            const auto guard = qScopeGuard([this, prevInApply] {
                m_daemonGate.inGeometryApply = prevInApply;
            });
            // Minimized guard for float/restore entries (empty screenId), the
            // batch twin of slotApplyGeometryRequested's check: applying the
            // pre-tile geometry while minimized would poison what KWin
            // restores to on unminimize, causing a visible flash of the
            // pre-snap geometry before the unfloat re-snaps to the zone. The
            // snap-tracking bookkeeping below still runs — the entry
            // genuinely un-snaps the window regardless of visibility.
            const bool skipMinimizedRestore = p.screenId.isEmpty() && p.window->isMinimized();
            if (!skipMinimizedRestore) {
                // Snap placements only (the discriminator below): a non-empty
                // authoritative screenId that is not autotile-managed marks a
                // real zone commit, and a surviving KWin maximize would fight
                // its rect and arm a cross-screen restore.
                const bool demoteForSnap = !p.screenId.isEmpty() && !m_tilingHandler->isManagedScreen(p.screenId);
                if (demoteForSnap) {
                    m_tilingHandler->demoteMaximizeForSnapPlacement(p.window, p.geometry);
                }
                applyWindowGeometry(p.window, p.geometry, /*allowDuringDrag=*/false,
                                    /*skipAnimation=*/false, batchProfilePath, QRectF(), QRectF(),
                                    /*demoteMaximizeOnDeferredReplay=*/demoteForSnap);
            }
            // Snapping owns its border set (mirrors autotile). The daemon
            // supplies a non-empty authoritative screenId only for real
            // placements; an EMPTY screenId marks a float/restore entry
            // (overflow __restore__, autotile float-restore) — never a snap
            // commit. Use p.screenId directly: a current-screen fallback would
            // misclassify a float-restore as a snap and leave a stale border.
            //   - empty screenId      → float/restore: leave snapping's set
            //   - autotile-mode screen → now autotile-managed: leave snap set
            //                            (TilingHandler tracks it)
            //   - snap-mode screen     → snap commit (clears any stale float marker)
            const QString batchWid = getWindowId(p.window);
            if (p.screenId.isEmpty() || m_tilingHandler->isManagedScreen(p.screenId)) {
                m_snapHandler->clearWindowSnapped(batchWid);
            } else {
                // Real snap commit on a snap-mode screen. The daemon emits a non-empty
                // authoritative screenId ONLY for genuine placements; float/restore
                // entries carry an EMPTY screenId and are handled above. So this window
                // is being snapped and is no longer floating — even if the effect's
                // float cache is stale (e.g. a window snapped straight from a
                // floated-in-autotile state via the daemon's windowsReleased snap-zone
                // restore). Clear the stale float marker: a surviving float flag
                // poisons the next pre-tile/float-back capture (the zone rect would be
                // saved as the "free" geometry) and wrongly exempts the window from
                // the drain-time restore veto. setWindowFloating is an idempotent
                // local FloatingCache write (no signal/D-Bus), so it is called
                // unconditionally — no need to read-guard a no-op overwrite.
                m_navigationHandler->setWindowFloating(batchWid, false);
                m_snapHandler->markWindowSnapped(batchWid, p.screenId);
            }
        },
        [this, savedStack, action, genByScreen]() {
            // Restore z-order after all geometries applied — but skip it when a
            // newer batch has superseded every screen this one targeted. The
            // superseding cascade captured and re-asserts the current stacking
            // order itself; replaying this batch's stale savedStack would
            // shuffle windows into a pre-supersession order. Snap-assist below
            // stays unconditional: for the non-resnap batches (rotate,
            // vs_reconfigure, snap_all) it is a no-op, and a superseded resnap
            // is still safe because the superseding resnap re-evaluates snap
            // assist itself.
            bool fullySuperseded = !genByScreen.isEmpty();
            for (auto it = genByScreen.constBegin(); it != genByScreen.constEnd(); ++it) {
                if (m_daemonGate.batchGenByScreen.value(it.key()) == it.value()) {
                    fullySuperseded = false;
                    break;
                }
            }
            auto* ws = fullySuperseded ? nullptr : KWin::Workspace::self();
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
            // Show snap assist after resnap if applicable.
            //
            // A resnap is a bulk operation (autotile→snap toggle, rotate,
            // vs-reconfigure) — not a per-window snap — so the continuation is
            // anchored to the active window: snap assist shows ONLY if the
            // resnap actually placed the active window in a zone. Passing its
            // windowId as the anchor makes showContinuationIfNeeded gate on
            // "this window is snapped", which also guarantees at least one
            // zone is occupied. Without the anchor, a resnap that snapped
            // nothing (e.g. toggling to snap mode with no prior assignments)
            // left every zone empty and popped snap assist for all of them.
            if (action == QLatin1String("resnap") && m_snapAssistHandler->isEnabled()) {
                KWin::EffectWindow* activeWin = getActiveWindow();
                QString activeScreenId = activeWin ? getWindowScreenId(activeWin) : QString();
                if (activeWin && !activeScreenId.isEmpty() && !m_tilingHandler->isManagedScreen(activeScreenId)) {
                    m_snapAssistHandler->showContinuationIfNeeded(activeScreenId, getWindowId(activeWin));
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

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    // Re-key to the window's LIVE id up front, mirroring slotWindowStateChanged
    // and slotApplyGeometryRequested: every effect-side cache below
    // (FloatingCache, drag-float skip set, snap tracking, rule-cache
    // invalidation) is keyed by the id the effect claimed with, so after a
    // cross-session uuid drift the daemon-supplied id would write entries the
    // rule matcher never reads. Preserve the daemon id when the exact instance
    // is unresolvable (ambiguous fuzzy hit) — same policy as the minimize
    // handlers below.
    bool stillMinimized = false;
    QString liveWindowId = windowId;
    KWin::EffectWindow* liveForOwner = nullptr;
    if (KWin::EffectWindow* live = findWindowByIdExact(windowId)) {
        stillMinimized = live->isMinimized();
        liveWindowId = getWindowId(live);
        liveForOwner = live;
    } else if (KWin::EffectWindow* fuzzy = findWindowById(windowId)) {
        // A same-app window exists but the exact instance is not
        // resolvable (drifted or ambiguous id). Default to PRESERVING
        // ownership: wrongly dropping a still-minimized window's marker
        // strands its unminimize, while wrongly keeping it is healed by
        // the next authoritative edge. Keep the fuzzy hit as the OWNER
        // resolve fallback too: with liveForOwner null and an empty
        // daemon-supplied screenId, the dual-hold repair below would
        // resolve the conflict to SNAP unconditionally instead of to the
        // window's actual screen mode.
        stillMinimized = true;
        liveForOwner = fuzzy;
    }
    m_navigationHandler->setWindowFloating(liveWindowId, isFloating);
    // This slot receives the WindowTracking interface's float signal, which
    // carries floats from every producer (the scroll passive channel's
    // windowFloatingStateSynced among them) and never reaches
    // TilingHandler::slotWindowFloatingChanged, so it never runs
    // applyFloatCleanup. The shed helper mirrors applyFloatCleanup's full
    // shed half — windowed-fullscreen release, clear-in-flight marker,
    // counter-assert rect, centering targets, parked paint hint (with
    // damage), decoration re-drive — not just the fullscreen drop; each of
    // those was otherwise silently bypassed on this channel. For the ACTIVE
    // channel (the Tiling interface's signal) the tiling handler's slot
    // performed the cleanup first and every remove in the shed is a no-op
    // belt. The release helper carries its own suppress counter and
    // inGeometryApply bracket, so the synchronous X11 exit signal cannot
    // re-enter the VS-crossing machinery from here.
    if (isFloating) {
        // Flip the snap facts BEFORE the shed: applyPassiveFloatShed can run
        // reconcileDecorationOnPlacementFlip, whose contract (decorations.cpp)
        // requires callers to flip engine facts first so the resolve sees the
        // new state. clearWindowSnapped drops the ZoneCache entry backing the
        // IsSnapped/Zone rule facts; it is also the backstop for float paths
        // that don't emit applyGeometryRequested with an empty zoneId (e.g. a
        // float toggle with no stored pre-tile geometry) or windowStateChanged
        // with an empty zone. Idempotent when the window wasn't snap-tracked.
        m_snapHandler->clearWindowSnapped(liveWindowId);
        m_tilingHandler->applyPassiveFloatShed(liveWindowId);
    }
    // When a window is unfloated (tiled/snapped), clear the drag-float skip flag.
    // Without this, a subsequent float toggle's geometry restore would be skipped
    // because m_dragActivation.floatedWindowIds still has the entry from the original drag.
    if (!isFloating) {
        m_dragActivation.floatedWindowIds.remove(liveWindowId);
        // A visible external unfloat normally moots any deferred recovery in
        // either engine. The daemon signal has no request generation, though,
        // so an older hidden-unfloat echo can arrive after the window becomes
        // visible. Preserve ownership while a recovery timer or confirmation
        // query is active; that query provides the terminal daemon state.
        // Mode teardown and a hidden snap-zone commit can legitimately clear
        // the daemon's live float while the window is still minimized. The
        // float is only the daemon-side occupancy mechanism; the effect-side
        // marker still owns the later unminimize transition. Dropping it here
        // loses that edge and leaves KWin restoring whichever geometry was last
        // applied by the old mode. Genuine visible unfloats still clear both
        // handlers below.
        // liveWindowId / stillMinimized were resolved at the top of the
        // function (the handler maps are keyed by the effect-side ids they
        // claimed with).
        const bool recoveryPending = m_tilingHandler->hasPendingUnminimizeUnfloat(liveWindowId)
            || m_snapHandler->hasPendingUnminimizeUnfloat(liveWindowId)
            || m_tilingHandler->hasUnfloatInFlight(liveWindowId) || m_snapHandler->hasUnfloatInFlight(liveWindowId);
        if (stillMinimized || recoveryPending) {
            qCDebug(lcEffect) << "Preserving minimize-float ownership across non-terminal unfloat:" << liveWindowId;
            // Single-owner repair: preservation must never leave the marker in
            // BOTH engines (a missed cross-mode adoption hop can). Resolve a
            // dual hold to the window's current screen mode — the same rule
            // every adoption site applies.
            if (m_tilingHandler->isMinimizeFloated(liveWindowId) && m_snapHandler->isMinimizeFloated(liveWindowId)) {
                QString ownerScreen = screenId;
                if (ownerScreen.isEmpty() && liveForOwner) {
                    ownerScreen = getWindowScreenId(liveForOwner);
                }
                qCWarning(lcEffect) << "Minimize-float marker held by BOTH engines for" << liveWindowId
                                    << "— resolving to owner of screen" << ownerScreen;
                if (m_tilingHandler->isManagedScreen(ownerScreen)) {
                    m_snapHandler->removeMinimizeFloated(liveWindowId);
                } else {
                    m_tilingHandler->removeMinimizeFloated(liveWindowId);
                }
            }
        } else {
            m_tilingHandler->removeMinimizeFloated(liveWindowId);
            m_snapHandler->removeMinimizeFloated(liveWindowId);
        }
    } else {
        // clearWindowSnapped for this branch ran above, before the shed.

        // Invalidate any stale instant-restore entry for this app. The snap
        // restore cache (SnapHandler) is a single-shot latency cache populated at
        // daemon-ready from the daemon's pending restores. Once a window
        // floats, its saved zone no longer applies: windowClosed() will NOT
        // persist a PendingRestore for a floating window (it should reopen
        // floating). But a stale cache entry would still "Instant snap restore"
        // the reopened window into its old zone WITHOUT a daemon commit
        // (resolveWindowRestore finds nothing), leaving a ghost — visually
        // snapped but untracked. Dropping the entry makes the reopen take the
        // authoritative daemon path so the window stays floating. Keyed by
        // appId to survive the window's identity change across close/reopen.
        m_snapHandler->invalidateRestore(::PhosphorIdentity::WindowId::extractAppId(liveWindowId));
    }
    // The change-gated write above (setWindowFloating, plus the zone-cache
    // clear that now runs inside clearWindowSnapped on the floating branch)
    // re-resolves this window's rules only when a match field actually flips.
    // That is not enough on the cross-monitor drag-out path: the cross-screen
    // handoff pre-sets the floating / zone caches while the window is still
    // moving, so by the time this
    // authoritative windowFloatingChanged arrives both writes are no-ops and the
    // hide-title-bar / border reconcile never runs — the floated window keeps its
    // snap chrome (hidden title bar), ignoring the show-title-bar-when-floating rule.
    // This signal IS the authoritative placement-state change, so reconcile
    // unconditionally; invalidateRuleCacheForStateChange coalesces to a single flush
    // per event-loop turn, so the redundant call when a write did flip the cache is
    // cheap. Mirrors the drag-end paths, which invalidate directly and unconditionally.
    invalidateRuleCacheForStateChange(liveWindowId);
}

void PlasmaZonesEffect::slotWindowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& state)
{
    // Validate the daemon payload at the boundary, mirroring the other
    // daemon-data slots (DragPolicy / BridgeRegistrationResult). A garbled entry
    // naming no window must not write a zone keyed by an empty/garbage id.
    if (const QString err = state.validationError(); !err.isEmpty()) {
        qCWarning(lcEffect) << "slotWindowStateChanged: rejecting invalid entry —" << err;
        return;
    }
    // Re-key to the window's LIVE id before writing, not the daemon-supplied one.
    //
    // ZoneCache keys on the instance UUID (extractInstanceId), and so does the IsSnapped /
    // Zone rule-match READ — but through a cross-session restore the daemon can still hold
    // the pre-restore UUID, so a write keyed on it files the entry under an instance id the
    // rule matcher will never read, and a restored snapped window's IsSnapped / Zone(...)
    // rules silently stop matching. Every other commit path already keys by the live id
    // (slotApplyGeometryRequested spells out why). A window that cannot be resolved falls
    // back to the daemon id — best-effort, and correct for the ordinary same-session case
    // where the two ids are identical.
    QString liveWindowId = windowId;
    if (KWin::EffectWindow* const w = findWindowById(windowId)) {
        liveWindowId = getWindowId(w);
    }
    // Keep the effect-side zone cache current so the IsSnapped / Zone rule-match
    // fields resolve against the live placement. An empty zoneId (unsnapped /
    // floated / screen-changed) removes the entry. setWindowZone re-resolves this
    // window's rules when the zone actually changes (coalescing with the floating
    // path's invalidation when a float toggle emits both signals — see
    // flushPendingRuleInvalidations), so no separate invalidate call is needed.
    m_navigationHandler->setWindowZone(liveWindowId, state.zoneId);
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const bool minimized = w->isMinimized();

    // window.minimize shader transition fires for EVERY window the
    // animation filter admits — like the open / close / focus events, it
    // gates only on `shouldAnimateWindow` (enforced inside
    // tryBeginShaderForEvent), NOT on the tiling filters below. A window
    // excluded from tiling (min-size, exclusion rule) still animates its
    // other lifecycle events, so it must animate minimize too. It also
    // fires for BOTH snap and autotile screens — the shader event is
    // screen-mode-independent and the autotile handler's own
    // minimised-change slot does not fire it (which would otherwise be
    // asymmetric per-screen UX for the same user-configured
    // "WindowMinimize" event).
    //
    // Un-minimize plays forward (0→1, "appear"); going-to-minimized plays
    // the same shader in reverse (1→0, "going away"), matching the
    // close / unmaximize reverse-leg convention.
    //
    // The going-to-minimized direction was historically skipped on the
    // claim that KWin pulls the surface (collapses the frame to 0×0)
    // before this signal fires. That was a misdiagnosis: minimizing keeps
    // the frame geometry and the last committed buffer intact and only
    // disables painting via PAINT_DISABLED_BY_MINIMIZE, which
    // beginShaderTransition lifts with an EffectWindowVisibleRef for the
    // transition's lifetime (the mechanism KWin's own Magic Lamp / Squash
    // minimize effects use).
    // animateMinimized opts the going-to-minimized leg past the
    // begin-side minimized-window reject; the un-minimize leg runs on a
    // visible window where the flag is moot.
    //
    // Spurious-pair suppression: plasmashell notification stacking makes
    // KWin emit minimizedChanged(true) on tiled windows with the matching
    // unminimize ~1-2 ms later (the same quirk kMinimizeFloatDebounceMs
    // in tilinghandler/minimizefloat.cpp debounces on the float side). The
    // reverse leg installs immediately — a genuine minimize must not
    // start late, and a spurious pair paints at most one barely-started
    // frame — but an unminimize landing inside the window means the pair
    // was noise: silently drop the reverse leg instead of superseding it
    // with a full un-minimize replay, which with an icon pack made every
    // tiled window pour out of its taskbar icon on every notification.
    if (minimized) {
        // Stamp only a leg that is provably OURS, identified by generation.
        // tryBeginShaderForEvent can install nothing (no pack assigned,
        // null-shader sentinel, animation filter), and stamping blindly
        // would let a spurious pair kill an unrelated leg mid-animation —
        // a future reverse leg, or a superseding one. A fresh install is
        // detected by the generation changing; a repeated minimize event
        // whose prior minimize leg is still live (same-effect
        // short-circuit) refreshes the stamp's timestamp while keeping
        // the same generation.
        const auto* pre = m_shaderManager.findTransition(w);
        const quint64 preGeneration = pre ? pre->generation : 0;
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                               /*reverse=*/true, /*holdCloseGrab=*/false, /*holdAddedGrab=*/false,
                               /*animateMinimized=*/true);
        if (const auto* post = m_shaderManager.findTransition(w); post && post->reverse) {
            const auto stampIt = m_minimizeShaderStamp.constFind(w);
            const bool freshInstall = post->generation != preGeneration;
            const bool ourLiveLeg =
                stampIt != m_minimizeShaderStamp.constEnd() && stampIt->generation == post->generation;
            if (freshInstall || ourLiveLeg) {
                // Stamp with a FRESH clock sample, not one taken at slot
                // entry: a cold-cache install compiles the pack on this
                // thread (tens of ms for a heavy shader), and the paired
                // spurious unminimize measures its gap from ITS slot
                // entry — an entry-time stamp would inflate the measured
                // gap by the compile cost and let the session's first
                // notification-induced pair escape suppression.
                m_minimizeShaderStamp.insert(w, {ShaderInternal::shaderClockNowMs(), post->generation});
            }
        }
    } else {
        const qint64 nowMs = ShaderInternal::shaderClockNowMs();
        const MinimizeShaderStamp stamp = m_minimizeShaderStamp.take(w);
        const bool spuriousPair = stamp.timeMs > 0 && nowMs - stamp.timeMs < kSpuriousMinimizePairMs;
        // Only the exact leg we stamped is ours to drop — the generation
        // check keeps a superseding leg (or anything else live on the
        // window) on its own teardown. Liveness is part of the SUPPRESSION
        // decision, not just the teardown: when our stamped leg is no
        // longer live, the earlier shape skipped BOTH the teardown and the
        // forward leg, so a fast genuine minimize/unminimize cycle got no
        // unminimize animation at all.
        const auto* st = m_shaderManager.findTransition(w);
        const bool stampedLegLive = st && st->reverse && st->generation == stamp.generation;
        if (spuriousPair && stampedLegLive) {
            endShaderTransition(w);
        } else {
            tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                                   /*reverse=*/false);
        }
    }

    // Snap-mode-only minimize→float bookkeeping is owned by SnapHandler
    // (mirrors TilingHandler running its own minimize→float machine for
    // autotile screens). Unlike the shader event above, this state
    // machine only concerns windows the tiling system manages.
    if (!shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    m_snapHandler->handleMinimizeChanged(w, getWindowId(w), getWindowScreenId(w), minimized);
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
        // !isDeleted: a window mid-close-animation must not be offered in
        // the rule picker (same stacking-walk hygiene as the other walks).
        if (!w || w->isDeleted()) {
            continue;
        }

        // Include all normal, non-special windows (relaxed filter for the picker).
        // isCriticalNotification is a distinct KWin window type from isNotification,
        // so both must be rejected — a window flagged only critical-notification
        // would otherwise show up in the app picker.
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher() || w->isNotification()
            || w->isCriticalNotification() || w->isOnScreenDisplay() || w->isPopupWindow()) {
            continue;
        }

        QString windowClass = w->windowClass();
        if (windowClass.isEmpty()) {
            continue;
        }

        // Hide the daemon's own overlay / editor windows from the rule picker —
        // surfacing `plasmazonesd` or `plasmazones-editor` as an authoring
        // target invites users to write rules against the very surfaces that
        // implement the rule engine. The settings app windowClass falls
        // outside `isOwnOverlayClass` so it stays pickable.
        if (isOwnOverlayClass(windowClass)) {
            continue;
        }

        // Normalize X11 "resourceName resourceClass" to just resourceClass
        // (lowercased), matching the canonical form `normalizeAppId` produces
        // for every other appId entry point — getWindowId, rule evaluators,
        // pending-restore prune patterns. An inline first-space split would
        // drift in two ways: (1) a three-token resource string like
        // "foo bar baz" yields "bar baz" here but "baz" through
        // normalizeAppId; (2) case is preserved here but lowercased
        // downstream — a rule the user authors against the picker's
        // case-preserved output with `Equals` would silently never match.
        windowClass = ::PhosphorIdentity::WindowId::normalizeAppId(QString(), windowClass);
        if (windowClass.isEmpty()) {
            continue;
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

        // Pull the desktop-file basename from the underlying KWin::Window
        // (EffectWindow doesn't expose desktopFileName directly). Empty
        // for windows without a registered desktop file — the rule picker
        // hides the entry from its DesktopFile mode in that case.
        QString desktopFile;
        if (KWin::Window* kw = w->window()) {
            desktopFile = kw->desktopFileName();
        }

        QJsonObject obj;
        obj[QLatin1String("windowClass")] = windowClass;
        obj[QLatin1String("appName")] = appName;
        obj[QLatin1String("caption")] = w->caption();
        obj[QLatin1String("desktopFile")] = desktopFile;
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    if (m_daemonGate.serviceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Settings,
                                                       QStringLiteral("provideRunningWindows"), {jsonString},
                                                       QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

} // namespace PlasmaZones
