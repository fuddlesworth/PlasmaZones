// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

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
            applySnapGeometry(w, sizeOnlyGeo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
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
    if (zoneId.isEmpty() && m_dragFloatedWindowIds.remove(liveWindowId)) {
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
        // Capture frame geometry synchronously BEFORE applySnapGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        m_snapHandler->ensurePreSnapGeometryStored(w, liveWindowId, w->frameGeometry());
    }

    // Empty zoneId = float-restore (daemon placing the window back at its pre-snap geometry, e.g.
    // autotile drag-to-float, drag-out unsnap). Non-empty zoneId = snap into a target zone. The
    // shader-tree path differs accordingly so users can give snap-in and snap-out distinct effects.
    applySnapGeometry(w, geometry, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
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

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending, batchProfilePath](int i) {
            const auto& p = pending[i];
            // isDeleted too, not just destruction: close-shader grabs (which
            // this effect takes) keep deleted windows alive in the stacking
            // order for the close-animation duration, and the stagger delay
            // widens the race window — moving/animating a dying window would
            // also re-pollute the just-scrubbed id caches via getWindowId.
            if (!p.window || p.window->isDeleted()) {
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
            // Drain any title-bar restores deferred from autotile→snap mode
            // toggle. slotScreensChanged queues deferred releases in the
            // DecorationManager instead of running the slow Wayland
            // decoration round-trips synchronously; by the time onComplete
            // fires here, animations for all resnapped windows are already
            // in flight via the animation framework, so borders return
            // mid-animation rather than after a 250+ ms stall before motion
            // begins. Windows snap re-acquired during the resnap keep their
            // title bars hidden (the manager cancels their queued restores).
            // Unconditional — for non-mode-toggle batches (rotate,
            // vs_reconfigure, snap_all) the pending set is empty and the
            // call is a no-op.
            m_decorationManager->drainPendingRestores();
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
    // isFloating is now a rule MATCH field — re-resolve appearance / animation
    // rules for this window so a `WHEN isFloating` border / opacity re-applies.
    invalidateRuleCacheForStateChange(windowId);
}

void PlasmaZonesEffect::slotWindowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& state)
{
    // Keep the effect-side zone cache current so the IsSnapped / Zone rule-match
    // fields resolve against the live placement. An empty zoneId (unsnapped /
    // floated / screen-changed) removes the entry. The isFloating cache WRITE lives
    // on the separate windowFloatingChanged path, so it is not duplicated here; the
    // rule-cache invalidation below may still run on both paths (it is idempotent).
    m_navigationHandler->setWindowZone(windowId, state.zoneId);
    invalidateRuleCacheForStateChange(windowId);
}

void PlasmaZonesEffect::invalidateRuleCacheForStateChange(const QString& windowId)
{
    if (m_shaderManager.animationRuleSet().isEmpty()) {
        return;
    }
    // The match cache is keyed on (windowId, ruleSet revision); neither moves on
    // a placement-state change, so drop it so border / opacity rules re-resolve
    // against the new snapped / floating / zone state.
    //
    // Only the CACHED verdicts (border / opacity) need explicit invalidation here.
    // shouldHandleWindow's exclusion query is evaluated on-demand every time it is
    // consulted (drag start, lifecycle filtering), so a snap/float/zone change is
    // picked up at the next natural call without eager re-filtering — re-running it
    // for every window on every state change would be a needless hot-path cost.
    m_shaderManager.animationRuleEvaluator().clearCache();
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        // Recreate this window's border so a state-scoped border colour re-applies.
        updateWindowBorder(windowId, w);
        // An opacity-only (borderless) window needs an explicit repaint for its
        // re-resolved opacity to reach the screen (mirrors slotWindowActivated).
        if (m_shaderManager.hasOpacityRules()) {
            w->addRepaintFull();
        }
    }
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    const QString windowId = getWindowId(w);
    const QString screenId = getWindowScreenId(w);
    const bool minimized = w->isMinimized();

    // window.minimize shader transition fires for BOTH snap and autotile
    // screens — the shader event is screen-mode-independent and the
    // autotile handler's own minimised-change slot does not fire it
    // (which would otherwise be asymmetric per-screen UX for the same
    // user-configured "WindowMinimize" event).
    //
    // We only fire on UN-minimize (forward 0→1, "appear"). The
    // going-to-minimized direction is intentionally not a shader event
    // on the kwin-effect path: KWin pulls the surface (collapses frame
    // geometry to 0×0 / sets isMinimized=true) BEFORE this signal fires,
    // and beginShaderTransition's collapsed-surface guard rejects the
    // install — the FBO allocation aborts on a 0×0 redirect target.
    // A genuine "going away" minimise animation would need an
    // unredirect-time hook that captures the last live frame before
    // KWin tears the surface down; that's out of scope for this layer.
    if (!minimized) {
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                               /*reverse=*/false);
    }

    // Snap-mode-only minimize→float bookkeeping is owned by SnapHandler (mirrors
    // AutotileHandler running its own minimize→float machine for autotile screens).
    m_snapHandler->handleMinimizeChanged(windowId, screenId, minimized);
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
