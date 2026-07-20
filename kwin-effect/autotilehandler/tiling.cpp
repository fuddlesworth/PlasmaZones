// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiling request handling and window centering for AutotileHandler.
// Part of AutotileHandler — split from autotilehandler.cpp for SRP.

#include "../autotilehandler.h"
#include "../dragtracker.h"
#include "../plasmazoneseffect.h"
#include "../windowanimator.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>
#include <QScopeGuard>
#include <QtMath>

#include <algorithm>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Human-readable KWin maximize mode for the tile-request log. The autotile
// "ballooning" feedback loop (discussion #461) hinges on a window still
// carrying a maximize flag when it is tiled, so the tile-request log records
// this to keep a future report diagnosable without a reproduction.
const char* maximizeModeName(KWin::MaximizeMode mode)
{
    switch (mode) {
    case KWin::MaximizeRestore:
        return "restore";
    case KWin::MaximizeVertical:
        return "vertical";
    case KWin::MaximizeHorizontal:
        return "horizontal";
    case KWin::MaximizeFull:
        return "full";
    }
    return "unknown";
}
} // namespace

void AutotileHandler::slotWindowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests)
{
    if (tileRequests.isEmpty()) {
        return;
    }

    // Validate every request up-front. A single malformed entry is logged and
    // dropped from the batch; the remaining requests still apply. This avoids
    // one corrupt payload (e.g. zero-size tiled request from a protocol glitch)
    // from either resizing a window to 0×0 or poisoning the whole retile pass.
    PhosphorProtocol::TileRequestList validatedRequests;
    validatedRequests.reserve(tileRequests.size());
    for (const auto& req : tileRequests) {
        if (const QString err = req.validationError(); !err.isEmpty()) {
            qCWarning(lcEffect) << "slotWindowsTileRequested: dropping invalid entry:" << err;
            continue;
        }
        validatedRequests.append(req);
    }
    if (validatedRequests.isEmpty()) {
        qCWarning(lcEffect) << "slotWindowsTileRequested: all" << tileRequests.size() << "entries invalid — aborting";
        return;
    }

    // A tile / reflow / overflow-float changes each window's placement mode
    // (tiling ↔ floating) and tiled state, which are rule MATCH fields (Mode,
    // IsTiled). The effect's per-window rule match cache is keyed on
    // (windowId, ruleSet revision) and does not move on a placement change, and
    // unlike the snap path the autotile engine emits no per-window
    // windowStateChanged for the effect to key off. Invalidate here so a
    // `Mode == "tiling"` / `IsTiled` border / title-bar / opacity rule
    // re-resolves for every window this batch touches. Each call coalesces into
    // a single end-of-turn flush, so this is cheap and is a no-op when no
    // appearance/animation rules are loaded.
    for (const auto& req : validatedRequests) {
        // Re-key to the window's LIVE id: the rule-match cache keys on
        // getWindowId, and after a cross-session restore the daemon can still
        // send the pre-restore UUID (slotWindowStateChanged spells out why).
        // Unresolved falls back to the daemon id, correct for the ordinary
        // same-session case where the two are identical.
        QString liveWindowId = req.windowId;
        if (KWin::EffectWindow* const w = m_effect->findWindowById(req.windowId)) {
            liveWindowId = m_effect->getWindowId(w);
        }
        m_effect->invalidateRuleCacheForStateChange(liveWindowId);
    }

    // Stagger generations are bumped PER SCREEN below, once this batch's target
    // screens are known (see m_autotileStaggerGenByScreen). A blanket global
    // bump here would let a cross-output move's destination batch cancel the
    // source reflow's still-staggered windows. The global generation is reserved
    // for desktop/screen switches (slotScreensChanged).
    // NOTE: m_autotileTargetZones and m_centeredWaylandZones are intentionally
    // NOT cleared globally here. Each retile fires for a single screen at a
    // time (per-VS retile after a swap/rotate), so a global clear would wipe
    // sibling-VS entries mid-animation and strand their windows without a
    // centering target. The per-window erase-on-consumption below (and inside
    // the centering handler) keeps the map self-cleaning — entries for
    // windows in the new request get overwritten, entries for windows not in
    // any request are consumed the next time their frame geometry changes.
    // Closed windows are pruned via cleanupClosedWindowState.

    // Snapshot the full global stacking order before tiling. After all
    // moveResize calls (which implicitly raise on KWin 6 / Wayland),
    // the onComplete callback re-raises in this order so non-tiled
    // windows (e.g. Settings) retain their stacking position.
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedGlobalStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedGlobalStack.append(QPointer<KWin::EffectWindow>(w));
    }

    struct Entry
    {
        QString windowId;
        QRect geometry;
        KWin::EffectWindow* window = nullptr;
        QVector<KWin::EffectWindow*> candidates;
        bool isMonocle = false;
        QString screenId; ///< daemon's TARGET screen for this window (req.screenId)
    };
    QVector<Entry> entries;

    for (const auto& req : validatedRequests) {
        const QString& windowId = req.windowId;

        // Float entries: overflow windows that should be restored to pre-autotile geometry.
        // Process inline — same cleanup as slotWindowFloatingChanged(windowId, true, ...).
        // Geometry is restored from the effect's local pre-autotile cache, avoiding
        // the per-window D-Bus roundtrip through the daemon's applyGeometryForFloat.
        if (req.floating) {
            const QString& screenId = req.screenId;
            qCInfo(lcEffect) << "Autotile batch float:" << windowId << "screen:" << screenId;
            applyFloatCleanup(windowId);

            // Restore pre-autotile geometry from the effect's local cache.
            // Scan all screen buckets (all-bucket reader policy — a VS
            // config change can re-key the window's screen without moving
            // its geometry bucket).
            KWin::EffectWindow* floatWin = m_effect->findWindowById(windowId);
            if (floatWin) {
                if (const QRectF savedGeo = findPreAutotileGeometry(windowId); savedGeo.isValid()) {
                    // Daemon-driven apply: the restored rect may lie in a
                    // different virtual screen than the tiled rect, and batch
                    // floats fire in the same swap/rotate window the
                    // crossing-detection guard below (per-window tile apply)
                    // protects against. Without the guard, the synchronous
                    // frameGeometryChanged would resolve the new position
                    // against stale m_virtualScreenDefs and spuriously
                    // re-announce the just-floated window.
                    m_effect->m_inDaemonGeometryApply = true;
                    const auto floatGuard = qScopeGuard([this] {
                        m_effect->m_inDaemonGeometryApply = false;
                    });
                    // Snap-out: leaving tile-managed sizing.
                    m_effect->applyWindowGeometry(floatWin, savedGeo.toRect(), /*allowDuringDrag=*/false,
                                                  /*skipAnimation=*/false,
                                                  PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    qCInfo(lcEffect) << "Restored pre-autotile geometry for overflow" << windowId << savedGeo.toRect();
                }
            }
            continue;
        }

        QRect geo = req.toRect();
        QRect normalizedGeometry = geo.normalized();

        if (normalizedGeometry.width() <= 0 || normalizedGeometry.height() <= 0) {
            qCWarning(lcEffect) << "Autotile tile request: invalid geometry for" << windowId << normalizedGeometry;
            continue;
        }

        QVector<KWin::EffectWindow*> candidates = m_effect->findAllWindowsById(windowId);
        if (candidates.isEmpty()) {
            qCDebug(lcEffect) << "Autotile: window not found:" << windowId;
            continue;
        }
        KWin::EffectWindow* w = nullptr;
        if (candidates.size() == 1) {
            w = candidates.first();
        }
        Entry entry;
        entry.windowId = windowId;
        entry.geometry = normalizedGeometry;
        entry.window = w;
        entry.isMonocle = req.monocle;
        entry.screenId = req.screenId;
        if (candidates.size() > 1) {
            entry.candidates = candidates;
        }
        entries.append(entry);
    }

    // Disambiguate entries with multiple candidates (same appId)
    QHash<QString, QVector<int>> appIdToEntryIndices;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries[i].candidates.isEmpty()) {
            appIdToEntryIndices[::PhosphorIdentity::WindowId::extractAppId(entries[i].windowId)].append(i);
        }
    }
    for (const QVector<int>& indices : std::as_const(appIdToEntryIndices)) {
        if (indices.size() <= 1) {
            if (indices.size() == 1 && entries[indices[0]].candidates.size() > 1) {
                Entry& e = entries[indices[0]];
                QPoint targetCenter = e.geometry.center();
                KWin::EffectWindow* best = nullptr;
                qreal bestDist = 1e9;
                for (KWin::EffectWindow* c : std::as_const(e.candidates)) {
                    QPointF cf = c->frameGeometry().center();
                    qreal d = QPointF(targetCenter - cf).manhattanLength();
                    if (d < bestDist) {
                        bestDist = d;
                        best = c;
                    }
                }
                e.window = best;
            }
            continue;
        }
        QVector<KWin::EffectWindow*> candidates = entries[indices[0]].candidates;
        if (candidates.size() != indices.size()) {
            qCDebug(lcEffect) << "Autotile: stableId has" << indices.size() << "entries and" << candidates.size()
                              << "candidates; assigning by position";
        }
        QVector<int> sortedIndices = indices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), [&entries](int a, int b) {
            return entries[a].geometry.x() < entries[b].geometry.x();
        });
        std::sort(candidates.begin(), candidates.end(), [](KWin::EffectWindow* a, KWin::EffectWindow* b) {
            return a->frameGeometry().x() < b->frameGeometry().x();
        });
        const int n = qMin(sortedIndices.size(), candidates.size());
        for (int i = 0; i < n; ++i) {
            entries[sortedIndices[i]].window = candidates[i];
        }
    }

    // Build snapshot with QPointer for safe deferred access
    struct TileSnap
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString windowId;
        QString screenId;
        bool isMonocle = false;
    };
    QVector<TileSnap> toApply;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        // Key on the daemon's TARGET screen (from the tile request), NOT the
        // window's current physical screen. On a cross-output move the moved
        // window has not physically relocated when this batch is built, so
        // getWindowScreenId() still returns the SOURCE screen — which made the
        // destination batch bump the SOURCE screen's stagger generation and
        // cancel the source monitor's own reflow (its remaining windows never
        // re-tiled). req.screenId is the screen the daemon tiled the window on,
        // and TileRequestEntry::validationError() rejects an empty screenId
        // before it ever reaches `entries`, so it is always present here.
        toApply.append({QPointer<KWin::EffectWindow>(e.window), e.geometry, e.windowId, e.screenId, e.isMonocle});
    }

    // A TILE window the daemon asked us to tile that we could not resolve to a
    // live EffectWindow is dropped from this batch. Surface it: a silent drop
    // here is exactly how a source-monitor reflow loses windows (only the
    // resolvable ones move, the rest are stranded). Compare against the count of
    // TILE requests only — float entries (req.floating) are validated but
    // handled inline above and never enter `toApply`, so counting them would
    // make every batch containing a float falsely report stranded windows.
    const qsizetype tileRequestCount =
        std::count_if(validatedRequests.cbegin(), validatedRequests.cend(), [](const auto& r) {
            return !r.floating;
        });
    if (toApply.size() != tileRequestCount) {
        QStringList resolved;
        for (const TileSnap& s : toApply) {
            resolved << s.windowId;
        }
        qCInfo(lcEffect) << "slotWindowsTileRequested: sent" << tileRequestCount << "tile requests, resolved"
                         << toApply.size() << "windows — applying:" << resolved;
    }

    // Global epoch (desktop/screen switch) captured for the apply guards below.
    const uint64_t gen = m_autotileStaggerGeneration;

    // Build per-screen "new request" sets so the onComplete cleanup can
    // compare each screen's previous bucket against its new bucket in
    // isolation — no cross-screen contamination.
    QHash<QString, QSet<QString>> newTiledByScreen;
    for (const TileSnap& s : toApply) {
        newTiledByScreen[s.screenId].insert(s.windowId);
    }

    // Bump the per-screen generation for every screen this batch retiles, and
    // capture the bumped values. The staggered apply / onComplete below treat a
    // window as superseded only when ITS screen's generation has advanced past
    // the captured value — so a later batch for another screen can no longer
    // cancel this batch's windows.
    QHash<QString, uint64_t> genByScreen;
    for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
        genByScreen.insert(it.key(), ++m_autotileStaggerGenByScreen[it.key()]);
    }

    auto onComplete = [this, newTiledByScreen, savedGlobalStack, gen, genByScreen]() {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        // Per-screen untile cleanup. For each screen that participated in
        // this retile, the set of windows previously tracked as tiled on
        // that screen minus the set in the new request is exactly the
        // windows that left that screen's tiling state. Title bars are
        // restored by the DecorationManager only when no owner remains —
        // a sibling VS's claim or a snap takeover keeps the window hidden.
        for (auto screenIt = newTiledByScreen.constBegin(); screenIt != newTiledByScreen.constEnd(); ++screenIt) {
            const QString& screenId = screenIt.key();
            // A newer retile of this screen has superseded us — it owns this
            // screen's untile cleanup now. (Other screens in this batch may still
            // be current, so skip per-screen rather than aborting the whole
            // onComplete.)
            if (m_autotileStaggerGenByScreen.value(screenId) != genByScreen.value(screenId)) {
                continue;
            }
            const QSet<QString>& newSet = screenIt.value();
            const QSet<QString> previous = AutotileStateHelpers::tiledOnScreen(m_border, screenId);
            const QSet<QString> untiled = previous - newSet;
            for (const QString& wid : untiled) {
                // Exact resolve only: findWindowById's appId fuzzy fallback
                // could hand back a same-app SIBLING for a gone id, and the
                // jurisdiction gate below must read the REAL window's
                // desktop/activity (a vanished window resolves null and still
                // falls through and clears).
                KWin::EffectWindow* win = m_effect->findWindowByIdExact(wid);
                // A retile batch describes ONE (screen, desktop, activity)
                // TilingState — the screen's CURRENT context. A tracked window
                // sitting on another desktop or activity is absent from
                // `newSet` because it belongs to a sibling context's state,
                // not because it was untiled, and this batch has no
                // jurisdiction over it. Clearing it anyway flipped IsTiled,
                // dropped the tiled appearance scope, and restored the title
                // bar on the outgoing desktop's windows for the whole
                // desktop-switch animation (#808) — and identically for an
                // activity switch. Its own context's retile decides its fate;
                // genuine untiles while off-context (float, close) flow
                // through funnels that clear all screens regardless.
                if (win && (!win->isOnCurrentDesktop() || !win->isOnCurrentActivity())) {
                    continue;
                }
                // Every untiled window drops its per-screen tiled tracking,
                // minimized/unresolvable or not — hoisted so the branch below
                // reads as what it actually gates: the centering-target
                // cleanup.
                clearWindowTiledOnScreen(screenId, wid);
                if (!win || win->isMinimized()) {
                    // A minimized (or vanished) window KEEPS its centering
                    // target: the re-tile on unminimize re-asserts it.
                    continue;
                }
                // A daemon-initiated untile that is not a float/fullscreen/
                // close (e.g. a rule change dropping the window from the
                // layout) must not leave a stale centering target that
                // teleport-centers the window on its next
                // frameGeometryChanged. Cross-screen transfers are safe: the
                // apply lambda wrote a fresh entry only for windows in
                // toApply, which are never in `untiled` for their new screen.
                if (!AutotileStateHelpers::isTiledWindow(m_border, wid)) {
                    m_autotileTargetZones.remove(wid);
                    m_centeredWaylandZones.remove(wid);
                }
            }
        }
        auto* ws = KWin::Workspace::self();
        if (ws) {
            // Restore the full global stacking order (all screens, all windows).
            // This ensures non-tiled windows (e.g. Settings KCM, windows on
            // other screens) retain their position instead of being buried.
            for (const auto& wPtr : savedGlobalStack) {
                if (wPtr && !wPtr->isDeleted()) {
                    KWin::Window* kw = wPtr->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }

            // Restore saved autotile stacking order from previous session.
            // These raises go ON TOP of the global restore, preserving user's
            // z-order choices (e.g. floated window raised to front) across
            // mode toggles.
            for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
                const QString& screenId = it.key();
                const QStringList savedOrder = m_savedAutotileStackingOrder.value(screenId);
                if (savedOrder.isEmpty()) {
                    continue;
                }
                for (const QString& windowId : savedOrder) {
                    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
                    if (w && !w->isDeleted()) {
                        KWin::Window* kw = w->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
                m_savedAutotileStackingOrder.remove(screenId);
            }

            if (!m_pendingAutotileFocusWindowId.isEmpty()) {
                KWin::EffectWindow* focusWin = m_effect->findWindowById(m_pendingAutotileFocusWindowId);
                m_pendingAutotileFocusWindowId.clear();
                if (focusWin) {
                    KWin::Window* kw = focusWin->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }
        }

        // After daemon restart, the raise loop above puts all tiled windows on
        // top, burying non-tiled windows (e.g. System Settings KCM) that had
        // focus. Re-activate the previously focused window to restore stacking.
        if (m_pendingReactivateWindow && !m_pendingReactivateWindow->isDeleted()) {
            // Skip (and drop) the reactivation during show-desktop/peek:
            // activateWindow() would synchronously cancel the peek. The
            // stacking restore is cosmetic, so losing it beats breaking peek.
            if (!PlasmaZonesEffect::isShowingDesktop()) {
                KWin::effects->activateWindow(m_pendingReactivateWindow);
            }
            m_pendingReactivateWindow = nullptr;
        }

        // Wayland centering is handled reactively by slotWindowFrameGeometryChanged
        // as soon as the client commits its constrained size — no deferred timer needed.

        // Refresh the active border for the focused window (tiledWindows may have changed)
        m_effect->updateAllDecorations();
    };

    m_effect->applyStaggeredOrImmediate(
        toApply.size(),
        [this, toApply, gen, genByScreen](int i) {
            // Local copy (not const ref) so a stale window pointer can be
            // re-resolved below; the rest of the body reads snap.window.
            TileSnap snap = toApply[i];
            // Drop this apply if superseded by a desktop/screen switch (global
            // epoch) OR by a newer retile of THIS window's screen (per-screen).
            // A batch for a DIFFERENT screen no longer cancels us — that was the
            // cross-output "hole on the source monitor" bug.
            if (m_autotileStaggerGeneration != gen
                || m_autotileStaggerGenByScreen.value(snap.screenId) != genByScreen.value(snap.screenId)) {
                // A genuinely newer retile — of this window's screen, OR a
                // global bump (desktop/screen switch) — has superseded this
                // apply; normal during rapid ops. Both epochs are logged
                // because either can be the one that tripped: printing only
                // the per-screen pair read as "superseded, but the gens match"
                // whenever the global epoch fired. Logged at debug to keep the
                // supersession trail available without production noise (it
                // was the smoking gun for the cross-output "source doesn't
                // reflow" bug: a destination batch keyed to the moved window's
                // STALE screen bumped the source screen's gen).
                qCDebug(lcEffect) << "Autotile apply: skip superseded" << snap.windowId << "screen" << snap.screenId
                                  << "| globalGen now" << m_autotileStaggerGeneration << "captured" << gen
                                  << "| screenGen now" << m_autotileStaggerGenByScreen.value(snap.screenId)
                                  << "captured" << genByScreen.value(snap.screenId);
                return;
            }
            if (!snap.window || snap.window->isDeleted()) {
                // The QPointer was captured when this batch was built; under the
                // rapid window churn of a cross-output move it can go stale
                // before this staggered timer fires. Re-resolve by EXACT id
                // rather than silently dropping the window — dropping it
                // stranded the source monitor's reflow (windows past the first
                // never moved). Exact, not the fuzzy findWindowById: the appId
                // fallback could resolve a SIBLING same-app window (which has
                // its own batch entry) and hand it this window's geometry.
                snap.window = m_effect->findWindowByIdExact(snap.windowId);
            }
            if (!snap.window || snap.window->isDeleted()) {
                qCInfo(lcEffect) << "Autotile apply: window unresolvable at apply time, skipping" << snap.windowId;
                return;
            }
            // Suppress the windowFrameGeometryChanged crossing-detection paths for the
            // duration of this per-window apply. applyWindowGeometry's moveResize emits
            // frameGeometryChanged synchronously, and after a VS swap/rotate the cached
            // m_virtualScreenDefs may still hold pre-rotation regions — without this
            // guard the slot would resolve the new position against stale boundaries
            // and falsely conclude the window crossed VSes, then unsnap it.
            m_effect->m_inDaemonGeometryApply = true;
            const auto guard = qScopeGuard([this] {
                m_effect->m_inDaemonGeometryApply = false;
            });
            saveAndRecordPreAutotileGeometry(snap.windowId, snap.screenId, snap.window, snap.window->frameGeometry());
            KWin::Window* kwForLog = snap.window->window();
            qCInfo(lcEffect) << "Autotile tile request:" << snap.windowId << "QRect=" << snap.geometry
                             << "monocle=" << snap.isMonocle << "maximizeMode="
                             << (kwForLog ? maximizeModeName(kwForLog->maximizeMode()) : "no-window");
            // A window can only be tile-managed by one screen at a time.
            // If this is a cross-screen transfer, strip the stale tracking
            // from any other screen before recording the new owner. This
            // keeps the tracking map coherent when e.g. a window drags
            // from an autotile VS onto a sibling autotile VS.
            AutotileStateHelpers::removeFromOtherScreens(m_border, snap.windowId, snap.screenId);
            markWindowTiled(snap.screenId, snap.windowId);
            // Title-bar (borderless) state is driven by rules through the
            // effect's reconcileRuleHiddenTitleBar → DecorationManager path.

            if (snap.isMonocle) {
                if (KWin::Window* kw = snap.window->window()) {
                    const bool wasAlreadyMaximized = (kw->maximizeMode() == KWin::MaximizeFull);
                    ++m_suppressMaximizeChanged;
                    kw->maximize(KWin::MaximizeFull);
                    if (!wasAlreadyMaximized) {
                        m_monocleMaximizedWindows.insert(snap.windowId);
                    }
                    m_effect->applyWindowGeometry(snap.window, snap.geometry);
                    --m_suppressMaximizeChanged;
                } else {
                    m_effect->applyWindowGeometry(snap.window, snap.geometry);
                }
            } else {
                unmaximizeMonocleWindow(snap.windowId);
                // Clear any KWin maximize state before tiling. A user-
                // maximized window keeps its MaximizeFull flag through
                // moveResize; KWin then re-asserts the maximize-area
                // geometry and the reactive centering in
                // slotWindowFrameGeometryChanged re-applies — the two
                // authorities never converge and compound into the
                // "ballooning" growth (discussion #461). unmaximizeMonocleWindow
                // above only restores windows PlasmaZones itself maximized
                // for monocle; a user-maximized window is never in that set.
                //
                // The MaximizeRestore call resizes the window to its pre-
                // maximize restore geometry before applyWindowGeometry below
                // overwrites it; that intermediate frameGeometryChanged is
                // intentionally absorbed by the m_inDaemonGeometryApply guard
                // set at the top of this lambda — a refactor that moves or
                // narrows that guard reintroduces the ballooning re-entry.
                if (KWin::Window* kw = snap.window->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore) {
                    ++m_suppressMaximizeChanged;
                    kw->maximize(KWin::MaximizeRestore);
                    --m_suppressMaximizeChanged;
                }
                QRect geo = snap.geometry;

                // For Wayland windows being retiled to the same zone, skip the
                // moveResize if the window was previously centered in this zone.
                // This prevents flicker where the window jumps from its centered
                // position back to the zone origin, then gets re-centered 200ms later.
                // It also avoids flooding the Wayland client with configure events
                // which can freeze terminals like Ghostty.
                bool skipMoveResize = false;
                if (snap.window->isWaylandClient()) {
                    auto prevIt = m_centeredWaylandZones.find(snap.windowId);
                    if (prevIt != m_centeredWaylandZones.end() && prevIt.value() == geo) {
                        const QRectF actual = snap.window->frameGeometry();
                        // Window is still within the zone bounds — already centered
                        if (actual.x() >= geo.x() - 1 && actual.y() >= geo.y() - 1 && actual.right() <= geo.right() + 2
                            && actual.bottom() <= geo.bottom() + 2) {
                            skipMoveResize = true;
                            qCDebug(lcEffect) << "Skipping redundant moveResize for centered Wayland window"
                                              << snap.windowId << "zone=" << geo;
                        }
                    }
                }

                if (!skipMoveResize) {
                    m_centeredWaylandZones.remove(snap.windowId);
                    m_effect->applyWindowGeometry(snap.window, geo);
                }
            }

            if (!snap.isMonocle && snap.window->isWaylandClient()) {
                m_autotileTargetZones[snap.windowId] = snap.geometry;
            }
        },
        onComplete);
}

void AutotileHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    Q_UNUSED(oldGeometry)
    if (!w) {
        return;
    }

    // Fast bail: skip getWindowId entirely when neither VS detection nor centering needs it
    if (m_effect->m_virtualScreenDefs.isEmpty() && m_autotileTargetZones.isEmpty()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Virtual screen change detection: KWin's outputChanged only fires on
    // physical monitor changes. When a window moves between virtual screens
    // on the same physical monitor (e.g., A/vs:0 → A/vs:1), no outputChanged
    // fires. Detect the change here so the autotile engine can transfer the
    // window. Only check windows we're already tracking (m_notifiedWindowScreens)
    // and only when the physical screen has virtual subdivisions.
    // Skip during a daemon-driven apply (slotWindowsTileRequested /
    // slotApplyGeometriesBatch): the daemon is the authoritative source of the
    // window's intended VS during VS swap/rotate, and the cached
    // m_virtualScreenDefs may still reflect pre-rotation regions.
    if (m_notifiedWindows.contains(windowId) && !m_effect->m_virtualScreenDefs.isEmpty()
        && m_effect->m_virtualScreensReady && !m_effect->m_inDaemonGeometryApply) {
        // Don't detect VS crossings for the dragged window — the drop handler
        // (callDragStopped / autotile drag end) owns state transitions.
        // Detecting mid-drag would transfer the window before the user drops it.
        // Other windows (e.g., a terminal reflowing) should still get VS crossing checks.
        const bool isDraggedWindow =
            m_effect->m_dragTracker->isDragging() && windowId == m_effect->m_dragTracker->draggedWindowId();
        if (!isDraggedWindow) {
            const QString newScreenId = m_effect->getWindowScreenId(w);
            const QString oldScreenId = m_notifiedWindowScreens.value(windowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                // Virtual screen changed on the same physical monitor — delegate to
                // the same handler used by outputChanged. The re-entrancy guard
                // inside handleWindowOutputChanged prevents infinite loops from
                // geometry changes caused by tiling.
                handleWindowOutputChanged(w);
                return;
            }
        }
        // Fall through to centering logic below for all windows (including dragged)
    }

    if (m_autotileTargetZones.isEmpty()) {
        return;
    }

    auto it = m_autotileTargetZones.find(windowId);
    if (it == m_autotileTargetZones.end()) {
        return;
    }

    const QRect& targetZone = it.value();
    const QRectF actual = w->frameGeometry();

    constexpr qreal MinCenteringDelta = 3.0;

    const qreal dw = targetZone.width() - actual.width();
    const qreal dh = targetZone.height() - actual.height();

    // Window fills the zone (or close enough) — no centering needed; consume entry
    if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
        qCDebug(lcEffect) << "Autotile centering: matched" << windowId << "dw=" << dw << "dh=" << dh;
        m_autotileTargetZones.erase(it);
        return;
    }

    // Window doesn't match zone — center it within the zone so it's visually
    // balanced rather than stuck at the zone origin.
    // Clamp offsets to non-negative: when the window is LARGER than the zone
    // (oversized, dx < 0), left/top-align instead of centering. Centering an
    // oversized window pushes it to a negative position (off-screen left/top),
    // which is worse than a slight overflow to the right/bottom. The daemon
    // receives the min-size report below and will retile with adjusted zones.
    const qreal dx = qMax(0.0, dw / 2.0);
    const qreal dy = qMax(0.0, dh / 2.0);
    QRectF centered(targetZone.x() + dx, targetZone.y() + dy, actual.width(), actual.height());

    // Defensive bounds clamp: if the (oversized) window would extend past the
    // physical output containing the zone, shift it left/up so it stays on
    // the same output. Without this, a window whose min size exceeds its
    // zone leaks into an adjacent monitor — KWin then reassigns the window's
    // output and the autotile engine ejects it. The daemon-side bounds clamp
    // in recalculateLayout already shifts zones to fit, so this is a backstop
    // for cases where the zone still violates min size (script algorithms,
    // unsatisfiable constraints, residual rounding).
    //
    // Scope: this clamps to the physical Output*, NOT the virtual-screen
    // sub-region. Overflow that crosses a virtual-screen boundary on the
    // same physical monitor is the daemon-side clamp's responsibility (it
    // resolves the VS region from screenGeometry(screenId); the effect side
    // has no reliable lookup for that here).
    //
    // Contract parity with PhosphorGeometry::clampZonesToScreen: both keep
    // an "effective rect" inside the bounds. The two implementations
    // intentionally use different size sources — daemon-side uses
    // max(zone.size, declared minSize) because it runs *before* KWin
    // enforces min size, while the effect runs *after* and reads the actual
    // (already-enforced) frame size from `centered`. Same contract, different
    // input source and rect type (QRectF here, QRect there). Keep the four
    // shift formulas in sync at the contract level.
    if (auto* output = KWin::effects->screenAt(targetZone.center())) {
        const QRect screenGeo = output->geometry();
        // Use exclusive edges (x + width / y + height) since QRectF::right()
        // and QRect::right() disagree (QRect is x+width-1, QRectF is x+width).
        const qreal screenLeft = screenGeo.x();
        const qreal screenTop = screenGeo.y();
        const qreal screenRight = screenGeo.x() + screenGeo.width();
        const qreal screenBottom = screenGeo.y() + screenGeo.height();
        const QRectF preClamp = centered;
        if (centered.x() + centered.width() > screenRight) {
            centered.moveLeft(qMax(screenLeft, screenRight - centered.width()));
        }
        if (centered.y() + centered.height() > screenBottom) {
            centered.moveTop(qMax(screenTop, screenBottom - centered.height()));
        }
        // Symmetric left/top underflow: a centered position before the screen
        // origin (target zone with negative offset, oversized window centered
        // off-edge) gets snapped back. Matches the daemon-side clamp.
        if (centered.x() < screenLeft) {
            centered.moveLeft(screenLeft);
        }
        if (centered.y() < screenTop) {
            centered.moveTop(screenTop);
        }
        // Symmetric with daemon-side clampZonesToScreen logging: when the
        // clamp actually fired, log the before/after so a "clamp ran but
        // didn't fix it" report is diagnosable from one side.
        if (Q_UNLIKELY(lcEffect().isDebugEnabled()) && centered.topLeft() != preClamp.topLeft()) {
            qCDebug(lcEffect) << "Autotile centering: clamp adjusted" << windowId << "from" << preClamp.topLeft()
                              << "to" << centered.topLeft() << "screen=" << screenGeo;
        }
    } else {
        // screenAt may return null if the zone center happens to fall in the
        // air between outputs (unusual; daemon assigns zones to a real
        // screen). Log so the silent skip is diagnosable rather than
        // mysterious.
        qCDebug(lcEffect) << "Autotile centering: screenAt(" << targetZone.center()
                          << ") returned null — skipping bounds clamp for" << windowId;
    }

    // Already at the centered position — record and consume
    if (qAbs(actual.x() - centered.x()) < 1.0 && qAbs(actual.y() - centered.y()) < 1.0) {
        m_centeredWaylandZones[windowId] = targetZone;
        m_autotileTargetZones.erase(it);
        return;
    }

    KWin::Window* kw = w->window();
    if (!kw) {
        // No KWin::Window — consume stale entry to prevent perpetual lookups
        m_autotileTargetZones.erase(it);
        return;
    }

    qCInfo(lcEffect) << "Centering autotile window" << windowId << "actual=" << actual.size()
                     << "zone=" << targetZone.size() << "offset=(" << dx << "," << dy << ")";

    // Window refused to shrink below its actual size — report its declared
    // minimum to the daemon so future retiles can account for it. Only report
    // when the window is larger than the zone (negative delta = oversized).
    //
    // IMPORTANT: Only use the window's declared minSize() from the compositor.
    // The frame geometry is the current size, which may be transiently larger
    // during resize animations (Wayland configure round-trips) or media player
    // loading. Reporting the frame geometry as the min-size creates a feedback
    // loop: inflated min → expanded zone → window fills expanded zone →
    // inflated min confirmed → ratio stuck.
    //
    // Previously, windows without a declared min-size fell back to
    // targetZone.width() as a bounded hint. This caused the same feedback
    // loop: the zone width became the stored min-size, which then prevented
    // the algorithm from reducing the zone on subsequent retiles — even when
    // the user adjusted the split ratio or a screen geometry change required
    // reflow. The stale min-size persisted until the window was removed or
    // unfloated (minimize+restore), making the ratio appear "stuck."
    //
    // Without the fallback, apps that don't declare a min-size simply won't
    // get min-size enforcement from this path. They still get the initial
    // min-size from the windowOpened D-Bus call (kw->minSize() at open time),
    // and the centering code handles the visual placement correctly.
    // declaredMinSize() carries the internal-window guard (KWin's
    // InternalWindow::minSize() segfaults on a null backing QWindow, see
    // discussion #511); internal windows never reach the autotile-centering
    // pipeline, but the helper keeps the call site safe independently of the
    // upstream eligibility filter.
    if (dw < -MinCenteringDelta || dh < -MinCenteringDelta) {
        const QSize declaredMin = declaredMinSize(w);
        int discoveredMinW = 0;
        int discoveredMinH = 0;
        if (dw < -MinCenteringDelta && declaredMin.width() > 0) {
            discoveredMinW = declaredMin.width();
        }
        if (dh < -MinCenteringDelta && declaredMin.height() > 0) {
            discoveredMinH = declaredMin.height();
        }
        if (discoveredMinW > 0 || discoveredMinH > 0) {
            reportDiscoveredMinSize(windowId, discoveredMinW, discoveredMinH);
        }
    }

    // Erase BEFORE moveResize to prevent re-entrancy: moveResize emits
    // windowFrameGeometryChanged synchronously, which would re-enter
    // this slot and find the entry still present → infinite recursion → crash.
    m_centeredWaylandZones[windowId] = targetZone;
    m_autotileTargetZones.erase(it);
    m_effect->m_windowAnimator->removeAnimation(w);
    kw->moveResize(centered);
}

void AutotileHandler::slotFocusWindowRequested(const QString& windowId)
{
    // Showing-desktop guard (see isShowingDesktop's doc): the tile engine
    // re-emits this after every relayout, and activating a hidden window
    // cancels a peek. The pending id is deliberately not recorded either.
    if (PlasmaZonesEffect::isShowingDesktop()) {
        qCDebug(lcEffect) << "Autotile: focus request dropped during show desktop:" << windowId;
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "Autotile: window not found for focus request:" << windowId;
        return;
    }

    m_pendingAutotileFocusWindowId = windowId;
    KWin::effects->activateWindow(w);
}

void AutotileHandler::reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight)
{
    if (minWidth <= 0 && minHeight <= 0) {
        return;
    }

    qCInfo(lcEffect) << "Discovered min size for" << windowId << ":" << minWidth << "x" << minHeight
                     << "- reporting to daemon for future retiles";

    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowMinSizeUpdated"),
        {windowId, minWidth, minHeight}, QStringLiteral("windowMinSizeUpdated"));
}

} // namespace PlasmaZones
