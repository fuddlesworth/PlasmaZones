// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "shader_internal.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
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
#include "../snapassisthandler.h"
#include "../snaphandler.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

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
    const QList<KWin::VirtualDesktop*> all = KWin::effects->desktops();
    if (desktop > all.size()) {
        qCDebug(lcEffect) << "slotWindowDesktopMoveRequested: desktop" << desktop << "out of range, have" << all.size();
        return;
    }
    // A sticky (on-all-desktops) window is already present on the target; pinning
    // it to a single desktop here would silently un-sticky it. Directional
    // cross-desktop move is meaningless for an everywhere window — leave it.
    if (w->isOnAllDesktops()) {
        qCDebug(lcEffect) << "slotWindowDesktopMoveRequested: window is on all desktops, ignoring" << windowId;
        return;
    }
    // 1-based desktop → the matching VirtualDesktop. Single-desktop membership
    // (not on-all-desktops) so the window genuinely moves to the target.
    KWin::effects->windowToDesktops(w, {all.at(desktop - 1)});
}

void PlasmaZonesEffect::slotWindowOutputMoveExpected(const QString& windowId, const QString& targetScreenId)
{
    if (windowId.isEmpty() || targetScreenId.isEmpty()) {
        return;
    }
    // Hand the one-shot to the autotile handler: it owns the cross-output
    // outputChanged transfer path that would otherwise re-issue close/open.
    if (AutotileHandler* handler = m_autotileHandler.get()) {
        handler->markExpectedOutputMove(windowId, targetScreenId);
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
                                PhosphorAnimation::ProfilePaths::WindowSnapOut);
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
    const bool wasDragFloated = zoneId.isEmpty() && m_dragFloatedWindowIds.remove(liveWindowId);

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
    if (wasDragFloated) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:"
                         << liveWindowId;
        return;
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
    if (!zoneId.isEmpty() && w->frameGeometry().toRect() != geometry) {
        // Capture frame geometry synchronously BEFORE applyWindowGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        m_snapHandler->ensurePreSnapGeometryStored(w, liveWindowId, w->frameGeometry());
    }

    // Empty zoneId = float-restore (daemon placing the window back at its pre-snap geometry, e.g.
    // autotile drag-to-float, drag-out unsnap). Non-empty zoneId = snap into a target zone. The
    // shader-tree path differs accordingly so users can give snap-in and snap-out distinct effects.
    applyWindowGeometry(w, geometry, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                        zoneId.isEmpty() ? PhosphorAnimation::ProfilePaths::WindowSnapOut
                                         : PhosphorAnimation::ProfilePaths::WindowSnapIn);
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
    //                             (AutotileHandler tracks autotile-screen windows)
    //   - snap-mode screen      → snap commit
    if (zoneId.isEmpty() || screenId.isEmpty() || m_autotileHandler->isAutotileScreen(screenId)) {
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
        p.screenId = entry.screenId;
        pending.append(p);
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
    // future daemon-emitted string) defaults to WindowSnapIn.
    const QString batchProfilePath = (action == QLatin1String("resnap"))
        ? PhosphorAnimation::ProfilePaths::WindowLayoutSwitch
        : PhosphorAnimation::ProfilePaths::WindowSnapIn;

    // Per-screen supersession epoch (see m_daemonBatchGenByScreen): bump and
    // snapshot each target screen's counter so this cascade's still-queued
    // ticks self-cancel if a newer batch lands on the same screen. Float/
    // restore entries (empty screenId) are independent and never guarded.
    QHash<QString, uint64_t> genByScreen;
    for (const auto& p : pending) {
        if (p.screenId.isEmpty() || genByScreen.contains(p.screenId)) {
            continue;
        }
        genByScreen.insert(p.screenId, ++m_daemonBatchGenByScreen[p.screenId]);
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
            if (!p.screenId.isEmpty() && m_daemonBatchGenByScreen.value(p.screenId) != genByScreen.value(p.screenId)) {
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
            // The pre-seed handles async follow-up frame changes; m_inDaemonGeometryApply
            // (set below) handles the synchronous frame change emitted from inside
            // applyWindowGeometry, which would otherwise resolve the new position against
            // pre-rotation m_virtualScreenDefs and report a phantom cross-VS unsnap.
            if (!p.screenId.isEmpty()) {
                m_trackedScreenPerWindow[p.window] = p.screenId;
                m_autotileHandler->updateNotifiedScreen(getWindowId(p.window), p.screenId);
            }
            m_inDaemonGeometryApply = true;
            const auto guard = qScopeGuard([this] {
                m_inDaemonGeometryApply = false;
            });
            applyWindowGeometry(p.window, p.geometry, /*allowDuringDrag=*/false,
                                /*skipAnimation=*/false, batchProfilePath);
            // Snapping owns its border set (mirrors autotile). The daemon
            // supplies a non-empty authoritative screenId only for real
            // placements; an EMPTY screenId marks a float/restore entry
            // (overflow __restore__, autotile float-restore) — never a snap
            // commit. Use p.screenId directly: a current-screen fallback would
            // misclassify a float-restore as a snap and leave a stale border.
            //   - empty screenId      → float/restore: leave snapping's set
            //   - autotile-mode screen → now autotile-managed: leave snap set
            //                            (AutotileHandler tracks it)
            //   - snap-mode screen     → snap commit (clears any stale float marker)
            const QString batchWid = getWindowId(p.window);
            if (p.screenId.isEmpty() || m_autotileHandler->isAutotileScreen(p.screenId)) {
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
                if (m_daemonBatchGenByScreen.value(it.key()) == it.value()) {
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
                if (activeWin && !activeScreenId.isEmpty() && !m_autotileHandler->isAutotileScreen(activeScreenId)) {
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
    } else {
        // Backstop: a window that becomes floating is no longer snap-managed.
        // Covers float paths that don't emit applyGeometryRequested with an
        // empty zoneId (e.g. a float toggle when no pre-tile geometry is
        // stored, so applyGeometryForFloat sends nothing). Idempotent — a
        // no-op if the window wasn't snap-tracked.
        // clearWindowSnapped also drops the ZoneCache entry (the IsSnapped /
        // Zone rule-fact source), covering float paths that don't emit
        // windowStateChanged with an empty zone.
        m_snapHandler->clearWindowSnapped(windowId);

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
        m_snapHandler->invalidateRestore(::PhosphorIdentity::WindowId::extractAppId(windowId));
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
    invalidateRuleCacheForStateChange(windowId);
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
    // in autotilehandler/signals.cpp debounces on the float side). The
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
                // Stamp with a FRESH clock sample, not the slot-entry
                // nowMs: a cold-cache install compiles the pack on this
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
        if (spuriousPair) {
            // Only the exact leg we stamped is ours to drop — the
            // generation check keeps a superseding leg (or anything else
            // live on the window) on its own teardown.
            if (const auto* st = m_shaderManager.findTransition(w);
                st && st->reverse && st->generation == stamp.generation) {
                endShaderTransition(w);
            }
        } else {
            tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                                   /*reverse=*/false);
        }
    }

    // Snap-mode-only minimize→float bookkeeping is owned by SnapHandler
    // (mirrors AutotileHandler running its own minimize→float machine for
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
    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Settings,
                                                       QStringLiteral("provideRunningWindows"), {jsonString},
                                                       QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

} // namespace PlasmaZones
