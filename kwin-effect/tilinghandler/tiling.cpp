// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiling request handling and window centering for TilingHandler.
// Part of TilingHandler — split from tilinghandler.cpp for SRP.
//
// FILE-SIZE EXCEPTION (sanctioned): slotWindowsTileRequested is one batch
// pipeline — parse, float split, view-leg seeding, cascade ordering, the
// apply lambda and its completion — whose stages hand per-batch state
// (generations, seeded sets, the residual-origin gates) straight down the
// function. Over the 1150 ceiling before PR #891 and accepted as such;
// a future split should carve at the batch-parse / apply boundary, not
// mid-pipeline.

#include "tilinghandler.h"
#include "scrolldecisions.h"
#include "handlers/dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"
#include "transitions/striptransitionmanager.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QtMath>

#include <algorithm>

namespace PlasmaZones {

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

void TilingHandler::slotWindowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests)
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

    // Seam diagnostics (docs/strip-identity-seam-plan.md, stage 0). The
    // ARRIVAL of a batch is itself the evidence: the engine emits on change
    // only (engine_apply.cpp:746), so returning to a desktop whose strip is
    // unchanged emits nothing at all, and candidate A is diagnosed by this
    // line NOT appearing after a desktop switch. Read against the
    // desktopChanged line from the same category.
    if (lcStripDiag().isDebugEnabled()) {
        QSet<QString> screens;
        int parkedEntries = 0;
        for (const auto& req : validatedRequests) {
            screens.insert(req.screenId);
            if (req.hasVisualPos) {
                ++parkedEntries;
            }
        }
        qCDebug(lcStripDiag) << "batch arrived:" << validatedRequests.size() << "entries," << parkedEntries
                             << "with visualPos, screens=" << screens;
    }

    // A geometry batch on a scrolling screen slides columns under the
    // stationary pointer; pause FFM until the cursor moves deliberately
    // (see suppressFfmUntilCursorMoves) so the next pointer twitch cannot
    // steal focus onto whatever landed under it.
    // Walked in full rather than broken out of, because the same pass answers
    // whether every TILE request is on a scrolling screen — which the stacking
    // snapshot below uses to skip work a scroll-only batch never reads.
    //
    // The two answers deliberately cover different sets. The FFM suppression
    // reacts to any request at all, float entries included: a float restores
    // pre-autotile geometry, which slides a window under a stationary pointer
    // exactly like a tile does. The stacking answer counts TILE entries only,
    // matching its sole consumer — savedGlobalStack is read under
    // `hasApplies && !scrollOnlyBatch`, and scrollOnlyBatch is computed over
    // toApply, which float entries never enter (they are handled inline
    // below). Counting a float there made a scroll batch carrying one copy the
    // whole stacking order into QPointers and haul it through the onComplete
    // closure for a consumer that could not read it.
    //
    // Seeded true, not `!validatedRequests.isEmpty()`: the empty case already
    // returned above, so the loop always sees at least one request. A
    // floats-only batch legitimately leaves it true, which is the right
    // answer — with no tile applies there is no z-order to repair.
    bool anyRequestOnScrollingScreen = false;
    bool allRequestsOnScrollingScreens = true;
    for (const auto& req : validatedRequests) {
        if (isScrollingScreen(req.screenId)) {
            anyRequestOnScrollingScreen = true;
        } else if (!req.floating) {
            allRequestsOnScrollingScreens = false;
        }
    }
    if (anyRequestOnScrollingScreen) {
        suppressFfmUntilCursorMoves();
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
    // Screens whose batch is a heartbeat view tick: every entry there is the
    // same tiled window at a translated position — Mode and IsTiled cannot
    // have changed (the engine's owned scroll runs no structural mutator),
    // so invalidating would wipe the global rule-match cache and re-resolve
    // every strip window's decoration ~60 times a second for the length of
    // an edge hold. A structural change mid-hold gives ownership back first,
    // so its batch arrives without the flag and invalidates as before.
    // Accepted trade: the POSITIONAL rule fields (positionX/Y, width,
    // height) DO move on a view tick, so a rule scoped on them freezes for
    // the length of the hold and refreshes on the first non-immediate batch
    // — a bounded cosmetic lag bought for not re-resolving every window's
    // decoration at heartbeat rate.
    QSet<QString> immediateTickScreens;
    for (const auto& req : validatedRequests) {
        if (req.viewImmediate) {
            immediateTickScreens.insert(req.screenId);
        }
    }
    for (const auto& req : validatedRequests) {
        if (req.viewImmediate || immediateTickScreens.contains(req.screenId)) {
            continue;
        }
        // Re-key to the window's LIVE id: the rule-match cache keys on
        // getWindowId, and after a cross-session restore the daemon can still
        // send the pre-restore UUID (slotWindowStateChanged spells out why).
        // Unresolved falls back to the daemon id, correct for the ordinary
        // same-session case where the two are identical. findWindowById's
        // fuzzy appId fallback is fine HERE, unlike the tab-swap install's
        // exact wire-to-live map below: a wrong-instance hit invalidates a
        // sibling's cache entry (a spurious re-resolve), not a wrong paint.
        QString liveWindowId = req.windowId;
        if (KWin::EffectWindow* const w = m_effect->findWindowById(req.windowId)) {
            liveWindowId = m_effect->getWindowId(w);
        }
        m_effect->invalidateRuleCacheForStateChange(liveWindowId);
    }

    // Stagger generations are bumped PER SCREEN below, once this batch's target
    // screens are known (see m_tileStaggerGenByScreen). A blanket global
    // bump here would let a cross-output move's destination batch cancel the
    // source reflow's still-staggered windows. The global generation is reserved
    // for desktop/screen switches (slotScreensChanged).
    // NOTE: m_tileTargetZones and m_centeredWaylandZones are intentionally
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
    // windows (e.g. Settings) retain their stacking position. Overlap-layout
    // batches substitute their tiled group with a deterministic order during
    // that restore — see the onComplete raise loop below.
    // Skipped entirely for a scroll-only batch. onComplete reads this only
    // under `hasApplies && !scrollOnlyBatch`, and toApply is a subset of
    // validatedRequests, so all-scrolling requests imply a scroll-only batch
    // and the snapshot would never be read. It is not free: a wheel tick or a
    // drag-insert fires a batch, and each one copied the whole stacking order
    // into QPointers and carried it by value into the onComplete closure.
    QVector<QPointer<KWin::EffectWindow>> savedGlobalStack;
    if (!allRequestsOnScrollingScreens && KWin::effects) {
        const auto allWindows = KWin::effects->stackingOrder();
        savedGlobalStack.reserve(allWindows.size());
        for (KWin::EffectWindow* w : allWindows) {
            savedGlobalStack.append(QPointer<KWin::EffectWindow>(w));
        }
    }

    struct Entry
    {
        QString windowId;
        QRect geometry;
        KWin::EffectWindow* window = nullptr;
        QVector<KWin::EffectWindow*> candidates;
        bool isMonocle = false;
        bool isWindowedFullscreen =
            false; ///< scrolling windowed fullscreen: hold KWin fullscreen state at the column rect
        bool isMaximizedToEdges = false; ///< scrolling: this window's column is maximized (mirror KWin's maximize bit)
        QString screenId; ///< daemon's TARGET screen for this window (req.screenId)
        QString stacking; ///< overlap z-order policy ("firstOnTop"/"lastOnTop"), empty for non-overlap layouts
        /// scrolling strip: screen edge to animate from. Four-valued since
        /// wire v5 — "left"/"right" on a horizontal strip, "top"/"bottom" on
        /// a vertical one. Empty for a non-scrolling entry.
        QString scrollEdge;
        int viewDelta = 0; ///< scrolling strip: how far the view slid, 0 when this window is not carried by it
        bool viewImmediate = false; ///< scrolling strip: user-driven continuous view motion — apply the delta outright
        /// scrolling strip: where a PARKED column really sits. Stored at apply
        /// time as a ScrollVisualPlacement (this position plus the column
        /// size); the paint path derives its own translation from that per
        /// read, centring the committed frame inside the column rather than
        /// substituting for it — which would erase the X11 constrain-and-centre
        /// offset. See scrollVisualTranslationFor.
        QPoint visualPos;
        bool hasVisualPos = false;
        QString tabFrom; ///< scrolling strip: the tab this entry replaces in a tabbed column, else empty
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
            // Re-key to the window's LIVE id, the same way the rule-cache
            // invalidation above and the tile path below both do. After a
            // cross-session restore the daemon can still send the pre-restore
            // UUID, and every operation in applyFloatCleanup is id-keyed
            // (floating flag, tiled-state clear, target-zone and centering
            // maps, decoration reconcile, monocle unmaximize) — with a stale id
            // all of them miss the live entries and the exact resolve below
            // returns null, so the pre-autotile geometry restore is skipped
            // with nothing logged on that arm. Falls back to the daemon id when
            // unresolved, which is correct for the ordinary same-session case.
            QString floatWindowId = windowId;
            if (KWin::EffectWindow* const live = m_effect->findWindowById(windowId)) {
                floatWindowId = m_effect->getWindowId(live);
            }
            qCInfo(lcEffect) << "Autotile batch float:" << floatWindowId << "screen:" << screenId;
            applyFloatCleanup(floatWindowId);

            // Restore pre-autotile geometry from the effect's local cache.
            // preTileRestoreRectFor keeps the all-bucket reader policy — a VS
            // config change can re-key the window's screen without moving its
            // geometry bucket, and that rect is still applicable — while
            // degrading a rect from ANOTHER output to size-only, which would
            // otherwise move the window to that monitor. Exact resolve,
            // matching the tile lambda's deliberate policy: a fuzzy hit would
            // teleport a same-app SIBLING onto this window's restored rect.
            KWin::EffectWindow* floatWin = m_effect->findWindowByIdExact(floatWindowId);
            if (!floatWin) {
                // Both misses below are LOGGED. applyFloatCleanup above has
                // already run unconditionally, so the window is genuinely
                // floated and untiled either way — it just stays at its tile
                // rect. That state is coherent and looks exactly like a restore
                // that worked, so without a line here the journal cannot tell
                // the two apart. The tile arm logs every one of its drops for
                // the same reason.
                qCWarning(lcEffect) << "Autotile batch float: no live window for" << floatWindowId << "on" << screenId
                                    << "— floated without restoring its pre-tile geometry";
            }
            if (floatWin) {
                const QRectF savedGeo = preTileRestoreRectFor(floatWindowId, screenId, floatWin->frameGeometry());
                if (!savedGeo.isValid()) {
                    qCWarning(lcEffect) << "Autotile batch float: no applicable pre-tile rect for" << floatWindowId
                                        << "on" << screenId << "— floated, but left at its tile rect";
                }
                if (savedGeo.isValid()) {
                    // Daemon-driven apply: the restored rect may lie in a
                    // different virtual screen than the tiled rect, and batch
                    // floats fire in the same swap/rotate window the
                    // crossing-detection guard below (per-window tile apply)
                    // protects against. Without the guard, the synchronous
                    // frameGeometryChanged would resolve the new position
                    // against stale m_virtualScreenDefs and spuriously
                    // re-announce the just-floated window.
                    // Save/restore, not set/clear (nesting-safe).
                    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
                    m_effect->m_daemonGate.inGeometryApply = true;
                    const auto floatGuard = qScopeGuard([this, prevInApply] {
                        m_effect->m_daemonGate.inGeometryApply = prevInApply;
                    });
                    // Snap-out: leaving tile-managed sizing.
                    m_effect->applyWindowGeometry(floatWin, savedGeo.toRect(), /*allowDuringDrag=*/false,
                                                  /*skipAnimation=*/false,
                                                  PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    // Re-seed the tracked screen: the comment above names
                    // the exact precondition (the restored rect may lie in
                    // a different virtual screen than the tiled rect), the
                    // bracket suppressed the detectors' tracker write, and
                    // applyWindowGeometry does not self-seed — without this
                    // the next genuine geometry change reads the stale
                    // pre-apply screen and fires a spurious VS transfer.
                    m_effect->m_trackedScreenPerWindow[floatWin] = m_effect->getWindowScreenId(floatWin);
                    qCInfo(lcEffect) << "Restored pre-autotile geometry for overflow" << floatWindowId
                                     << savedGeo.toRect();
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
        entry.isWindowedFullscreen = req.windowedFullscreen;
        entry.isMaximizedToEdges = req.maximizedToEdges;
        entry.screenId = req.screenId;
        entry.stacking = req.stacking;
        entry.scrollEdge = req.scrollEdge;
        // Clamped here, at the wire boundary, because the batch has TWO
        // consumers: the view spring and the per-window origin below. The wire
        // deliberately does not validate this field, and clamping inside the
        // animator alone left the origin built from the raw value — so a
        // garbled delta would start every carried column's leg arbitrarily far
        // off-screen, which is the same flung strip the clamp exists to
        // prevent, just moved onto the per-window springs. One bounded value
        // for both consumers makes the animator's own clamp idempotent.
        entry.viewDelta =
            qBound(-StripViewAnimator::kMaxViewDeltaPx, req.viewDelta, StripViewAnimator::kMaxViewDeltaPx);
        entry.viewImmediate = req.viewImmediate;
        entry.visualPos = req.hasVisualPos ? QPoint(req.visualX, req.visualY) : QPoint();
        entry.hasVisualPos = req.hasVisualPos;
        entry.tabFrom = req.tabFrom;
        if (candidates.size() > 1) {
            entry.candidates = candidates;
        }
        entries.append(entry);
    }

    // Disambiguate entries with multiple candidates (same appId). An entry
    // that matched EXACTLY (one candidate, resolved above) must RESERVE its
    // window against the fuzzy entries: exact matches are not in the index
    // below, and without the claimed-set a same-appId fuzzy entry (the
    // stale-pre-restore-UUID case this file guards in three other places)
    // could resolve to the SAME window — every per-window map then written
    // twice for one id, the second apply overwriting the first, and the
    // window the fuzzy entry was meant for silently never tiled.
    QSet<KWin::EffectWindow*> claimedByExact;
    for (const Entry& e : std::as_const(entries)) {
        if (e.window && e.candidates.isEmpty()) {
            claimedByExact.insert(e.window);
        }
    }
    QHash<QString, QVector<int>> appIdToEntryIndices;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries[i].candidates.isEmpty()) {
            appIdToEntryIndices[::PhosphorIdentity::WindowId::extractAppId(entries[i].windowId)].append(i);
        }
    }
    for (const QVector<int>& indices : std::as_const(appIdToEntryIndices)) {
        if (indices.size() <= 1) {
            // No candidates.size() > 1 term: the map only admits entries with
            // a non-empty candidate vector, and Entry::candidates is only ever
            // assigned when that vector already has more than one element, so
            // the test could never be false here.
            if (indices.size() == 1) {
                Entry& e = entries[indices[0]];
                QPoint targetCenter = e.geometry.center();
                KWin::EffectWindow* best = nullptr;
                qreal bestDist = 1e9;
                for (KWin::EffectWindow* c : std::as_const(e.candidates)) {
                    if (claimedByExact.contains(c)) {
                        continue;
                    }
                    QPointF cf = c->frameGeometry().center();
                    qreal d = QPointF(targetCenter - cf).manhattanLength();
                    if (d < bestDist) {
                        bestDist = d;
                        best = c;
                    }
                }
                if (best) {
                    e.window = best;
                } else {
                    // Every candidate was claimed by an exact entry: the drop
                    // is correct (the alternative is a double-apply) but must
                    // not be silent — this is the "window never tiled" outcome
                    // the claimed-set diagnostics exist to surface.
                    qCWarning(lcEffect) << "Autotile: all fuzzy candidates for" << e.windowId
                                        << "claimed by exact entries — dropping";
                }
            }
            continue;
        }
        QVector<KWin::EffectWindow*> candidates = entries[indices[0]].candidates;
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [&claimedByExact](KWin::EffectWindow* c) {
                                            return claimedByExact.contains(c);
                                        }),
                         candidates.end());
        if (candidates.size() != indices.size()) {
            qCDebug(lcEffect) << "Autotile: stableId has" << indices.size() << "entries and" << candidates.size()
                              << "candidates; assigning by position";
        }
        // Both keys are the top-left compared LEXICOGRAPHICALLY, not x alone.
        // The two sorts are zipped index-to-candidate below, so they must
        // impose the same total order on the same layout — and an x-only key
        // does not: on a vertical strip every column spans the full work-area
        // width, so every pair ties, both sorts are unstable, and the zip pairs
        // a window with another window's committed rect (every per-window map
        // the apply lambda writes then follows the wrong window). The tie is
        // not vertical-only — two same-app windows sharing one tabbed column
        // tie on a horizontal strip too — so the key is axis-neutral rather
        // than axis-switched.
        const auto beforeByTopLeft = [](const QPoint& a, const QPoint& b) {
            return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
        };
        QVector<int> sortedIndices = indices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), [&entries, &beforeByTopLeft](int a, int b) {
            return beforeByTopLeft(entries[a].geometry.topLeft(), entries[b].geometry.topLeft());
        });
        std::sort(
            candidates.begin(), candidates.end(), [&beforeByTopLeft](KWin::EffectWindow* a, KWin::EffectWindow* b) {
                return beforeByTopLeft(a->frameGeometry().toRect().topLeft(), b->frameGeometry().toRect().topLeft());
            });
        const int n = qMin(sortedIndices.size(), candidates.size());
        if (n < sortedIndices.size()) {
            qCWarning(lcEffect) << "Autotile: only" << n << "unclaimed candidates for" << sortedIndices.size()
                                << "entries — trailing entries dropped";
        }
        for (int i = 0; i < n; ++i) {
            entries[sortedIndices[i]].window = candidates[i];
        }
    }

    // wire id → live id for every resolved entry of THIS batch, built in the
    // loop below. Consumed by the tab-swap install to re-key tabFrom (see the
    // comment at the insert). Captured BY VALUE into the apply lambda: the
    // staggered applies outlive this function.
    QHash<QString, QString> wireToLive;
    // Build snapshot with QPointer for safe deferred access
    struct TileSnap
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString windowId;
        QString screenId;
        bool isMonocle = false;
        bool isWindowedFullscreen = false;
        bool isMaximizedToEdges = false;
        QString stacking;
        QString scrollEdge;
        int viewDelta = 0;
        bool viewImmediate = false;
        /// Carried verbatim from Entry::visualPos — see its doc for how the
        /// paint path consumes it (stored as a placement, resolved per read).
        QPoint visualPos;
        bool hasVisualPos = false;
        QString tabFrom;
    };
    QVector<TileSnap> toApply;
    QSet<KWin::EffectWindow*> applied;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        // Belt behind the claimed-set above: a double-resolve that slips
        // through must not apply twice — and a WARNED drop shrinks
        // toApply below tileRequestCount, so the stranded-window log at
        // the end of this function fires and the case is diagnosable
        // instead of silent.
        if (applied.contains(e.window)) {
            qCWarning(lcEffect) << "Autotile: two batch entries resolved to one window — dropping the second for"
                                << e.windowId;
            continue;
        }
        applied.insert(e.window);
        // Re-key to the RESOLVED window's live id. The disambiguation above
        // can match a candidate whose uuid differs from the daemon-supplied
        // entry id (stale across a KWin restart), and every write this batch
        // performs — tiled tracking, the Wayland centering cache, the
        // pre-autotile capture — must key on the live id the readers use, or
        // the tiling goes untracked and the stale-keyed entries are never
        // reclaimed.
        //
        // The wire→live pairs are remembered so tabFrom — a daemon-supplied
        // id NAMING ANOTHER WINDOW — can be re-keyed through the same
        // resolution its target's own entry already went through. The
        // outgoing tab of a swap is by construction a sibling entry of this
        // very batch, so the map is the exact, fuzzy-free translation the
        // tabFrom lookup needs; a tabFrom whose window has no entry falls
        // back to the wire spelling unchanged.
        const QString wireId = e.windowId;
        e.windowId = m_effect->getWindowId(e.window);
        wireToLive.insert(wireId, e.windowId);
        // Key on the daemon's TARGET screen (from the tile request), NOT the
        // window's current physical screen. On a cross-output move the moved
        // window has not physically relocated when this batch is built, so
        // getWindowScreenId() still returns the SOURCE screen — which made the
        // destination batch bump the SOURCE screen's stagger generation and
        // cancel the source monitor's own reflow (its remaining windows never
        // re-tiled). req.screenId is the screen the daemon tiled the window on,
        // and TileRequestEntry::validationError() rejects an empty screenId
        // before it ever reaches `entries`, so it is always present here.
        toApply.append({QPointer<KWin::EffectWindow>(e.window), e.geometry, e.windowId, e.screenId, e.isMonocle,
                        e.isWindowedFullscreen, e.isMaximizedToEdges, e.stacking, e.scrollEdge, e.viewDelta,
                        e.viewImmediate, e.visualPos, e.hasVisualPos, e.tabFrom});
    }

    // Start this batch's view legs, ONCE per output. The delta is a property
    // of the batch that every carried entry repeats, so folding it in per
    // window would spring the strip N times as far. First non-zero wins per
    // screen; a well-formed batch agrees across its entries, and disagreement
    // would mean the engine resolved one screen twice in one pass, where the
    // first answer is as good as any.
    //
    // This is what makes the strip rigid: one spring per output, read back by
    // the paint path for every column, instead of N per-window springs that
    // each start a moment apart and integrate themselves apart.
    //
    // Filled with the screenIds whose spring actually STARTED (or
    // retargeted) a leg this batch. Consumed twice below: the residual-
    // origin branch of the apply lambda treats an entry's viewDelta as
    // real only for these screens (with no leg the paint offset is zero,
    // so an origin placed a delta behind the target would pop backwards
    // and slide double), and the cascade decision skips the direction sort
    // only when a leg exists to make the one-pass apply mandatory.
    // Membership comes from applyBatchDelta's return value, NOT from the
    // wire delta: animations-off and clockless outputs fold the delta into
    // the accumulator without a leg.
    //
    // When any leg started, the applies must land in ONE pass: the paint
    // path adds the view offset to every scroll-managed window on the
    // output with no test for whether that window's own commit has arrived,
    // so a column still waiting on its stagger timer draws at (old rect +
    // offset), a full delta from where it belongs. With the shipped 40 ms
    // cascade against a 150 ms leg, columns from the fourth on would sit
    // still for the whole leg and then teleport in sequence.
    QSet<QString> startedViewScreens;
    // Screens whose view travel this batch is USER-DRIVEN continuous motion
    // (the drag edge auto-scroll heartbeat, ~60 Hz). Their delta is folded
    // into the accumulator with NO leg and NO strip shader pass — a leg
    // retargeted every 16 ms never progresses on a stateless curve, so the
    // painted strip would stall behind the committed geometry and then glide
    // once when the ticks stop, with the drop indicator and tab strips
    // (computed from committed rects) running ahead of it. The per-tick
    // commits are the motion. Entries on these screens take degenerate legs
    // (applied outright) below.
    QSet<QString> immediateViewScreens;
    {
        // Both resolves are LAZY: an ordinary autotile batch with no view
        // travel never pays the two tree walks. Once needed they resolve
        // once per batch, not per screen — the view's motion node is not
        // screen-dependent, and this is the same cascade every other
        // event's animation goes through (global animator profile → the
        // scrolling.view motion-tree override). A windowless query skips
        // the per-window rule tier, which is right — the view belongs to
        // the strip, not to any window on it. The SHADER leg resolves
        // through resolveShaderWithDefault exactly like the desktop legs
        // (user override → ancestor override → built-in default, empty for
        // scrolling), gated on the animations master toggle and folded
        // into an empty id rather than a skip so notifyLeg's erase
        // contract still runs — clearing the pack (or disabling
        // animations) mid-flight disarms the pass on the very next wheel
        // tick.
        bool resolved = false;
        PhosphorAnimation::Profile viewProfile;
        QString stripEffectId;
        QVariantMap stripEffectParams;
        QSet<QString> seededScreens;
        QSet<KWin::LogicalOutput*> seededOutputs;
        // Outputs whose FIRST spelling took the immediate (no-leg) path.
        // The duplicate-spelling arm classifies from this, never from its
        // own entry's flags: a mixed-flag batch (one spelling immediate, the
        // other not) would otherwise put a spring-live output into
        // immediateViewScreens — whose apply arm takes the outright
        // placement origin against a non-zero paint offset — or drop the
        // second spelling's entries from both sets.
        QSet<KWin::LogicalOutput*> immediateOutputs;
        for (const TileSnap& s : toApply) {
            if (s.viewDelta == 0 || s.screenId.isEmpty() || seededScreens.contains(s.screenId)) {
                continue;
            }
            // Marked seeded only once the output RESOLVES. Marking before the
            // lookup treated an unresolvable screen as done, so a later entry
            // for the same screen — which is the ordinary case, since every
            // carried column repeats the delta — could not retry it.
            if (KWin::LogicalOutput* out = m_effect->outputForScreenId(s.screenId)) {
                seededScreens.insert(s.screenId);
                // Dedup on the resolved OUTPUT as well as the id string:
                // applyBatchDelta is additive, so two screenId spellings
                // resolving to one output would spring the strip twice as
                // far — the exact defect the once-per-output rule exists to
                // prevent. A second spelling still lands in seededScreens
                // (and startedViewScreens below) so ITS entries take the
                // residual-origin path against the one shared spring.
                if (seededOutputs.contains(out)) {
                    if (immediateOutputs.contains(out)) {
                        immediateViewScreens.insert(s.screenId);
                    } else if (m_effect->m_stripViewAnimator->isAnimatingOn(out)) {
                        startedViewScreens.insert(s.screenId);
                    }
                    continue;
                }
                seededOutputs.insert(out);
                if (s.viewImmediate) {
                    // Heartbeat-driven view motion: disarm any strip shader
                    // pass (there is no leg for it to decorate) and fold the
                    // delta straight into the accumulator. No profile resolve
                    // — nothing animates.
                    const PhosphorProtocol::ScrollAxis immediateAxis = scrollAxisForScreen(s.screenId);
                    m_effect->m_stripTransition.notifyLeg(out, QString(), QVariantMap(), 0, immediateAxis);
                    m_effect->m_stripViewAnimator->applyImmediateDelta(out, s.viewDelta, immediateAxis);
                    immediateViewScreens.insert(s.screenId);
                    immediateOutputs.insert(out);
                    continue;
                }
                if (!resolved) {
                    resolved = true;
                    viewProfile = m_effect->resolveEventMotionProfile(PhosphorAnimation::ProfilePaths::ScrollingView,
                                                                      PhosphorRules::WindowQuery{}, QString());
                    if (m_effect->m_windowAnimator->isEnabled()) {
                        const PhosphorAnimationShaders::ShaderProfile stripShaderProfile =
                            PhosphorAnimationShaders::resolveShaderWithDefault(
                                m_effect->m_shaderManager.profileTree(),
                                PhosphorAnimation::ProfilePaths::ScrollingView);
                        stripEffectId = stripShaderProfile.effectiveEffectId();
                        stripEffectParams = stripShaderProfile.effectiveParameters();
                    }
                }
                // Arm (or refresh, or — empty id — disarm) the strip shader
                // pass BEFORE the spring moves: notifyLeg distinguishes a
                // fresh leg from a retarget by whether the spring is
                // already live AND by comparing this batch's axis against
                // the one the spring still holds, so this ordering is
                // load-bearing on both counts (see its header contract).
                const PhosphorProtocol::ScrollAxis batchAxis = scrollAxisForScreen(s.screenId);
                m_effect->m_stripTransition.notifyLeg(out, stripEffectId, stripEffectParams, s.viewDelta, batchAxis);
                if (m_effect->m_stripViewAnimator->applyBatchDelta(out, s.viewDelta, batchAxis, viewProfile)) {
                    startedViewScreens.insert(s.screenId);
                } else {
                    // The spring declined (animations off, no clock): there
                    // is no leg for the pass to decorate and no offset for
                    // a residual origin to lean on — disarm the pass and
                    // let this screen's entries take the ordinary paths.
                    m_effect->m_stripTransition.notifyLeg(out, QString(), QVariantMap(), 0, batchAxis);
                }
            }
        }
    }
    const bool startedViewLegs = !startedViewScreens.isEmpty();
    // A batch carrying a tab swap must apply in ONE pass. The default Cascade
    // stagger runs paint frames BETWEEN entries, and the swap's two entries
    // are order-unspecified: park-first re-folds the outgoing tab's composite
    // at the park before the seed reads it (losing exactly the pre-park fold
    // the seed exists to capture), while arrival-first leaves both tabs
    // committed at one rect for a stagger interval — a visible double
    // exposure. forceImmediate is the documented "this batch cannot be split
    // without tearing" channel, the same one the view legs already use.
    const bool anyTabSwap = std::any_of(toApply.cbegin(), toApply.cend(), [](const TileSnap& s) {
        return !s.tabFrom.isEmpty();
    });

    // Cascade order follows the direction of travel for a scrolling strip.
    //
    // applyStaggeredOrImmediate below delays entry i by i * interval, so the
    // ORDER of toApply is the order the user watches windows move in. The
    // batch arrives in strip order (first column to last) whichever way the
    // strip scrolled, so the cascade ran in that one order both ways and the
    // two directions did not mirror each other — scrolling one way looked like
    // it led with the near edge, the other like it led with the far one.
    //
    // Everything below measures along the strip's OWN axis, resolved per
    // entry: a vertical strip travels in y, and one batch can carry entries
    // for screens running opposite axes.
    //
    // Sort on where each window is SEEN, not on its committed rect: an
    // arriving column's target is its on-screen rect, but a leaving column's
    // target is the park, which is far off-screen on whichever side was safe
    // and would sort it to an extreme unrelated to the motion the user
    // watches. A leaving column is keyed on where it currently sits instead,
    // which is the start of its exit slide.
    // Skipped when this batch started view legs: those apply in one pass, so
    // there is no cascade left to order. A scroll batch that moved no columns
    // in or out of view (a park with no view travel) still cascades and still
    // wants the direction sort, which is why the two predicates differ.
    // Immediate-view batches and tab-swap batches skip it too — all three
    // force one-pass application below (the forceImmediate disjunction at
    // the dispatch), so sorting them would be pure cost, on a ~60 Hz path
    // for the immediate case. The predicates are batch-wide: a batch mixing
    // one-pass screens with cascade screens conservatively loses the sort
    // for all of them, which today's per-screen emitters never produce.
    const bool isScrollBatch = !startedViewLegs && !anyTabSwap && immediateViewScreens.isEmpty()
        && std::any_of(toApply.cbegin(), toApply.cend(), [](const TileSnap& s) {
               return !s.scrollEdge.isEmpty();
           });
    if (isScrollBatch) {
        QHash<QString, QRect> screenRectCache;
        // BY VALUE, not by const reference into the hash: the same lambda
        // inserts into screenRectCache, and a reference handed out before a
        // later insert rehashes the table is a use-after-rehash the moment a
        // caller holds one across a second call. A QRect copy is four ints.
        const auto screenRectFor = [&](const TileSnap& s) -> QRect {
            auto it = screenRectCache.find(s.screenId);
            if (it == screenRectCache.end()) {
                QRect outRect;
                if (const KWin::LogicalOutput* out = m_effect->outputForScreenId(s.screenId)) {
                    outRect = out->geometry();
                }
                it = screenRectCache.insert(s.screenId, outRect);
            }
            return it.value();
        };
        // The rect applyWindowGeometry will actually COMMIT for each entry,
        // memoised per window for the duration of this sort. The animation
        // branch below builds its origins and its degenerate-leg tests from
        // exactly this rect (see constrainTileGeometry's own doc), and the
        // cascade has to classify and measure against the same thing. For a
        // size-constrained X11 column the constrained frame is inset from the
        // column by the centring offset, so the raw column rect made
        // `isArriving` answer differently than the apply does for a column
        // straddling its output by less than that offset, and gave every such
        // staying column's netDx term a spurious -centringOffset — enough to
        // flip the batch's net sign and run the whole cascade mirrored.
        // Falls back to the raw rect for a null window pointer, which is what
        // constrainTileGeometry returns for a windowless entry anyway.
        QHash<QString, QRect> constrainedCache;
        const auto committedRectFor = [&](const TileSnap& s) -> QRect {
            auto it = constrainedCache.find(s.windowId);
            if (it == constrainedCache.end()) {
                const QRect raw = s.geometry.normalized();
                it = constrainedCache.insert(s.windowId,
                                             s.window ? m_effect->constrainTileGeometry(s.window, raw) : raw);
            }
            return it.value();
        };
        // An UNRESOLVED output does not read as arriving: the committed rect
        // of a leaving column is the park, and sorting on it would feed the
        // cascade a coordinate unrelated to what the user watches. The
        // current frame is the visible truth either way.
        const auto isArriving = [&](const TileSnap& s) {
            const QRect rect = screenRectFor(s);
            return rect.isValid() && rect.intersects(committedRectFor(s));
        };
        // Axis per ENTRY, never hoisted for the batch: one batch can carry
        // entries for several screens, and a portrait monitor beside a
        // landscape one runs its strip the other way. Resolved through the
        // same scrollAxisForScreen the view leg and the origin math use, so
        // all three read one source.
        const auto isVertical = [this](const TileSnap& s) {
            return scrollAxisForScreen(s.screenId) == PhosphorProtocol::ScrollAxis::Vertical;
        };
        // Position along the strip's own axis. On a vertical strip every
        // column shares an x, so an x-only key made every comparison tie and
        // the stable_sort degenerated into "leave the batch order alone" —
        // which is the left-to-right-both-ways bug this sort exists to fix,
        // reappearing on the other axis.
        const auto visibleAlong = [&](const TileSnap& s) {
            const bool vertical = isVertical(s);
            if (!s.window || isArriving(s)) {
                const QRect committed = committedRectFor(s);
                return vertical ? committed.y() : committed.x();
            }
            const KWin::RectF frame = s.window->frameGeometry();
            return qRound(vertical ? frame.y() : frame.x());
        };
        // Net travel across the batch decides which end leads, measured on
        // STAYING columns only — target on-screen AND current frame
        // on-screen — and along each entry's own axis. An arriving column's
        // current position is the park, which carries no direction (the park
        // is direction-agnostic by design), and a leaving column's target is
        // the park, so both are excluded: each would swamp the staying
        // columns' true delta with an arbitrary sign. (The earlier form
        // filtered on !isArriving alone, which kept only LEAVING columns —
        // whose visible position is their own frame, making every term
        // identically zero and the branch below dead; the scrollEdge fallback
        // decided every batch.)
        qint64 netAlong = 0;
        for (const TileSnap& s : toApply) {
            if (!s.window || !isArriving(s)) {
                continue;
            }
            const QRect rect = screenRectFor(s);
            const QRect currentFrame = s.window->frameGeometry().toRect();
            if (rect.isValid() && rect.intersects(currentFrame)) {
                // Committed rect, matching isArriving above and the apply
                // below — the raw column origin differs from it by the
                // centring offset for a size-constrained X11 column, and that
                // offset is not travel.
                const QRect committed = committedRectFor(s);
                netAlong +=
                    isVertical(s) ? qint64(committed.y()) - currentFrame.y() : qint64(committed.x()) - currentFrame.x();
            }
        }
        // "Forward" is toward increasing coordinate along the entry's axis:
        // rightward on a horizontal strip, downward on a vertical one.
        bool movingForward = netAlong > 0;
        if (netAlong == 0) {
            // No staying column moved (all-leaving or all-arriving batch,
            // or the stays' deltas cancelled exactly).
            // The scrollEdge is authoritative there: a column LEAVES by the
            // edge the content moves toward, and ARRIVES from the edge it
            // once left by (content moving away from it).
            for (const TileSnap& s : toApply) {
                if (s.scrollEdge.isEmpty()) {
                    continue;
                }
                // Four-value since wire v5: a vertical strip names "top" and
                // "bottom". Testing `!= "left"` folded both of those into
                // "right", so a vertical batch that fell through to this
                // fallback ran its cascade in whichever direction the fold
                // happened to pick. Named against the two BACKWARD edges so an
                // unrecognised value still reads forward, as it did before.
                const bool edgeIsForward =
                    s.scrollEdge != QLatin1String("left") && s.scrollEdge != QLatin1String("top");
                movingForward = isArriving(s) ? !edgeIsForward : edgeIsForward;
                break;
            }
        }
        std::stable_sort(toApply.begin(), toApply.end(), [&](const TileSnap& a, const TileSnap& b) {
            return movingForward ? visibleAlong(a) > visibleAlong(b) : visibleAlong(a) < visibleAlong(b);
        });
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
    const uint64_t gen = m_tileStaggerGeneration;

    // Build per-screen "new request" sets so the onComplete cleanup can
    // compare each screen's previous bucket against its new bucket in
    // isolation — no cross-screen contamination.
    QHash<QString, QSet<QString>> newTiledByScreen;
    for (const TileSnap& s : toApply) {
        newTiledByScreen[s.screenId].insert(s.windowId);
    }

    // Deterministic z-order for overlap layouts. The daemon stamps each entry
    // of an overlap-algorithm batch with its stacking policy; the batch is in
    // tiling order, so per screen this yields the bottom-to-top raise order:
    // tiling order for "lastOnTop" (cascade/stair/paper — last window ends up
    // topmost), reversed for "firstOnTop" (reverse-nested layouts — index 0
    // topmost; no bundled algorithm declares it, custom scripts may).
    // Non-overlap batches leave this empty and keep the pre-tile stacking.
    QHash<QString, QVector<QPointer<KWin::EffectWindow>>> overlapStackByScreen;
    {
        QHash<QString, QString> stackPolicyByScreen;
        for (const TileSnap& s : toApply) {
            if (!s.stacking.isEmpty()) {
                overlapStackByScreen[s.screenId].append(s.window);
                stackPolicyByScreen[s.screenId] = s.stacking;
            }
        }
        for (auto it = overlapStackByScreen.begin(); it != overlapStackByScreen.end(); ++it) {
            if (stackPolicyByScreen.value(it.key()) == QLatin1String("firstOnTop")) {
                std::reverse(it.value().begin(), it.value().end());
            }
        }
    }

    // Bump the per-screen generation for every screen this batch retiles, and
    // capture the bumped values. The staggered apply / onComplete below treat a
    // window as superseded only when ITS screen's generation has advanced past
    // the captured value — so a later batch for another screen can no longer
    // cancel this batch's windows.
    QHash<QString, uint64_t> genByScreen;
    for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
        genByScreen.insert(it.key(), ++m_tileStaggerGenByScreen[it.key()]);
    }
    // The STRIP this batch was built for, captured beside the generations and
    // checked the same way. The generations alone cannot see a strip retire:
    // retireStripScopedState drops the per-window relocations but bumps
    // nothing, so a staggered apply still in flight from the OUTGOING strip's
    // batch would land afterwards and re-insert the very entry the retire just
    // dropped, putting a parked column back at the previous strip's position.
    //
    // The batch carries no epoch of its own. The value is READ OUT of
    // m_stripEpochByScreen here, while this batch is being processed, and what
    // makes that read mean "the strip this batch is for" is signal ordering:
    // the daemon announces BEFORE it schedules the forced retile, and both
    // travel one D-Bus connection, so a batch whose processing precedes the
    // announcement necessarily predates the switch and belongs to the OUTGOING
    // strip. slotStripContextChanged then records the new epoch before it
    // retires, so those pending applies fail this compare and are dropped,
    // while a batch processed after it captures the new value and runs.
    //
    // The genuinely raced-ahead case — a batch that established strip state
    // before the effect had ever heard an epoch for the screen — is not
    // handled here at all. slotStripContextChanged's first-epoch branch
    // handles it by declining to retire, so no apply is ever cancelled on its
    // behalf. A screen with no epoch yet reads empty on both sides and is
    // unaffected.
    QHash<QString, QString> stripEpochByScreen;
    for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
        stripEpochByScreen.insert(it.key(), m_stripEpochByScreen.value(it.key()));
    }

    const bool hasApplies = !toApply.isEmpty();
    // A batch that only touches scrolling screens never needs the stacking
    // repair: scroll applies commit through moveResize, which does not
    // restack, and a strip batch carries no overlap groups — so the global
    // saved-stack restore would raise every window in the workspace on every
    // scroll tick purely to reimpose the order nothing disturbed.
    bool scrollOnlyBatch = hasApplies;
    for (const TileSnap& s : toApply) {
        if (!isScrollingScreen(s.screenId)) {
            scrollOnlyBatch = false;
            break;
        }
    }
    auto onComplete = [this, newTiledByScreen, savedGlobalStack, overlapStackByScreen, gen, genByScreen, hasApplies,
                       scrollOnlyBatch, immediateViewScreens]() {
        if (m_tileStaggerGeneration != gen) {
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
            if (m_tileStaggerGenByScreen.value(screenId) != genByScreen.value(screenId)) {
                continue;
            }
            // A heartbeat view tick cannot change tiled membership — the
            // strip is structurally frozen for the whole hold (detach-once),
            // and any mutation that could untile (a close, a float) gives
            // the auto-scroll's ownership back first, so ITS batch arrives
            // without the immediate flag and runs this diff. Skipping saves
            // a per-screen set build + difference at ~60 Hz per hold.
            if (immediateViewScreens.contains(screenId)) {
                continue;
            }
            const QSet<QString>& newSet = screenIt.value();
            const QSet<QString> previous = TilingStateHelpers::tiledOnScreen(m_border, screenId);
            // No resolution-failure exclusion is needed here, which is worth
            // recording because it is not obvious. The three disambiguation
            // drops above never leave a tracked window out of newSet: the
            // all-claimed drop fires precisely because every candidate was
            // taken by an exact entry, the trailing-entry drop assigns every
            // candidate it has (n = qMin(entries, candidates)), and the
            // double-resolve drop discards the SECOND entry for a window the
            // first already claimed. In each case the WINDOWS are all in
            // newSet and only surplus ENTRIES are dropped, so an id in
            // `untiled` is a window the daemon genuinely stopped tiling.
            // That holds globally; newSet here is one SCREEN's bucket, so a
            // window whose claiming entry landed on a different screen does
            // appear in the old screen's `untiled`. It has moved, and untiling
            // it on the screen it left is the right answer.
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
                // The parked-column paint hint dies with the tiled tracking:
                // a window in a SUPERSEDED batch's entry never reached the
                // per-entry write (the apply lambda returns on supersession),
                // and a window this batch no longer carries would otherwise
                // keep the previous batch's strip position — painted there
                // for as long as the stale entry survives. The float path
                // clears its own (applyFloatCleanup); this covers the rest.
                // The removal changes where the paint path draws the window
                // (relocated position → nothing), so pair it with damage —
                // the batch's own applies only damage the regions they
                // touch, not the vacated relocation.
                if (m_effect->m_scrollVisualDelta.remove(wid) > 0 && KWin::effects) {
                    KWin::effects->addRepaintFull();
                }
                // The other two strip companions go with it. The teardown
                // funnels shed all three together, and this one shed only
                // the relocation, so a window untiled by a rule change kept the
                // column it was last OFFERED. On re-tile the apply then reads
                // columnUnchanged against that stale offer, skips offering the
                // column, and hands the client whatever size it is holding now
                // — possibly one the user resized during the untiled interval.
                // No damage pairing: neither is a paint input (the offered
                // column feeds a move(), the commanded rect gates a
                // counter-assert), unlike the relocation removed above.
                m_effect->m_scrollCommandedRects.remove(wid);
                m_effect->m_scrollOfferedColumn.remove(wid);
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
                if (!TilingStateHelpers::isTiledWindow(m_border, wid)) {
                    m_tileTargetZones.remove(wid);
                    m_centeredWaylandZones.remove(wid);
                    // Windowed fullscreen dies with the untile too: this is
                    // the one strip exit with no other release owner (float,
                    // close, cross-output transfer, mode/screen change and
                    // teardown all have theirs). `untiled` is a local copy,
                    // so the release's possible re-entry into
                    // cleanupAutotileTracking cannot invalidate this loop.
                    if (m_effect->m_windowedFullscreenWindows.contains(wid)) {
                        forgetWindowedFullscreen(wid);
                        releaseWindowedFullscreenState(wid);
                    }
                    // The column mirror is in exactly the same position on
                    // this exit, and for the same reason: no other owner
                    // releases it here, and the window is leaving the strip
                    // that would otherwise carry a cleared flag back. `win`
                    // is the exact resolve from above and is non-null and
                    // unminimized on this branch.
                    releaseMaximizedToEdges(wid, win);
                }
            }
        }
        // A batch with NO tile applies (every request resolved away or the
        // batch was floats-only) still owes the untile cleanup above, but
        // must not churn the whole stacking order or spend the one-shot
        // saved-order/pending-focus state — nothing moved, so there is no
        // z-order to repair.
        auto* ws = KWin::Workspace::self();
        if (ws && hasApplies && !scrollOnlyBatch) {
            // Membership index for the overlap restack: window -> the screen
            // whose ordered group it belongs to. Resolved at completion time
            // because QPointers may have gone null since the batch was built.
            // A screen whose per-screen stagger generation has advanced past
            // this batch's captured value is superseded (same guard as the
            // untile cleanup above): the newer batch's onComplete owns that
            // screen's stacking, and re-imposing this batch's stale order
            // AFTER it would stand as the final, wrong z-order. Excluding the
            // screen here routes its windows through the plain saved-stack
            // restore below and skips it in the fresh-window sweep.
            QHash<const KWin::EffectWindow*, QString> overlapMemberScreen;
            for (auto it = overlapStackByScreen.constBegin(); it != overlapStackByScreen.constEnd(); ++it) {
                if (m_tileStaggerGenByScreen.value(it.key()) != genByScreen.value(it.key())) {
                    continue;
                }
                for (const auto& gPtr : it.value()) {
                    if (gPtr && !gPtr->isDeleted()) {
                        overlapMemberScreen.insert(gPtr.data(), it.key());
                    }
                }
            }

            // Restore the full global stacking order (all screens, all windows).
            // This ensures non-tiled windows (e.g. Settings KCM, windows on
            // other screens) retain their position instead of being buried.
            //
            // Overlap-layout groups are the exception: their tiled windows are
            // raised as one block, in the daemon's declared bottom-to-top
            // order, at the stack position of the group's lowest pre-tile
            // member. Restoring the arbitrary pre-tile order for them is
            // exactly the reported bug (cascade/deck/monocle stacks scrambled
            // after every retile). Substitution keeps other screens' windows
            // and floats OUTSIDE the group's span where they were; a float
            // that sat between two group members ends up above the whole
            // block (its saved-stack raise comes after the block's slot),
            // which is the useful place for a float anyway.
            QSet<const KWin::EffectWindow*> groupRaised;
            for (const auto& wPtr : savedGlobalStack) {
                if (!wPtr || wPtr->isDeleted()) {
                    continue;
                }
                const auto memberIt = overlapMemberScreen.constFind(wPtr.data());
                if (memberIt != overlapMemberScreen.constEnd()) {
                    if (groupRaised.contains(wPtr.data())) {
                        continue;
                    }
                    // constFind, not operator[]: on a const QHash the
                    // subscript returns BY VALUE, deep-copying the group per
                    // member visit (O(members²) across the loop).
                    const auto& group = *overlapStackByScreen.constFind(memberIt.value());
                    for (const auto& gPtr : group) {
                        if (gPtr && !gPtr->isDeleted()) {
                            if (KWin::Window* gkw = gPtr->window()) {
                                ws->raiseWindow(gkw);
                            }
                            groupRaised.insert(gPtr.data());
                        }
                    }
                    continue;
                }
                KWin::Window* kw = wPtr->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
            }
            // A window can join an overlap batch without having been in the
            // pre-tile stack snapshot (opened in the same tick). Raise any
            // group not visited above so it still gets its declared order.
            // Superseded screens are skipped for the same reason as in the
            // membership build.
            for (auto it = overlapStackByScreen.constBegin(); it != overlapStackByScreen.constEnd(); ++it) {
                if (m_tileStaggerGenByScreen.value(it.key()) != genByScreen.value(it.key())) {
                    continue;
                }
                for (const auto& gPtr : it.value()) {
                    if (gPtr && !gPtr->isDeleted() && !groupRaised.contains(gPtr.data())) {
                        if (KWin::Window* gkw = gPtr->window()) {
                            ws->raiseWindow(gkw);
                        }
                        groupRaised.insert(gPtr.data());
                    }
                }
            }

            // Restore saved autotile stacking order from previous session.
            // These raises go ON TOP of the global restore, preserving user's
            // z-order choices (e.g. floated window raised to front) across
            // mode toggles.
            //
            // Superseded screens are skipped on the same terms as the three
            // loops above: a newer batch's onComplete owns that screen's
            // stacking, and this batch must neither re-impose a stale order on
            // top of it nor CONSUME the saved entry the newer batch still
            // needs — the remove() below is a one-shot.
            for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
                const QString& screenId = it.key();
                if (m_tileStaggerGenByScreen.value(screenId) != genByScreen.value(screenId)) {
                    continue;
                }
                const QStringList savedOrder = m_savedAutotileStackingOrder.value(screenId);
                if (savedOrder.isEmpty()) {
                    continue;
                }
                for (const QString& windowId : savedOrder) {
                    // Exact: a fuzzy same-app hit would raise a SIBLING into
                    // this window's saved z-position.
                    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
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
                // Exact for the same sibling-raise reason as the saved-order
                // loop above.
                KWin::EffectWindow* focusWin = m_effect->findWindowByIdExact(m_pendingAutotileFocusWindowId);
                m_pendingAutotileFocusWindowId.clear();
                if (focusWin) {
                    KWin::Window* kw = focusWin->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }

            // The deterministic overlap restack can bury the active window
            // mid-stack (e.g. focus sits on a middle cascade window). Lift it
            // back above its group — the same raise KWin performs on
            // activation — so the window the user is typing into stays
            // visible. Non-members are untouched: their position was already
            // restored by the saved-stack loop.
            if (KWin::EffectWindow* active = KWin::effects ? KWin::effects->activeWindow() : nullptr;
                active && overlapMemberScreen.contains(active)) {
                if (KWin::Window* kw = active->window()) {
                    ws->raiseWindow(kw);
                }
            }
        } else if (hasApplies) {
            // No `ws &&` term: this arm only CONSUMES state, it never touches
            // the Workspace, so gating it on a pointer it does not use meant a
            // null Workspace::self() leaked exactly the two one-shots the
            // comment below says must not survive the batch.
            //
            // Scroll-only batch: the restack block above is skipped, but two
            // of its CONSUMES must still happen or state leaks across
            // batches. The pending-focus id is written on EVERY scrolling
            // focus verb (slotFocusWindowRequested) and, left unconsumed,
            // would be spent by the first later non-scroll batch raising a
            // stale window at an unrelated moment; the raise itself is
            // redundant here (KWin raises on activation). The saved
            // stacking order is a one-shot the replay normally consumes —
            // drop this batch's screens' entries without replaying so a
            // much-older session's z-order is not re-imposed the first time
            // the screen leaves scrolling. Supersession guard matches the
            // replay loop's.
            m_pendingAutotileFocusWindowId.clear();
            for (auto it = newTiledByScreen.constBegin(); it != newTiledByScreen.constEnd(); ++it) {
                if (m_tileStaggerGenByScreen.value(it.key()) == genByScreen.value(it.key())) {
                    m_savedAutotileStackingOrder.remove(it.key());
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
            if (KWin::effects && !PlasmaZonesEffect::isShowingDesktop()) {
                KWin::effects->activateWindow(m_pendingReactivateWindow);
            }
            m_pendingReactivateWindow = nullptr;
        }

        // Wayland centering is handled reactively by slotWindowFrameGeometryChanged
        // as soon as the client commits its constrained size — no deferred timer needed.

        // Refresh the active border for the focused window (tiledWindows may
        // have changed). Skipped on a pure heartbeat batch: the walk resyncs
        // and re-decorates the ENTIRE stacking order, and a view tick
        // changes no decoration input (membership frozen, focus unchanged) —
        // running it synchronously ~60 times a second for the length of an
        // edge hold was the epilogue's dominant cost.
        if (immediateViewScreens.isEmpty()) {
            m_effect->updateAllDecorations();
        }

        // The compositor-drawn tab pills need a strip member to anchor on,
        // and this batch is what makes the screen's windows scroll-managed
        // again after a daemon handover (the per-session drain cleared the
        // membership; the strips were re-fetched before any column was
        // re-tiled, so their rebuild damaged a band nothing could yet paint).
        // Damage the pill bounds now that an anchor exists; a no-op on the
        // ordinary relayout path, where the strips' own push damaged them.
        if (KWin::effects) {
            for (auto screenIt = newTiledByScreen.constBegin(); screenIt != newTiledByScreen.constEnd(); ++screenIt) {
                // Heartbeat ticks skip this: their per-tick strip payload
                // push already damages the band, so this handover repaint
                // would be a redundant boundsFor + addRepaint per tick.
                if (immediateViewScreens.contains(screenIt.key())) {
                    continue;
                }
                if (KWin::LogicalOutput* out = m_effect->outputForScreenId(screenIt.key())) {
                    const QRect bounds = m_effect->m_scrollTabPainter->boundsFor(out);
                    if (bounds.isValid()) {
                        KWin::effects->addRepaint(KWin::Rect(bounds));
                    }
                }
            }
        }
    };

    m_effect->applyStaggeredOrImmediate(
        toApply.size(),
        [this, toApply, gen, genByScreen, stripEpochByScreen, startedViewScreens, immediateViewScreens,
         wireToLive](int i) {
            // Local copy (not const ref) so a stale window pointer can be
            // re-resolved below; the rest of the body reads snap.window.
            TileSnap snap = toApply[i];
            // Drop this apply if superseded by a desktop/screen switch (global
            // epoch), by a newer retile of THIS window's screen (per-screen),
            // or by the screen's STRIP being replaced under it. A batch for a
            // DIFFERENT screen no longer cancels us — that was the
            // cross-output "hole on the source monitor" bug.
            if (m_tileStaggerGeneration != gen
                || m_tileStaggerGenByScreen.value(snap.screenId) != genByScreen.value(snap.screenId)
                || m_stripEpochByScreen.value(snap.screenId) != stripEpochByScreen.value(snap.screenId)) {
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
                                  << "| globalGen now" << m_tileStaggerGeneration << "captured" << gen
                                  << "| screenGen now" << m_tileStaggerGenByScreen.value(snap.screenId) << "captured"
                                  << genByScreen.value(snap.screenId) << "| stripEpoch now"
                                  << m_stripEpochByScreen.value(snap.screenId) << "captured"
                                  << stripEpochByScreen.value(snap.screenId);
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
            // Save/restore, not set/clear (nesting-safe).
            const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
            m_effect->m_daemonGate.inGeometryApply = true;
            const auto guard = qScopeGuard([this, prevInApply] {
                m_effect->m_daemonGate.inGeometryApply = prevInApply;
            });
            // Pre-seed the effect's tracked screen (mirrors daemon_apply's
            // pre-seed): the outputChanged handler early-returns while
            // inGeometryApply is set, so without this a cross-output tile
            // apply leaves the map naming the OLD screen and the next
            // genuine user move diffs against stale state. The handler's
            // OWN notified-screen map must move too — for scroll-managed
            // windows getWindowScreenId answers FROM this map, so a
            // scroll→scroll handoff that skipped it would pin every
            // id-keyed consumer (close records, minimize routing, rule
            // Mode stamp, drag drop) to the old monitor forever.
            m_effect->m_trackedScreenPerWindow[snap.window] = snap.screenId;
            // Gated on tracked membership: a batch can still carry a window
            // whose open rolled back or was demoted by the desktop-switch
            // pass — writing its screen here would desynchronise the pair
            // (m_notifiedWindows says untracked, the screen map answers)
            // and feed notifyWindowAdded a self-referential seed.
            if (m_notifiedWindows.contains(snap.windowId)) {
                m_notifiedWindowScreens[snap.windowId] = snap.screenId;
            }
            saveAndRecordPreTileGeometry(snap.windowId, snap.screenId, snap.window, snap.window->frameGeometry());
            KWin::Window* kwForLog = snap.window->window();
            qCInfo(lcEffect) << "Autotile tile request:" << snap.windowId << "QRect=" << snap.geometry
                             << "monocle=" << snap.isMonocle << "maximizeMode="
                             << (kwForLog ? maximizeModeName(kwForLog->maximizeMode()) : "no-window");
            // Scroll-batch decision inputs, at debug. The animation arm a
            // strip entry takes is chosen from these four plus the predicted
            // commit, and none of them were observable — which is a problem
            // for any window whose real frame diverges from the column rect
            // (a Wayland client with an aspect or minimum-size constraint
            // commits centred inside its column, and constrainTileGeometry
            // predicts nothing for it: it returns early for non-X11).
            if (!snap.scrollEdge.isEmpty() || snap.hasVisualPos || snap.viewDelta != 0) {
                qCDebug(lcEffect) << "  scroll entry:" << snap.windowId << "edge=" << snap.scrollEdge
                                  << "viewDelta=" << snap.viewDelta << "viewImmediate=" << snap.viewImmediate
                                  << "hasVisualPos=" << snap.hasVisualPos << "visualPos=" << snap.visualPos
                                  << "| live=" << snap.window->frameGeometry()
                                  << "predicted=" << m_effect->constrainTileGeometry(snap.window, snap.geometry)
                                  << "x11=" << snap.window->isX11Client();
            }
            // A window can only be tile-managed by one screen at a time —
            // markWindowTiled enforces the single-owner sweep itself.
            markWindowTiled(snap.screenId, snap.windowId);
            // NOTE: the parked-column visual-delta write for this entry
            // happens BELOW, after the windowed-fullscreen block, so its
            // fullscreen-bail term reads the membership this batch just
            // adopted or cleared rather than last batch's — see the block's
            // own comment down there.
            // Re-report the declared minimum size when it changed since the
            // last report. KWin exposes minSize with no change signal, and
            // the one report at announce is too early for clients that set
            // their size hints AFTER mapping — a Wine game maps hintless,
            // then pins min size to its configured resolution once the game
            // is up. From that point KWin clamps every commit to the
            // minimum, so without this re-report the engine models a column
            // the real frame can never match (seen live: a full-width game
            // over a half-width model, overlapping its neighbour). The
            // daemon's windowMinSizeUpdated widens the column and retiles.
            // A cache miss (effect-restart adoption, where no announce seeded
            // it) reports too: the call is idempotent daemon-side, so at
            // worst it confirms what the engine already holds.
            {
                const QSize declared = declaredMinSize(snap.window);
                const auto lastIt = m_effect->m_lastReportedMinSize.constFind(snap.windowId);
                if ((lastIt == m_effect->m_lastReportedMinSize.constEnd() || *lastIt != declared)
                    && m_effect->m_daemonGate.serviceRegistered) {
                    m_effect->m_lastReportedMinSize.insert(snap.windowId, declared);
                    // Watched rather than fire-and-forget, purely for the
                    // rollback: this leg is change-gated, so a lost call would
                    // leave the cache recording a size the daemon never heard
                    // and the engine modelling the old minimum until the hints
                    // move AGAIN or the window closes — the "full-width game
                    // over a half-width model" failure this block exists to
                    // fix. Both sibling announce sites roll back for the same
                    // reason. No extra cost: fireAndForget builds a watcher
                    // anyway, it just gives the caller no hook.
                    const QString minSizeWid = snap.windowId;
                    auto* minSizeWatcher = new QDBusPendingCallWatcher(
                        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Tiling,
                                                                   QStringLiteral("windowMinSizeUpdated"),
                                                                   {minSizeWid, declared.width(), declared.height()}),
                        m_effect);
                    connect(minSizeWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, minSizeWid, declared](QDBusPendingCallWatcher* pw) {
                                pw->deleteLater();
                                if (!pw->isError()) {
                                    return;
                                }
                                qCWarning(lcEffect)
                                    << "windowMinSizeUpdated failed for" << minSizeWid << pw->error().message();
                                // Only roll back OUR value: a newer report (or
                                // reportDiscoveredMinSize) may have landed while
                                // this call was in flight, and clearing that
                                // would cost a redundant re-report.
                                const auto cached = m_effect->m_lastReportedMinSize.constFind(minSizeWid);
                                if (cached != m_effect->m_lastReportedMinSize.constEnd() && *cached == declared) {
                                    m_effect->m_lastReportedMinSize.remove(minSizeWid);
                                }
                            });
                }
            }
            // Title-bar (borderless) state is driven by rules through the
            // effect's reconcileRuleHiddenTitleBar → DecorationManager path.

            // Windowed fullscreen: flip KWin fullscreen state to match the
            // batch flag, under the suppression counter so our own
            // slotWindowFullScreenChanged does not shed the tiling state the
            // flag exists to keep. Flipped BEFORE the geometry apply below so
            // the column rect overrides KWin's internal FullScreenArea
            // moveResize in the same call stack, before any client
            // round-trip. Set membership plus requested-fullscreen state is
            // what steers the fullscreen bail inside applyWindowGeometry:
            // setFullScreen flips the REQUESTED state synchronously (the
            // committed isFullScreen() lags a client round-trip), so on
            // un-flag the bail already sees requested=false and the batch
            // rect lands over KWin's restore-rect moveResize. An entry whose
            // window went KWin-fullscreen on its own (F11) and is NOT
            // flagged stays untouched: it was never in the set.
            if (KWin::Window* kwFs = snap.window->window()) {
                const bool inSet = m_effect->m_windowedFullscreenWindows.contains(snap.windowId);
                // The 5-way decision is pure and unit-tested
                // (scrolldecisions.h, test_scroll_decisions); this block
                // only performs the chosen arm's KWin/D-Bus side effects.
                const ScrollDecisions::WfsDecision wfs = ScrollDecisions::resolveWindowedFullscreenAction(
                    snap.isWindowedFullscreen, inSet, kwFs->isRequestedFullScreen(),
                    m_windowedFsClearInFlight.contains(snap.windowId));
                // A flag-off entry is the authoritative echo of a
                // clearWindowedFullscreen this effect sent — consume the
                // in-flight marker so a completed clear cannot latch the
                // adopt guard.
                if (wfs.consumeClearMarker) {
                    m_windowedFsClearInFlight.remove(snap.windowId);
                }
                // Every arm but the steady-state Refresh is a STATE CHANGE the
                // user can see, and the whole block was silent: entering,
                // adopting after an effect restart, reconciling a client's own
                // exit and releasing all looked identical from a log. Refresh
                // is excluded because it runs on every batch of every member
                // and would drown the category.
                if (wfs.action != ScrollDecisions::WfsAction::None
                    && wfs.action != ScrollDecisions::WfsAction::Refresh) {
                    qCInfo(lcEffect) << "Windowed fullscreen:" << ScrollDecisions::wfsActionName(wfs.action) << "for"
                                     << snap.windowId << "at" << snap.geometry
                                     << "(wire flag=" << snap.isWindowedFullscreen << "member=" << inSet
                                     << "kwinRequestedFs=" << kwFs->isRequestedFullScreen() << ")";
                }
                if (wfs.action == ScrollDecisions::WfsAction::Adopt) {
                    // Adopt-on-batch: also the effect-restart path, where the
                    // daemon still holds the flag for a window this effect
                    // instance has never seen. The stored rect is what the
                    // committed-ack re-assert in slotWindowFullScreenChanged
                    // applies.
                    m_effect->m_windowedFullscreenWindows.insert(snap.windowId, snap.geometry);
                    if (!kwFs->isFullScreen()) {
                        // Seed KWin's fullscreen restore rect with the COLUMN
                        // rect before the state flips: setFullScreen captures
                        // fullscreenGeometryRestore from moveResizeGeometry()
                        // at request time, and for a window this very batch
                        // scrolled in from a park that is still the PARK rect,
                        // so the eventual exit would restore the window
                        // off-screen (seen live with ghostty). The in-stack
                        // moveResize is never presented: setFullScreen's own
                        // FullScreenArea moveResize and the geometry apply
                        // below both land in the same stack. The save/restore
                        // bracket is belt-and-braces: the surrounding batch
                        // apply may already hold inGeometryApply, and this
                        // keeps the guarantee local instead of leaning on the
                        // caller's bracket (moveResize emits
                        // frameGeometryChanged synchronously on X11 and the
                        // VS-crossing detector must not re-enter).
                        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
                        m_effect->m_daemonGate.inGeometryApply = true;
                        // Scoped to THIS block, so the flag is handed back at
                        // the closing brace exactly where the hand-balanced
                        // restore used to sit — not at the end of the apply
                        // lambda, which would extend the suppression over the
                        // geometry apply below.
                        const auto fsGuard = qScopeGuard([this, prevInApply] {
                            m_effect->m_daemonGate.inGeometryApply = prevInApply;
                        });
                        // Ordering note vs the tab-swap install below: this
                        // in-stack moveResize lands the frame at the column
                        // BEFORE atScrollPark reads it, so an arriving tab
                        // taking THIS arm loses its park-origin override and
                        // derives a difference origin instead. Accepted: the
                        // arm needs Adopt (effect restart, window never seen)
                        // AND an uncommitted fullscreen state at once — a
                        // same-session arriving tab takes Refresh, which does
                        // not move the frame.
                        kwFs->moveResize(QRectF(snap.geometry));
                        applyFullScreenSuppressed(kwFs, true);
                    }
                    applyWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                } else if (wfs.action == ScrollDecisions::WfsAction::DeferredReconcile) {
                    // Flagged, member, yet fullscreen is not even REQUESTED:
                    // the client exited on its own while the daemon gate was
                    // closed, and the exit slot deferred its reconcile to
                    // exactly this moment. Deliver it now — membership
                    // drops, the daemon clears its flag and re-applies —
                    // rather than re-asserting fullscreen against the
                    // user's exit.
                    m_effect->m_windowedFullscreenWindows.remove(snap.windowId);
                    restoreWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                    if (m_effect->m_daemonGate.serviceRegistered) {
                        // Marker armed + reply-gated inside the helper: the
                        // flag-off echo above consumes it on success, and a
                        // failed clear drops it so it cannot latch.
                        dispatchWindowedFullscreenClear(snap.windowId);
                    }
                } else if (wfs.action == ScrollDecisions::WfsAction::Refresh) {
                    // Keep the stored rect current — the strip may have
                    // resized or scrolled the column since the flag went on.
                    m_effect->m_windowedFullscreenWindows.insert(snap.windowId, snap.geometry);
                    // Re-assert the layer demotion too (change-gated in KWin,
                    // free in the steady state): a manual keep-flag toggle
                    // under the hold is re-asserted away on the next batch,
                    // the same ownership KWin rules claim while they match.
                    applyWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                } else if (wfs.action == ScrollDecisions::WfsAction::Release) {
                    // isRequestedFullScreen: an un-flag landing inside our own
                    // enter round-trip (flag on, then off, before the client
                    // acks) must still un-set — the sibling self-heal arm
                    // above uses the same term for the same committed lag.
                    // Skipping it drops membership with the request standing,
                    // and the pending ack then commits fullscreen ownerless.
                    if (kwFs->isFullScreen() || kwFs->isRequestedFullScreen()) {
                        // Through the helper for its stated RAII reason — this
                        // was the other hand-rolled copy of the bracket. A leak
                        // here is silent and permanent, and it disables the
                        // very slot that notices a client leaving windowed
                        // fullscreen by itself.
                        applyFullScreenSuppressed(kwFs, false);
                    }
                    m_effect->m_windowedFullscreenWindows.remove(snap.windowId);
                    restoreWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                }
            }

            // Remember (or forget) where a parked column should be PAINTED.
            //
            // Every applied entry updates this, monocle or not and whether or
            // not the geometry itself is re-committed. It used to sit beside
            // the scroll animation decision further down, inside the
            // skip-if-already-at-target branch — so a column re-parked at the
            // rect it already held took the skip and kept the PREVIOUS batch's
            // strip position, which the paint path then drew it at for as long
            // as it stayed parked. The commit being unchanged says nothing
            // about where the column now sits on the strip.
            // Change-gated, and a REAL change pairs with damage: the entry
            // moves where the paint path draws the window (the sibling
            // removers document the same rule), and the apply that follows
            // frequently commits nothing for a parked column (its committed
            // rect is the park, stable between batches, so the no-op skip
            // fires) — with no view leg live, nothing else damages, and the
            // last presented frame keeps drawing the column at the old
            // strip position. The gate keeps the steady state (same visual
            // pos every batch) at zero repaint cost.
            //
            // The fullscreen-bail term makes the write take the REMOVE arm
            // for a self-fullscreened non-member: the apply below commits
            // nothing for it (the fullscreen bail), so an inserted relocation
            // would only be removed again by a later batch's re-evaluation —
            // insert-repaint-remove-repaint churn, since the change gate can
            // never latch on an entry that never survives. Selecting the
            // remove arm converges on a stable ABSENT entry. Evaluated AFTER
            // the windowed-fullscreen block above, so the membership term
            // reflects this batch's own adopt/clear — evaluated before it,
            // the effect-restart adopt batch read "non-member", wrongly
            // dropped a legitimate relocation and disarmed the commanded-rect
            // counter for that batch. Also read by the commanded-rect disarm
            // at the tail of this lambda.
            KWin::Window* kwcForBail = snap.window->window();
            const bool fullscreenBailSkippedCommit = snap.window->isFullScreen()
                && (!kwcForBail || kwcForBail->isRequestedFullScreen())
                && !m_effect->m_windowedFullscreenWindows.contains(snap.windowId);
            // The rect this entry actually OFFERED the client, recorded as the
            // commanded rect at the tail of this lambda so the counter-assert
            // compares the client's commit against what was really asked for.
            //
            // Only the MAXIMIZED-TO-EDGES arm reaches the record: the
            // size-continuity pass excludes that state (it holds a declared
            // rect), and the consumer requires it, so the two are mutually
            // exclusive and the re-centred column never lands here. Null when
            // this entry took no committing path.
            QRect scrollDeliveredRect;
            {
                // Stored as the column's strip POSITION and SIZE, not as a
                // precomputed translation from the batch's park rect. The paint
                // path derives the translation per read, centring the window's
                // committed frame inside the stored column, which preserves the
                // offset applyWindowGeometry's X11 constrain-and-centre pass
                // puts between the park rect and the committed frame. An
                // earlier form subtracted the committed frame from the strip
                // position up front and erased that offset, drawing a
                // fixed-size game at its column's top-left for the length of
                // every park. Deriving at read time also survives a park that
                // did not land where it was requested, which a precomputed
                // translation cannot — see ScrollVisualPlacement. For an
                // unconstrained window the committed rect IS the park rect and
                // every form agrees.
                bool visualDeltaChanged = false;
                if (snap.hasVisualPos && !fullscreenBailSkippedCommit) {
                    // Bounded on the way in, for the same reason viewDelta is
                    // and with the same constant: the wire deliberately does
                    // not validate visualX/visualY, on the stated grounds that
                    // the effect clamps the delta before it reaches the paint
                    // path. It has to actually do that — the delta is added to
                    // the committed rect at draw time AND folds into the
                    // backdrop capture, so a garbled pair would draw the column
                    // and its sample arbitrarily far off until the next batch.
                    // The clamp runs in qint64, per axis. The value is an
                    // unvalidated int off the wire, so widening before the
                    // qBound the protocol layer explicitly leans on keeps a
                    // garbled pair from overflowing on the way in — signed
                    // overflow is undefined, not a wrapped number the clamp
                    // could then rescue. Widen, clamp, then narrow.
                    //
                    // The bound is StripViewAnimator's per-leg DELTA budget,
                    // reused here as a sanity bound on an absolute strip
                    // coordinate. The two are not the same quantity: a strip is
                    // endless by design, so a far enough column's position can
                    // legitimately exceed a budget meant for one leg's travel,
                    // and it would be clamped rather than rejected. Reused
                    // deliberately, because the value is a wire-garbling guard
                    // rather than a layout limit and the animator re-clamps
                    // against the same constant downstream — but do NOT widen
                    // the constant to "fix" this without following that
                    // downstream use and the test that pins its value.
                    constexpr qint64 kMaxDelta = StripViewAnimator::kMaxViewDeltaPx;
                    ScrollVisualPlacement placement;
                    placement.stripPos =
                        QPoint(static_cast<int>(qBound(-kMaxDelta, qint64(snap.visualPos.x()), kMaxDelta)),
                               static_cast<int>(qBound(-kMaxDelta, qint64(snap.visualPos.y()), kMaxDelta)));
                    // The column rect this tile was handed. The resolver reads
                    // only its size, to centre a differently-sized commit
                    // within it.
                    placement.columnSize = snap.geometry.size();
                    // Mirrors the commit path, which is the only thing this may
                    // be derived from. The three declared-rect states are
                    // exactly the set the size-continuity guard below excludes
                    // and the set whose m_scrollOfferedColumn entry is dropped,
                    // so for a Wayland client nothing centres a smaller commit
                    // and the window sits at the column origin. A declared rect
                    // is the raw work area besides, so a client that accepts it
                    // resolves zero offsets either way.
                    //
                    // The Wayland term is NOT redundant. applyWindowGeometry
                    // pre-centres every X11 tile through constrainTileGeometry
                    // with no declared-rect exemption, so an X11 client with
                    // size hints IS committed centred inside a maximized column
                    // and the draw has to follow it there. Dropping that term
                    // trades this bug for its mirror image on X11.
                    placement.centreInColumn = !(snap.isMaximizedToEdges || snap.isMonocle || snap.isWindowedFullscreen)
                        || !snap.window->isWaylandClient();
                    const auto vit = m_effect->m_scrollVisualDelta.constFind(snap.windowId);
                    visualDeltaChanged =
                        (vit == m_effect->m_scrollVisualDelta.constEnd() || !(vit.value() == placement));
                    m_effect->m_scrollVisualDelta.insert(snap.windowId, placement);
                } else {
                    visualDeltaChanged = m_effect->m_scrollVisualDelta.remove(snap.windowId) > 0;
                }
                // The KWin::effects term matches the sibling removers across
                // the handler — one rule for every m_scrollVisualDelta damage
                // pair. Unlike those removers (rare events: window close,
                // screen change), this site is driven per batch and the edge
                // auto-scroll heartbeat re-derives every parked column's
                // relocation from the moving strip each tick, so this fires
                // at ~60 Hz for the length of a drag hold — full-canvas
                // damage there recomposites every unrelated monitor. Scoped
                // to the window's own output; the relocation cannot paint
                // beyond it (the paint path clips parked columns to their
                // output), and Full stays as the unresolvable fallback.
                if (visualDeltaChanged && KWin::effects) {
                    if (KWin::LogicalOutput* vout = m_effect->outputForScreenId(snap.screenId)) {
                        KWin::effects->addRepaint(KWin::Rect(vout->geometry()));
                    } else {
                        KWin::effects->addRepaintFull();
                    }
                }
            }

            // Column maximize: mirror the engine's state onto KWin's maximize
            // bit, so the titlebar button agrees with the strip however the
            // maximize was reached — the interception, the Meta+Alt+F
            // shortcut, or a width verb that happened to land full width.
            //
            // Runs BEFORE the geometry apply below for the monocle arm's
            // reason: the bit has to be set first so the column rect is what
            // overrides KWin's own maximize-area moveResize, in the same call
            // stack and inside the same inGeometryApply guard, rather than
            // racing it a frame later.
            //
            // The 3-way decision is pure and unit-tested (scrolldecisions.h);
            // this block only performs the chosen arm's side effects.
            // Screen-gated like every sibling arm in this lambda (the
            // size-continuity block, the commanded-rect writes, the centring
            // shed). The wire flag can only be true for a scroll entry today,
            // so this is defensive rather than a live gap — but the per-window
            // applies are STAGGERED and outlive this function, and
            // setScrollingScreens does not bump the stagger generation, so a
            // pending apply from a batch built while the screen was scrolling
            // can land after the flip has already released the claim, taking
            // membership back with nothing to hand it to.
            // Set ONLY where this iteration actually wrote KWin's maximize
            // bit, because the geometry apply further down needs to know
            // whether THIS batch is the one that changed the maximize state.
            //
            // A membership-vs-wire delta looks like the obvious signal and is
            // not a transition detector: releaseMaximizedToEdges RETAINS
            // membership on its fullscreen and mid-gesture arms, so membership
            // stays true while the wire flag stays false and the delta then
            // reads true on EVERY following batch, with no
            // windowMaximizedStateAboutToChange having fired to refresh the
            // captured departure rect. The Apply arm's own fullscreen/gesture
            // skip has the same shape in the other direction: it takes
            // membership without calling maximize().
            bool maximizeBitWrittenThisBatch = false;
            if (KWin::Window* kwMax = isScrollingScreen(snap.screenId) ? snap.window->window() : nullptr) {
                // requestedMaximizeMode, not the committed maximizeMode: the
                // committed bit trails a client round-trip on Wayland, the
                // same lag the windowed-fullscreen arm above reads
                // isRequestedFullScreen for and documents. On the committed
                // value every batch landing inside that round-trip window
                // re-resolves to Apply and re-issues maximize(MaximizeFull),
                // each one carrying a moveResize to the maximize area that
                // the geometry apply below immediately overwrites — and on a
                // scrolling strip batches arrive at wheel-tick rate, so that
                // window is crossed routinely rather than rarely.
                // PEEKED, not consumed. The marker's lifetime is the round
                // trip, not one batch: batches arrive at wheel-tick rate on a
                // scrolling strip, so more than one pre-toggle batch can be in
                // flight and consuming here would leave the second unguarded.
                // It is also read per entry rather than once per batch,
                // because the per-window applies are staggered and a later
                // entry runs after the earlier ones have returned.
                const bool kwinAlreadyFull = kwMax->requestedMaximizeMode() == KWin::MaximizeFull;
                const ScrollDecisions::MaximizeAction maxAction = ScrollDecisions::resolveMaximizeToEdgesAction(
                    snap.isMaximizedToEdges, m_maximizedToEdgesWindows.contains(snap.windowId), kwinAlreadyFull,
                    maximizeToggleInFlight(snap.windowId));
                if (maxAction == ScrollDecisions::MaximizeAction::Apply) {
                    // Membership BEFORE the compositor call, so a synchronous
                    // re-entry through maximize() (X11 emits
                    // frameGeometryChanged inside it) already sees the
                    // handler owning the bit rather than reading it as a
                    // stray user maximize.
                    m_maximizedToEdgesWindows.insert(snap.windowId);
                    // The fullscreen guard the sibling ledgers take: a
                    // windowed-fullscreen tile in a maximized column is a
                    // legitimate pairing, and maximize() on a presenting
                    // surface would moveResize it down to geometryRestore.
                    // Membership still stands — the flag is a mirror, and the
                    // Apply arm re-runs on the next batch once the client
                    // leaves fullscreen.
                    //
                    // The drag terms are the ones every other compositor-
                    // touching block in this lambda carries (the tab-swap
                    // install, the commanded-rect record, the strip offer):
                    // maximize() moveResizes to the maximize area, and the
                    // geometry apply that would override it defers mid-gesture
                    // (applyWindowGeometry, allowDuringDrag=false), so a batch
                    // arriving while the user drags would snap the window to
                    // full size under the pointer with nothing committed
                    // behind it until the gesture ends. Membership is still
                    // taken above, so the decision resolves to Apply again
                    // (inSet, not kwinMaximized) and re-applies for real: it
                    // converges and cannot loop.
                    //
                    // What is NOT guaranteed is WHEN. The gesture end replays
                    // geometry only; it does not re-enter this arm or request
                    // a retile, and the engine emits on change, so a drag that
                    // ends without changing the strip schedules no batch. The
                    // window is then left un-maximized with the engine
                    // believing otherwise until some later batch lands. That is
                    // the accepted trade against snapping a window to the full
                    // maximize area under the user's pointer mid-gesture.
                    if (!kwMax->isFullScreen() && !kwMax->isRequestedFullScreen() && !snap.window->isUserMove()
                        && !snap.window->isUserResize()) {
                        applyMaximizeSuppressed(kwMax, KWin::MaximizeFull);
                        // NOT unconditionally true. Apply also covers the
                        // ADOPT case (!inSet with the bit already held, an
                        // effect restart against a daemon that kept the
                        // state), where maximize() is called on a window
                        // already at MaximizeFull and emits nothing — so no
                        // windowMaximizedStateAboutToChange fires and the
                        // captured departure rect belongs to an arbitrarily
                        // older episode. Claiming a write there would hand the
                        // origin arm the same stale rect this gate exists to
                        // keep out. The monocle arm below takes the identical
                        // precaution.
                        maximizeBitWrittenThisBatch = !kwinAlreadyFull;
                    }
                } else if (maxAction == ScrollDecisions::MaximizeAction::Release) {
                    maximizeBitWrittenThisBatch = releaseMaximizedToEdges(snap.windowId, snap.window);
                }
            }

            // MAXIMIZE-EDGE MARKER, consumed here: once per window per batch,
            // above the monocle/else split and above every arm that can decline
            // to commit, because consumption is what bounds the marker to a
            // single placement.
            //
            // The two arms below used to take it themselves, and both leaked.
            // The monocle arm had it as the last operand of an `||` whose left
            // side is true exactly when that arm just wrote the bit, so `||`
            // short-circuited and the marker went unconsumed; the else arm's
            // take sits inside `if (!skipMoveResize)`, which a Wayland client
            // re-tiled to the zone it already holds takes as its steady state.
            // Either way the entry survived to be claimed by the next,
            // unrelated placement of the same window, which then animated a
            // maximize morph from a stale departure rect. Hoisting is the fix
            // for both, and for the monocle arm's null-KWin::Window fallback
            // that never took it at all.
            //
            // Safe above the writes below because no write routed through
            // applyMaximizeSuppressed can re-arm it: that helper stamps its own
            // authorship and noteMaximizeEdge declines on the stamp. Every
            // maximize write this iteration makes goes through it.
            //
            // What the hoist COSTS, stated because it is a real trade and not
            // a free fix: a batch that reaches here and then declines to commit
            // now destroys the marker, so "no-op batch, then the real answering
            // batch a moment later" loses the maximize leg where it used to
            // survive. That is the better half. A no-op batch answers nothing,
            // so the entry it used to leave behind was claimed by whatever
            // placement came next — commonly a scroll or focus retile with no
            // relation to the press — which then morphed from a departure rect
            // belonging to an older episode. A missed leg plays the wrong pack
            // once; a stale claim animates the wrong motion from the wrong
            // place, and does it on a placement the user did not ask for.
            const bool userMaximizeEdge = m_effect->m_shaderManager.takeRecentMaximizeEdge(snap.window);

            if (snap.isMonocle) {
                if (KWin::Window* kw = snap.window->window()) {
                    // REQUESTED, not committed, for the reason the column arm
                    // above spells out: on Wayland the committed bit trails a
                    // client round trip, so a batch landing inside that window
                    // reads a user-imposed maximize as absent, takes membership
                    // for a bit the effect never set, and later hands back a
                    // maximize it never owned.
                    const bool wasAlreadyMaximized = (kw->requestedMaximizeMode() == KWin::MaximizeFull);
                    // SUPPRESSION HELD ACROSS THE WHOLE ARM, not merely across
                    // the maximize write inside applyMaximizeSuppressed below.
                    // The bracket this replaces was a hand-rolled ++/-- with a
                    // qScopeGuard declared here, so it spanned the membership
                    // insert, the departure-rect read and the terminal geometry
                    // apply as well; narrowing it to the write alone would let
                    // anything that apply emits reach the maximize lambda
                    // unsuppressed, and — now that the lambda arms a
                    // maximize-edge marker — arm one with no authorship stamp
                    // behind it. The counter is a counter, so the helper's own
                    // bracket nests inside this one harmlessly.
                    ++m_suppressMaximizeChanged;
                    const auto maxSuppressGuard = qScopeGuard([this] {
                        --m_suppressMaximizeChanged;
                    });
                    // Same two guards the column arm above carries, and for the
                    // same reasons. maximize() has no fullscreen conditional, so
                    // on a presenting surface it moveResizes down to the restore
                    // rect; and mid-gesture it snaps the window to full size
                    // under the pointer, because the geometry apply below defers
                    // during a drag while this call does not.
                    //
                    // The membership insert stays OUTSIDE the gate, matching the
                    // column arm: a maximize claim legitimately stands while the
                    // bit is not yet held, so a later batch can pay it, and
                    // unmaximizeMonocleWindow is itself guarded and cannot hand
                    // back a bit it never took.
                    //
                    // Through applyMaximizeSuppressed rather than a hand-rolled
                    // ++/-- around a bare maximize(), which is what this arm
                    // used to hold. That helper owns the suppression bracket
                    // (leak one increment and isSuppressingMaximizeChanged()
                    // stays true for the session, so the interception never
                    // fires again) AND the authorship stamp that keeps the
                    // maximize-edge marker from arming on the effect's own
                    // write. A second copy of the bracket would have to grow
                    // the stamp too, and silently mis-animate if it did not.
                    bool monocleBitWritten = false;
                    if (!kw->isRequestedFullScreen() && !snap.window->isUserMove() && !snap.window->isUserResize()) {
                        applyMaximizeSuppressed(kw, KWin::MaximizeFull);
                        monocleBitWritten = !wasAlreadyMaximized;
                    }
                    if (!wasAlreadyMaximized) {
                        m_monocleMaximizedWindows.insert(snap.windowId);
                    }
                    // Same departure-rect fix the column arm takes, for the
                    // same reason and by the same mechanism. maximize() above
                    // has already moved the window to KWin's maximize area, so
                    // a leg departing from the live frame animates almost
                    // nothing growing and nothing at all shrinking. The
                    // capture at windowMaximizedStateAboutToChange is the only
                    // place the pre-monocle rect still exists, and this arm's
                    // own maximize() is what just refreshed it.
                    //
                    // Gated on that call having actually happened AND on the
                    // state having changed: on the already-maximized path
                    // maximize() emits nothing, so the entry would be stale.
                    // A maximize the USER took also owns this leg, not just one
                    // this arm authored: on a monocle screen the press reaches
                    // KWin directly (nothing intercepts it here), so
                    // monocleBitWritten is false for it and the placement that
                    // follows would otherwise land as a plain snapIn. The
                    // marker was consumed above the split, so the `||` here is
                    // a plain test of two locals with nothing to short-circuit
                    // past.
                    const bool monocleMaximizeLeg = monocleBitWritten || userMaximizeEdge;
                    QRectF monocleOrigin;
                    if (monocleMaximizeLeg) {
                        const QRectF preMaximize = m_effect->m_shaderManager.preMaximizeFrame(snap.window);
                        if (preMaximize.isValid() && !preMaximize.isEmpty()) {
                            monocleOrigin = preMaximize;
                        }
                    }
                    // The leg that MAXIMIZES the tile is the user's maximize
                    // event, not a snap: it rides window.movement.maximize so
                    // the pack assigned there plays, the same node the
                    // KWin-native path (beginMaximizeShaderMorph) uses for an
                    // unmanaged window. The two never double-install: on X11
                    // the native handler skips this window under the
                    // suppression counter (isSuppressingMaximizeChanged), and
                    // on Wayland its later echo short-circuits onto this very
                    // leg (window_connections.cpp spells out the absorb). A
                    // batch that re-asserts an already-maximized monocle tile
                    // is a plain placement and stays on snapIn.
                    m_effect->applyWindowGeometry(snap.window, snap.geometry, /*allowDuringDrag=*/false,
                                                  /*skipAnimation=*/false,
                                                  monocleMaximizeLeg ? PhosphorAnimation::ProfilePaths::WindowMaximize
                                                                     : PhosphorAnimation::ProfilePaths::WindowSnapIn,
                                                  monocleOrigin);
                } else {
                    m_effect->applyWindowGeometry(snap.window, snap.geometry);
                }
            } else {
                // True only when THIS call handed KWin's maximize bit back for
                // a window PlasmaZones itself maximized for monocle. It routes
                // the geometry apply below onto window.movement.maximize and
                // anchors its departure at the captured pre-restore rect, the
                // same pairing the column Release arm gets through
                // maximizeBitWrittenThisBatch.
                const bool monocleBitReleased = unmaximizeMonocleWindow(snap.windowId);
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
                // intentionally absorbed by the m_daemonGate.inGeometryApply guard
                // set at the top of this lambda — a refactor that moves or
                // narrows that guard reintroduces the ballooning re-entry.
                // The fullscreen terms match unmaximizeMonocleWindow's guard for
                // the same reason: maximize() has no fullscreen conditional, so
                // on a still-fullscreen window it moveResizes to geometryRestore
                // and shrinks the presentation. A window that went fullscreen
                // while monocle now keeps its membership, so it can reach this
                // arm still fullscreen once the batch demotes it to a plain tile.
                //
                // A MAXIMIZED COLUMN is exempt. Its bit is not a user
                // maximize the tiler has to clear, it is this handler's own
                // mirror of engine state, and clearing it here would strip it
                // on every batch — the flag would never survive to be seen.
                // The exemption is on the WIRE flag rather than on
                // membership, so the very first batch that maximizes a column
                // does not clear the bit the arm below is about to set.
                //
                // The steady state is precedented rather than novel: the
                // monocle arm directly below holds a window at MaximizeFull
                // against a gapped, non-maximize-area rect and re-asserts it
                // every batch, and has shipped that way. What protects both is
                // that KWin's maximize-area re-assert is event-driven rather
                // than continuous, and that the geometry apply lands last in
                // this call stack inside the same inGeometryApply guard.
                //
                // The counter-assert is NOT part of that cover, contrary to an
                // earlier reading: it is gated on !isWaylandClient(), so it
                // does not exist for a Wayland column at all, and it is capped
                // at a few asserts per rolling second besides.
                //
                // The sharpest case is the WORK-AREA CHANGE, and it has been
                // measured rather than argued: KWin re-maximizes every
                // MaximizeFull window when the work area moves, and a
                // MULTI-TILE column differs from the maximize area by its
                // whole cross extent rather than by a monocle window's few gap
                // pixels, so a re-assert there would put every tile full
                // screen over its siblings.
                //
                // Driven in a nested compositor on a two-tile maximized column
                // (both tiles at the full main extent, each holding half the
                // cross extent, both carrying MaximizeFull) across a real
                // logical-geometry change of 1600x900 to 1280x720: both tiles
                // held their stacked column rects and neither took KWin's
                // maximize area. The engine's apply lands last and wins.
                if (KWin::Window* kw = snap.window->window(); kw && !snap.isMaximizedToEdges
                    && kw->maximizeMode() != KWin::MaximizeRestore && !kw->isFullScreen()
                    && !kw->isRequestedFullScreen()) {
                    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
                }
                QRect geo = snap.geometry;

                // Size continuity for a strip client that will not take its
                // column.
                //
                // Every placement offering the COLUMN size is a resize, and a
                // client that enforces its own geometry re-derives a size from
                // it each time. Its answer is not bit-stable — the same
                // 1908x2052 column came back as 1908x1073 and then 1911x1074 —
                // so the window resizes by a few pixels on every scroll step,
                // and no placement can ever take the pure-move path that
                // avoids KWin's configure re-anchor (see applyWindowGeometry).
                //
                // So stop re-asking. Once a client has answered for a given
                // column SIZE, offer the size it actually holds, centred in the
                // column. That is a pure move, which it cannot renegotiate, so
                // the size stops drifting. The offer only changes when the
                // COLUMN changes.
                //
                // Keyed on the column size, never on position — that is the
                // whole point. An earlier attempt gated on the target being
                // off-screen, which made a column scrolling in and out
                // alternate between two sizes and resize its way across the
                // strip. Position must not enter, because a column keeps its
                // size wherever it sits.
                //
                // Inert for a window that accepts its column: the sizes match,
                // so the offer is the column rect unchanged and every branch
                // below sees exactly what it saw before.
                //
                // A MAXIMIZED-TO-EDGES column is exempt, alongside monocle and
                // windowed fullscreen and for the same reason: in all three the
                // rect is DECLARED state, and honouring the client's own
                // smaller answer instead defeats the state outright. The
                // mechanism above exists to stop a size renegotiation that is
                // only a few pixels wide; here the gap is the whole difference
                // between the raw work area and whatever the client prefers, so
                // "offer the size it actually holds, centred" commits a
                // maximized window at its pre-maximize size in the middle of
                // the screen.
                //
                // It is also self-sustaining once entered, which is what made
                // this present as "maximize sometimes does nothing". The first
                // batch after the toggle finds no entry for the new column
                // size, offers the full raw area and records it; a client that
                // answers smaller then makes EVERY later batch for that column
                // read columnUnchanged and re-issue the shrunken centred rect,
                // and the entry is deliberately never removed. Since a maximized
                // column's batches arrive on any focus change or scroll tick,
                // the full-width frame is typically gone before the user sees it.
                // Null unless this batch is a plain strip entry that reaches
                // the record below; see the capture site for why the two are
                // separated.
                QRect stripOfferedColumn;
                if (isScrollingScreen(snap.screenId) && !snap.isMonocle && !snap.isWindowedFullscreen
                    && !snap.isMaximizedToEdges && snap.window->isWaylandClient()) {
                    const QSize columnSize = geo.size();
                    const QSize committedSize = snap.window->frameGeometry().toRect().size();
                    const auto offeredIt = m_effect->m_scrollOfferedColumn.constFind(snap.windowId);
                    // SIZE only. The column's position changes on every scroll
                    // step while its size does not, and it is the size that
                    // decides whether the client has answered for this column.
                    const bool columnUnchanged =
                        offeredIt != m_effect->m_scrollOfferedColumn.constEnd() && offeredIt->size() == columnSize;
                    // The whole rect is captured: the commit-time correction
                    // below needs the column's position to centre within it.
                    //
                    // CAPTURED here, RECORDED after the commit decision further
                    // down. The entry means "this client has been offered this
                    // column", and the batch has not offered anything yet — the
                    // apply below can still be skipped as redundant, deferred
                    // to windowFinishUserMovedResized mid-gesture, or bailed on
                    // the non-member fullscreen path. Recording it here made the
                    // NEXT batch read columnUnchanged against a column the
                    // client was never sent, skip offering it, and hand back
                    // whatever size the window happens to be holding — which
                    // never self-corrects, because the offer looks answered.
                    // The commanded-rect sibling already guards the same three
                    // cases; this is the missing half of that pairing.
                    stripOfferedColumn = geo;
                    // The whole carry-forward decision, truth-tabled in
                    // scrolldecisions.h. declaredRect is false here by
                    // construction — the enclosing guard already excluded all
                    // three declared-rect states — but it is passed rather than
                    // hardcoded so the predicate stays complete on its own
                    // terms and the table documents why they are excluded.
                    if (ScrollDecisions::mayCarryCommittedSize(/*declaredRect=*/false, columnUnchanged, committedSize,
                                                               columnSize)) {
                        // Centred the same way the paint resolver centres
                        // (scrollVisualTranslationFor), so the drawn and
                        // committed positions agree: same toRect() rounding on
                        // the size, and the same clamp at zero. The clamp is
                        // what constrainTileGeometry already does for the X11
                        // pre-centre — a frame whose minimum exceeds its column
                        // stays anchored at the column's origin instead of
                        // shifting past its edge.
                        //
                        // isEmpty rather than isValid: QSize::isValid() admits
                        // 0x0, and a degenerate mid-unmap commit would then
                        // centre the window by the whole column.
                        geo = QRect(geo.x() + qMax(0, columnSize.width() - committedSize.width()) / 2,
                                    geo.y() + qMax(0, columnSize.height() - committedSize.height()) / 2,
                                    committedSize.width(), committedSize.height());
                        qCDebug(lcEffect) << "scroll size continuity:" << snap.windowId << "column=" << columnSize
                                          << "holding=" << committedSize << "offer=" << geo;
                    }
                }
                // DROP THE OFFERED-COLUMN ENTRY for every declared-rect state,
                // which is the same set the size-continuity guard above
                // excludes. Taking that exemption means nothing refreshes the
                // entry for the length of the episode, and the entry is not
                // read only by the guard above: the reactive centring pass in
                // window_connections.cpp reads it on every frame-geometry
                // change with no declared-rect exclusion of its own, so a
                // stale entry there centres the window inside a column it no
                // longer occupies. Windowed fullscreen removes the entry on
                // enter for exactly this reason, and the monocle arm at the
                // bottom of this lambda already drops it on every batch, but
                // nothing removed it for a column that is merely maximized to
                // edges. The predicate covers all three anyway so it reads as
                // the one set the size-continuity guard above excludes, and
                // the monocle leg is a harmless earlier repeat of a removal
                // that batch performs regardless.
                //
                // Removing an absent key costs nothing, so no isWaylandClient
                // term is needed: an entry is only ever written for one.
                if (isScrollingScreen(snap.screenId)
                    && (snap.isMaximizedToEdges || snap.isMonocle || snap.isWindowedFullscreen)) {
                    m_effect->m_scrollOfferedColumn.remove(snap.windowId);
                }
                // A maximized-to-edges column takes the exemption above, so it
                // never reaches that assignment — but it is the ONE Wayland
                // population the counter-assert exists for (KWin's own
                // maximize-area re-assert is its external mover), and the arm
                // at the bottom of this lambda refuses to record a commanded
                // rect while this is null. Without this the counter would be
                // permanently disarmed for exactly the windows it protects.
                //
                // The rect is `geo` unchanged, and that is now the honest
                // record: the sibling arm's warning about recording "the raw
                // column instead" was written when the size-continuity pass
                // did NOT exclude maximized columns, so the delivered rect
                // could be a centred commit the counter had never offered.
                // With the exemption in place the column rect IS what gets
                // delivered, so the two agree by construction.
                if (isScrollingScreen(snap.screenId) && snap.isMaximizedToEdges && !snap.isMonocle
                    && !snap.isWindowedFullscreen && snap.window->isWaylandClient()) {
                    scrollDeliveredRect = geo;
                }

                // For Wayland windows being retiled to the same zone, skip the
                // moveResize if the window was previously centered in this zone.
                // This prevents flicker where the window jumps from its centered
                // position back to the zone origin, then gets re-centered by the
                // reactive pass in slotWindowFrameGeometryChanged. It also avoids
                // flooding the Wayland client with configure events which can
                // freeze terminals like Ghostty.
                //
                // The re-centring used to run off a 200ms QTimer, which is what
                // the flicker window described above was; it has been reactive
                // since the timer was removed, so there is no delay to name here.
                // The skip is deliberately CONTAINMENT, not fill: an entry only
                // exists for a window the centring pass placed undersized in
                // exactly this zone, so "still inside the zone" means "still
                // sitting where we centred it" for the population the skip was
                // built for. What makes that reading safe is that the entry can
                // no longer be stamped from a stale mid-configure frame — the
                // reactive pass now refuses to run inside the apply bracket and
                // while a resize configure is in flight — so a live entry
                // records a genuine refusal, not a lost race. Without those
                // guards this branch was a self-perpetuating latch: a raced
                // centring stamped the entry, and the skip then suppressed the
                // re-assert that would have fixed it, on every later batch.
                bool skipMoveResize = false;
                if (snap.window->isWaylandClient()) {
                    auto prevIt = m_centeredWaylandZones.find(snap.windowId);
                    if (prevIt != m_centeredWaylandZones.end() && prevIt.value() == geo) {
                        const QRectF actual = snap.window->frameGeometry();
                        // Window is still within the zone bounds — already
                        // centered. Exclusive-edge arithmetic (x + width), like
                        // the centring bounds clamp below, because QRectF::right()
                        // and QRect::right() disagree by one; a symmetric 1px
                        // tolerance on every edge covers fractional-scale snap.
                        if (actual.x() >= geo.x() - 1.0 && actual.y() >= geo.y() - 1.0
                            && actual.x() + actual.width() <= geo.x() + geo.width() + 1.0
                            && actual.y() + actual.height() <= geo.y() + geo.height() + 1.0) {
                            skipMoveResize = true;
                            qCDebug(lcEffect) << "Skipping redundant moveResize for centered Wayland window"
                                              << snap.windowId << "zone=" << geo;
                        }
                    }
                }

                // Record the strip offer now that the commit decision is known.
                // Three ways this batch can fail to deliver the column it
                // computed: the redundant-apply skip just above, a deferred
                // commit mid-gesture (applyWindowGeometry hands those to
                // windowFinishUserMovedResized), and the non-member fullscreen
                // bail, which commits nothing at all. In each case the client
                // has not been offered THIS column, so nothing is written — a
                // recorded offer that never went out would be read as answered.
                //
                // Not written, and equally NOT removed. Any entry already there
                // describes a column an earlier batch genuinely delivered and
                // the client genuinely answered, and this batch failing to
                // deliver says nothing about that. Removing it would re-offer
                // the full column on the next batch and restart the size
                // renegotiation this whole mechanism exists to end — every
                // batch arriving during a drag or resize takes this path, so
                // that would fire for the length of any gesture. Staleness is
                // decided by the size comparison above, which a genuine column
                // change fails on its own; membership loss is handled by the
                // teardown removers.
                if (!stripOfferedColumn.isNull() && !skipMoveResize && !snap.window->isUserMove()
                    && !snap.window->isUserResize() && !fullscreenBailSkippedCommit) {
                    m_effect->m_scrollOfferedColumn.insert(snap.windowId, stripOfferedColumn);
                }

                if (!skipMoveResize) {
                    m_centeredWaylandZones.remove(snap.windowId);
                    // Scrolling strip: scrollEdge names the screen edge this
                    // column's motion belongs to — one of FOUR since wire v5
                    // ("left"/"right" on a horizontal strip, "top"/"bottom" on
                    // a vertical one), so nothing here may treat "not left" as
                    // "right". It is NOT recoverable from the geometry — the
                    // park position is direction-agnostic (below the union of
                    // all outputs), so the rect cannot say which way the user
                    // scrolled.
                    //
                    // Two cases, and they need opposite treatment:
                    //  - ARRIVING (target is on the window's own screen): the
                    //    window was parked somewhere unrelated, so animate it
                    //    in from just outside the named edge. This is the
                    //    origin parking used to supply implicitly, back when
                    //    position and direction were the same number.
                    //  - LEAVING (target is a park, off the screen): the window
                    //    must be seen sliding OUT past the named edge, but its
                    //    committed rect is the park, which is below every
                    //    output. Animating to the park directly would sweep
                    //    it downwards across the whole screen instead of out
                    //    by the edge. So the animation ends just past the named
                    //    edge while the commit still goes to the park. Both
                    //    are off-screen, so the step between them when the
                    //    animation finishes is never visible. Accepted cost:
                    //    the animation's swept bounds touch the neighbouring
                    //    output's coordinate range, so that output takes
                    //    full transformed repaints for the slide's duration
                    //    even though the cull draws nothing there (the
                    //    committed geometry never enters its render list).
                    //
                    // The edge math resolves the screen id to its OUTPUT
                    // geometry; scrolling is assigned per physical screen, so
                    // a virtual sub-screen spelling never reaches this path.
                    QRectF originOverride;
                    QRectF visualTargetOverride;
                    bool skipScrollAnimation = false;
                    // The rect applyWindowGeometry will actually COMMIT for
                    // this request. For an X11 client with size hints that is
                    // the constrained frame centred in the column, NOT the
                    // column rect — and every origin/target below that means
                    // "the committed rect" must be built from it, or a
                    // fixed-size game gets a real animation leg from the
                    // full-column rect to its centred frame on every batch
                    // (drawn anchored at the column's top-left, the top of
                    // the screen) instead of the degenerate leg these
                    // branches intend. Identical to `geo` for everything
                    // else, Wayland included.
                    //
                    // Deliberate consequence, not an accident: the constrained
                    // rect is inset from the column by the centring offset, so
                    // for a size-constrained X11 column that straddles its
                    // output by LESS than that offset the intersects() tests
                    // below can now answer differently than they did against
                    // the raw column rect — `arriving` can read false where it
                    // read true, and the vertical-park test can skip an
                    // animation it used to run. That is the point: those
                    // predicates are asking where the window will actually be,
                    // and the answer is the constrained frame.
                    // Normalized first, matching what applyWindowGeometry does
                    // before it constrains the same rect: this is a D-Bus
                    // boundary, and an inverted wire rect would otherwise hand
                    // KWin a negative frame size here while the apply rejected
                    // it downstream, leaving the two disagreeing about the
                    // predicted commit.
                    const QRect committedGeo = m_effect->constrainTileGeometry(snap.window, geo.normalized());
                    // Shared four-edge anchor for both LEAVING treatments
                    // below: move the rect just past the named departure edge
                    // of the screen. Four departure edges since v5:
                    // "left"/"right" on a horizontal strip, "top"/"bottom" on
                    // a vertical one. Folding the two vertical tokens into an
                    // else would anchor them past the RIGHT screen edge and
                    // slide every vertical park sideways across the
                    // neighbouring output.
                    //
                    // QRect::right()/bottom() are x+width-1 and y+height-1,
                    // so +1 sits one past the edge. screenRect must stay a
                    // QRect (KWin::Rect's right()/bottom() are x+width and
                    // y+height and would shift the slide by a pixel).
                    const auto anchorPastEdge = [](QRect rect, const QRect& screenRect, const QString& edge) {
                        if (edge == QLatin1String("left")) {
                            rect.moveLeft(screenRect.left() - rect.width());
                        } else if (edge == QLatin1String("top")) {
                            rect.moveTop(screenRect.top() - rect.height());
                        } else if (edge == QLatin1String("bottom")) {
                            rect.moveTop(screenRect.bottom() + 1);
                        } else {
                            rect.moveLeft(screenRect.right() + 1);
                        }
                        return rect;
                    };
                    if (snap.hasVisualPos) {
                        // Parked, but drawn at its real strip position and
                        // carried by the view like every other column. There is
                        // no per-window motion left to describe, so the leg is
                        // deliberately degenerate — the edge-anchored slide-out
                        // below would fight the view offset for the same pixels.
                        originOverride = QRectF(committedGeo);
                    } else if (snap.viewDelta != 0 && immediateViewScreens.contains(snap.screenId)) {
                        // Heartbeat-driven view motion (drag edge
                        // auto-scroll): no view leg ran, the paint offset is
                        // zero, and the ~60 Hz commits are the motion — so
                        // every carried window is placed outright. This
                        // covers arriving-from-park entries too: they simply
                        // appear at the edge and the next ticks carry them
                        // in, which is the niri behaviour.
                        originOverride = QRectF(committedGeo);
                    } else if (snap.viewDelta != 0 && startedViewScreens.contains(snap.screenId)) {
                        // Carried by the view: the strip's own spring moves
                        // this window, so the per-window animation must cover
                        // only what the view does NOT explain — the residual.
                        //
                        // Gated on the spring having actually STARTED for
                        // this screen, not on the wire delta alone: when
                        // applyBatchDelta declined (no clock, animations
                        // off), the paint offset is zero and an origin
                        // placed a delta behind the target pops the column
                        // backwards and slides it double. Declined screens
                        // fall through to the scrollEdge / plain branches,
                        // which already handle the no-view case.
                        //
                        // The paint position is `animatedRect + viewOffset`,
                        // and the offset starts at exactly viewDelta. So for
                        // the first frame to land where the window is now, the
                        // animation has to start a delta BEHIND its current
                        // rect. Everything that then differs from the target is
                        // the residual, and it animates on its own.
                        //
                        // The pure-scroll case falls out rather than being
                        // special-cased: when the window's whole movement is
                        // the view's, this origin IS the target, startAnimation
                        // reports the leg degenerate, and no second spring
                        // exists to desync from the first. An edge column whose
                        // width changed in the same batch keeps a real leg, so
                        // its width interpolates while its position rides the
                        // strip — which is why the clamp needs no special
                        // handling of its own.
                        //
                        // Outranks the scrollEdge branch below on purpose: an
                        // ARRIVING column carries both, and its true pre-scroll
                        // strip position beats a point synthesized just outside
                        // the screen edge, because that is where it actually
                        // was.
                        // Direction of THIS entry, asked of the committed
                        // target: a target off the union of every output is a
                        // park, and a park-bound (LEAVING) entry needs the
                        // opposite treatment from an arriving one in both arms
                        // below. The historical code assumed edge+viewDelta
                        // means ARRIVING and edge-less+viewDelta means
                        // on-strip travel — but a mid-strip close's re-anchor
                        // batch carries viewDelta on EVERY entry, including
                        // the parks the re-anchor just created, and routing
                        // those through the arrival arms produced the two
                        // artifacts fixed here: an unmasked pop to the park
                        // (edged), and a real animated leg sweeping the
                        // window visibly down to the park row (edge-less) —
                        // the "closing a mid-strip window slides down off the
                        // screen" bug.
                        const bool viewLeavingToPark = rectAtScrollPark(committedGeo);
                        if (!snap.scrollEdge.isEmpty() && viewLeavingToPark) {
                            // LEAVING to a park with a named edge, carried by
                            // the same batch's view travel. Give it the
                            // scrollEdge branch's leaving treatment: the
                            // commit still goes to the park, but the VISIBLE
                            // motion is redirected to just past the named
                            // edge so the window slides out as itself instead
                            // of popping straight to the (off-screen) park.
                            // Same edge math as the scrollEdge branch below;
                            // same teleport fallback when no output resolves.
                            const KWin::LogicalOutput* out = m_effect->outputForScreenId(snap.screenId);
                            const QRect screenRect = out ? QRect(out->geometry()) : QRect();
                            if (!screenRect.isValid()) {
                                qCDebug(lcEffect) << "scroll batch: no output for" << snap.screenId << "- teleporting"
                                                  << snap.windowId << "to its target";
                                skipScrollAnimation = true;
                            } else if (atScrollPark(snap.window)) {
                                // Already sitting at a park: both ends of the
                                // slide-out would be off the union, so the
                                // animated leg (and its shader arm) buys
                                // nothing visible. Commit plain, matching the
                                // at-park-now treatment of the sibling arms.
                                skipScrollAnimation = true;
                            } else {
                                const KWin::RectF cur = snap.window->frameGeometry();
                                const QRect liveRect =
                                    QRect(qRound(cur.x()), qRound(cur.y()), qRound(cur.width()), qRound(cur.height()));
                                visualTargetOverride = QRectF(anchorPastEdge(liveRect, screenRect, snap.scrollEdge));
                            }
                        } else if (!snap.scrollEdge.isEmpty()) {
                            // ARRIVING from a park. Its live frameGeometry is
                            // the park itself — below the union of all outputs
                            // — which is not a visual position at all, so
                            // differencing against it would start the leg from
                            // somewhere off the bottom of the desktop.
                            //
                            // A column arriving has no per-window motion to
                            // describe: the view alone brought it back. Making
                            // the origin the target is what says that, because
                            // the paint position is origin + offset, and the
                            // offset already starts a delta out — which is
                            // exactly where this column was before the scroll.
                            originOverride = QRectF(committedGeo);
                        } else {
                            // Rounded to integers, exactly as the sibling
                            // leaving-column branch below does, and for a
                            // reason that is not cosmetic. The animation
                            // target is an integer QRect, while frameGeometry
                            // is qreal — and AnimatedValue::start decides a
                            // leg is degenerate with qFuzzyIsNull on a 4-D
                            // distance, an ABSOLUTE 1e-12 with no pixel
                            // epsilon. On a fractional-scale output the
                            // sub-pixel residue therefore reads as a real leg:
                            // a spring starts on every wheel tick, arms a
                            // shader transition, and the resulting
                            // hasAnimation defeats the next batch's
                            // already-at-target no-op skip, forcing a
                            // redundant moveResize per tick. Rounding makes
                            // the pure-scroll case genuinely degenerate again.
                            //
                            // EXCEPT a window sitting AT A PARK right now: an
                            // arriving tab whose batch also carries view
                            // travel lands here (its edge is empty — a hidden
                            // tab of an on-screen column parks without one —
                            // while viewDelta is set because it is not
                            // parked NOW). Differencing against the park
                            // builds an origin below the union of every
                            // output and the tab flies up the full screen:
                            // the exact bug the arrival branch below exists
                            // to prevent, resurfacing through this branch's
                            // priority. The park says nothing about motion,
                            // so the answer is the same degenerate leg,
                            // mirroring the sibling at the ARRIVING-from-park
                            // arm above.
                            if (atScrollPark(snap.window)) {
                                originOverride = QRectF(committedGeo);
                            } else if (viewLeavingToPark) {
                                // Edge-less LEAVING park (a hidden tab of an
                                // on-screen column, or a cross-axis stack
                                // overflow) riding a batch that also carries
                                // view travel. The difference-origin arm
                                // below would build a REAL leg from the live
                                // on-screen frame down to the park — the
                                // visible downward sweep. There is no edge to
                                // slide out by, so the answer is the same as
                                // the edge-less branch further down: commit
                                // without an animation.
                                skipScrollAnimation = true;
                            } else {
                                // viewDelta is a signed scalar ALONG this
                                // screen's strip axis, so the origin has to
                                // back out along that same axis. Subtracting
                                // it from x unconditionally would, on a
                                // vertical strip, displace the origin across
                                // the axis the paint offset moves along, and
                                // the leg would stop being degenerate: frame
                                // zero would land a full delta away in BOTH
                                // components instead of on the window.
                                const KWin::RectF cur = snap.window->frameGeometry();
                                const bool vertical =
                                    scrollAxisForScreen(snap.screenId) == PhosphorProtocol::ScrollAxis::Vertical;
                                const int originX = qRound(cur.x()) - (vertical ? 0 : snap.viewDelta);
                                const int originY = qRound(cur.y()) - (vertical ? snap.viewDelta : 0);
                                originOverride =
                                    QRectF(QRect(originX, originY, qRound(cur.width()), qRound(cur.height())));
                            }
                        }
                    } else if (!snap.scrollEdge.isEmpty()) {
                        const KWin::LogicalOutput* out = m_effect->outputForScreenId(snap.screenId);
                        const QRect screenRect = out ? QRect(out->geometry()) : QRect();
                        if (screenRect.isValid()) {
                            const bool arriving = screenRect.intersects(committedGeo);
                            // Arriving: start from the target's own row and
                            // size — the COMMITTED row and size, so a
                            // size-constrained window slides in as the frame
                            // it will actually be, at the height it will
                            // actually sit. Leaving: keep the rect the window
                            // occupies right now, so it slides out as itself
                            // rather than jumping to the park's row first.
                            QRect atEdge = committedGeo;
                            if (!arriving) {
                                const KWin::RectF cur = snap.window->frameGeometry();
                                atEdge =
                                    QRect(qRound(cur.x()), qRound(cur.y()), qRound(cur.width()), qRound(cur.height()));
                            }
                            // Edge semantics documented at anchorPastEdge.
                            atEdge = anchorPastEdge(atEdge, screenRect, snap.scrollEdge);
                            if (arriving) {
                                originOverride = QRectF(atEdge);
                            } else {
                                visualTargetOverride = QRectF(atEdge);
                            }
                        } else {
                            // No resolvable output (disconnect race): with no
                            // edge rect to anchor to, an animated apply would
                            // be the exact backwards park-sweep the overrides
                            // exist to prevent. Teleport instead.
                            qCDebug(lcEffect) << "scroll batch: no output for" << snap.screenId << "- teleporting"
                                              << snap.windowId << "to its target";
                            skipScrollAnimation = true;
                        }
                    } else if (!scrollTrackedScreenFor(snap.windowId).isEmpty()) {
                        // Edge-less commit of a scroll-tracked window. The
                        // engine clears the departure edge for exactly two
                        // parks, and this branch owns both directions of them.
                        // (Autotile batches never take it — their windows are
                        // not scroll-tracked and their targets are on-screen
                        // anyway.)
                        //
                        // LEAVING (target entirely off its own output): a
                        // vertical stack-overflow park, or a tab going hidden.
                        // There is no side to slide out by, and animating to a
                        // target below the union would sweep the window down
                        // the whole screen. Commit without an animation.
                        //
                        // ARRIVING (target on its output, window sitting at a
                        // park right now): the tab of an on-screen tabbed
                        // column being ACTIVATED, or a tile the layout pushed
                        // past the stack floor coming back. The park is below
                        // the union of every output and is chosen for safety,
                        // so it says nothing about where the window should
                        // appear to come from — animating from it flies the
                        // window up the full height of the screen. A tab switch
                        // has no motion to describe at all: the arriving tab
                        // occupies the rect the outgoing one just vacated, so
                        // it must appear IN PLACE. Degenerate leg, the same
                        // answer (and for the same reason) as the hasVisualPos
                        // branch above. It is also what makes the pair
                        // symmetric: the outgoing tab teleports away, so a
                        // travelling arrival had nothing to travel from.
                        const KWin::LogicalOutput* out = m_effect->outputForScreenId(snap.screenId);
                        const QRect screenRect = out ? QRect(out->geometry()) : QRect();
                        if (!screenRect.isValid()) {
                            // No resolvable output (disconnect race): both
                            // arms below need the rect, and falling through
                            // silently gave a scroll-tracked window a full
                            // animated leg from its live frame — the park,
                            // below the union — which is the exact sweep the
                            // arms exist to prevent. Teleport, mirroring the
                            // scrollEdge branch's identical case above.
                            qCDebug(lcEffect) << "scroll batch: no output for" << snap.screenId << "- teleporting"
                                              << snap.windowId << "to its target";
                            skipScrollAnimation = true;
                        } else if (!screenRect.intersects(committedGeo)) {
                            skipScrollAnimation = true;
                        } else if (atScrollPark(snap.window)) {
                            originOverride = QRectF(committedGeo);
                        }
                    }
                    // COLUMN-MAXIMIZE TOGGLE: depart from the rect the window
                    // held before the maximize, not from its live frame.
                    //
                    // By the time this runs the window has ALREADY been resized
                    // to KWin's maximize area, on both input paths: the titlebar
                    // button flips the bit itself before the effect sees the
                    // event, and the shortcut's own batch sets the bit in the
                    // maximize arm above. So the live frame is 3840x2068 on the
                    // way in and the target column rect on the way out, and a
                    // leg departing from it animates a 16px correction growing
                    // and NOTHING AT ALL shrinking — the whole visible motion is
                    // KWin's unanimated jump, with the leg twitching afterwards.
                    // Traced live: one leg per press, installing at
                    // (8,54 3840x2068) maximizing and at the target rect
                    // (8,54 1908x2052) restoring.
                    //
                    // m_preMaximizeFrame is the rect KWin captured at
                    // windowMaximizedStateAboutToChange, before any geometry
                    // change, which is the only place the departure rect still
                    // exists. Gated on THIS iteration having actually written
                    // KWin's maximize bit, which is what keeps a stale entry
                    // (the map is latest-wins and only swept on windowDeleted)
                    // from re-anchoring an unrelated later commit. A
                    // membership-vs-wire delta will not do: both release retain
                    // arms and the Apply arm's own fullscreen/gesture skip
                    // leave that delta true with no capture having been
                    // refreshed, so it latches on every following batch and
                    // anchors the leg at an arbitrarily old rect.
                    //
                    // Screen-gated like every sibling arm in this lambda. The
                    // override chain is reached by autotile batches too, and a
                    // window carrying retained column membership onto a tiling
                    // screen has no business anchoring off a scrolling
                    // maximize.
                    //
                    // Placed after the chain above so the park and view-travel
                    // arms keep priority — those describe where a window should
                    // appear to come from across the strip, which outranks the
                    // maximize's own departure rect.
                    //
                    // The monocle RELEASE takes the same arm on the same
                    // evidence: unmaximizeMonocleWindow's restore just moved
                    // the window off the maximize area, and the capture is the
                    // only place the maximized rect still exists. It is not
                    // screen-gated, because monocle is not a scrolling-only
                    // claim; the per-iteration flag is what keeps it fresh.
                    //
                    // The third owner of this leg, and the one that covers
                    // every screen: a maximize the USER took, marked at the
                    // windowMaximizedStateChanged hook before any of the paths
                    // that answer it diverge. The two terms beside it recognise
                    // only a maximize this batch AUTHORED — a scrolling
                    // screen's maximize-to-edges verb, or a monocle release —
                    // so on an autotile or unmanaged screen the press reached
                    // KWin directly, both were false, and the placement that
                    // followed took the snapIn leg and played whatever
                    // window.movement resolves instead of the pack the user
                    // assigned to window.movement.maximize.
                    //
                    // userMaximizeEdge is the marker, consumed once per window
                    // per batch above the monocle/else split rather than here:
                    // this line sits inside the redundant-apply skip, so taking
                    // it here left the entry armed on every batch a Wayland
                    // client answered by holding the zone it already had.
                    const bool maximizeLegThisBatch = (maximizeBitWrittenThisBatch && isScrollingScreen(snap.screenId))
                        || monocleBitReleased || userMaximizeEdge;
                    if (!originOverride.isValid() && maximizeLegThisBatch) {
                        const QRectF preMaximize = m_effect->m_shaderManager.preMaximizeFrame(snap.window);
                        if (preMaximize.isValid() && !preMaximize.isEmpty()) {
                            originOverride = preMaximize;
                        }
                    }
                    // Terminal backstop for every arm above, and for arms that
                    // do not exist yet: a commit whose target lies off the
                    // union of every output (a park) must NEVER take an
                    // animated leg unless a visualTargetOverride has
                    // redirected the visible motion on-screen. The chain
                    // above handles every KNOWN park shape, but its arms are
                    // gated on wire fields and tracking state
                    // (scrollTrackedScreenFor answers empty across a
                    // mode-flip or output-disconnect race, and
                    // setScrollingScreens does not bump the stagger
                    // generation), and an entry that falls through every gate
                    // used to reach applyWindowGeometry bare — a full
                    // animated leg from the live on-screen frame down to the
                    // park row. An ORIGIN override does not redeem the leg
                    // (its destination is still the park), so it is cleared
                    // rather than honoured.
                    if (!skipScrollAnimation && !visualTargetOverride.isValid() && rectAtScrollPark(committedGeo)) {
                        skipScrollAnimation = true;
                        originOverride = QRectF();
                    }
                    // A leg that this iteration MADE a maximize or a restore
                    // (column maximize-to-edges either way, monocle release),
                    // or that answers a maximize the user just took anywhere
                    // else, is the user's maximize event and rides
                    // window.movement.maximize, so the pack assigned there
                    // plays; every other placement is a snap.
                    //
                    // For the two the batch authored this apply is the single
                    // owner: the KWin-native handler in window_connections.cpp
                    // skips the column echo (interceptMaximizeRequest) and the
                    // X11 monocle echo (isSuppressingMaximizeChanged), and the
                    // Wayland monocle echo is absorbed by the same-effect
                    // short-circuit onto the leg installed here. For the
                    // marker-driven case the native handler DOES install first
                    // — that is the whole point of it, an unintercepted
                    // maximize is its event — and the same short-circuit
                    // absorbs this one onto it: same path, same resolved pack,
                    // so the leg is kept and only its endpoints are
                    // re-asserted. What the marker fixes is the leg this apply
                    // would otherwise have installed, which was snapIn, and
                    // which SUPERSEDED the native maximize leg because it
                    // resolved to a different pack.
                    m_effect->applyWindowGeometry(snap.window, geo, /*allowDuringDrag=*/false, skipScrollAnimation,
                                                  maximizeLegThisBatch ? PhosphorAnimation::ProfilePaths::WindowMaximize
                                                                       : PhosphorAnimation::ProfilePaths::WindowSnapIn,
                                                  originOverride, visualTargetOverride);
                }
            }

            // Tab swap: cross-fade the outgoing tab into this one.
            //
            // At the monocle/else JOIN, decoupled on purpose from the two arms
            // it used to sit inside: the Wayland already-centred short-circuit
            // (skipMoveResize) says the GEOMETRY needs no commit, which says
            // nothing about whether the CONTENTS just swapped, and burying the
            // install there silently suppressed the cross-fade for a swap
            // inside an already-parked column. The monocle case is refused
            // explicitly below rather than structurally, so it is diagnosable.
            //
            // A SEPARATE leg from the geometry apply above, not a shader hung
            // off it: the apply's leg is deliberately degenerate (the tab
            // appears in place) and a degenerate leg starts no animation, so
            // applyWindowGeometry installs no shader at all. The cross-fade is
            // not describing motion anyway — it is a discrete, time-driven
            // content change over a rect that does not move, which is exactly
            // what tryBeginShaderForEvent drives. Installed on the ARRIVING
            // tab; the outgoing one is parked off-canvas by the end of this
            // batch and painted by nothing.
            if (!snap.tabFrom.isEmpty()) {
                if (snap.isMonocle) {
                    // No producer emits the pair today (monocle comes from
                    // autotile, tabFrom from the scroll engine), but this
                    // file's own mode-flip reasoning declines to call the two
                    // provably disjoint — so the drop is logged rather than
                    // structural-and-silent.
                    qCDebug(lcEffect) << "tabSwap install: dropping monocle+tabFrom pair for" << snap.windowId;
                } else if (snap.window->isUserMove() || snap.window->isUserResize()) {
                    // Mid-drag, the geometry apply above DEFERRED its commit
                    // to windowFinishUserMovedResized — installing the swap
                    // now would play the cross-fade under the pointer against
                    // a window still at its drag rect, and the seed's
                    // identity map would miss (the frame is not at the
                    // column). The deferred replay deliberately does not
                    // re-install: by drag end the outgoing tab is parked and
                    // re-folded, so the honest outcome is no cross-fade —
                    // the same predicate the commanded-rect arm below uses.
                    qCDebug(lcEffect) << "tabSwap install: skipped mid-drag for" << snap.windowId;
                } else {
                    // tabFrom is a WIRE id; every entry of this batch had its
                    // own id re-keyed to the live one, and the outgoing tab
                    // is by construction a sibling entry — so translate
                    // through the batch's own wire→live map first. Exact
                    // lookup after that, never the fuzzy appId fallback: this
                    // is a paint hint, and a fuzzy match on a stale id would
                    // cross-fade a same-app window that has nothing to do
                    // with the swap. Unresolvable (closed between the emit
                    // and here) simply means no cross-fade. Not defended
                    // against: a tabFrom naming some OTHER window of this
                    // batch that is arriving on screen rather than parking.
                    // The engine cannot emit that shape (the pair is derived
                    // from one column's hidden-flag flip), and a garbled hint
                    // costs one wrong-source cross-fade, not a wrong
                    // placement.
                    const QString outgoingId = wireToLive.value(snap.tabFrom, snap.tabFrom);
                    KWin::EffectWindow* const outgoing = m_effect->findWindowByIdExact(outgoingId);
                    // Chain trace, DEBUG level (enable plasmazones.effect):
                    // every link here fails SILENTLY on screen — the pack
                    // still runs, it just cross-fades the arriving tab
                    // against itself — so the trace is the only way to tell
                    // which link broke.
                    qCDebug(lcEffect) << "tabSwap install:" << snap.windowId << "replacing" << outgoingId << "resolved"
                                      << (outgoing ? "yes" : "NO");
                    if (outgoing && outgoing != snap.window && !outgoing->isDeleted()) {
                        bool ownsLeg = false;
                        m_effect->tryBeginShaderForEvent(
                            snap.window, PhosphorAnimation::ProfilePaths::ScrollingTabSwitch,
                            m_effect->animationDurationMs(), /*reverse=*/false, /*holdCloseGrab=*/false,
                            /*holdAddedGrab=*/false, /*animateMinimized=*/false, &ownsLeg);
                        // ownsLeg, not liveness: the resolve installs nothing
                        // when the user picked "None", and findTransition
                        // would then hand back whatever unrelated leg is live
                        // and re-point ITS snapshot at a foreign window. For
                        // THIS path ownsLeg also means FRESH: the tab install
                        // defeats the same-effect short-circuit inside
                        // tryBeginShaderForEvent (each swap supersedes a
                        // same-pack leg — repeat switches and a focus leg
                        // running the same pack both got stale snapshots and
                        // a mid-progress clock before that).
                        if (ownsLeg) {
                            auto* st = m_effect->m_shaderManager.findTransition(snap.window);
                            const bool hasCached = st && st->cached;
                            if (st) {
                                // Marks the leg as the swap so the activation
                                // this very switch causes does not install a
                                // focus leg over it — see
                                // ShaderTransition::tabSwap. Set BEFORE the
                                // snapshot request and outside its uOldWindow
                                // gate: a pack that ignores the outgoing tab
                                // still owns the swap's slot.
                                st->tabSwap = true;
                                // The capture is gated on the compiled pack
                                // LINKING uOldWindow, like the drag-snap and
                                // held-move requests. Seeded NOW,
                                // synchronously: by the leg's first paint
                                // frame the outgoing tab has been re-folded
                                // at its park with the park's unwritten
                                // backdrop baked into any frost pane (the
                                // "goes opaque, then animates" artifact seen
                                // live); inside this batch slot the composite
                                // still holds the pre-switch column fold. See
                                // seedTabSwapSnapshot.
                                qCDebug(lcEffect) << "tabSwap leg installed, cached" << (hasCached ? "yes" : "NO")
                                                  << "uOldWindow linked" << (hasCached ? st->cached->iOldWindowLoc : -1)
                                                  << "existing snapshot" << (st->oldSnapshot ? "yes" : "no");
                                if (hasCached && st->cached->iOldWindowLoc >= 0 && !st->oldSnapshot) {
                                    m_effect->seedTabSwapSnapshot(*st, outgoing, snap.window);
                                }
                            }
                        }
                    }
                }
            }

            // The relocation hint is a translation FROM the batch's park rect,
            // so it only means anything once that park rect is what the window
            // is committed at. applyWindowGeometry's non-member fullscreen bail
            // commits nothing at all: a window the client fullscreened itself
            // (F11) keeps being tiled by the strip — the daemon never untiles on
            // fullscreen, and the next batch re-marks its tiled membership — so
            // it still receives park entries, but its committed frame is the
            // full output. Adding a park-to-strip translation (whose y reaches
            // below the union of every output) on top of THAT drags the
            // fullscreen surface off the viewport. Drop the entry instead, so
            // the window simply paints where it is committed.
            //
            // Deliberately NOT extended to the user-move/user-resize defer arm
            // that commitDeferredOrBailed also covers below: the relocation is
            // already switched off for the whole drag (scrollManagedOutputFor
            // rejects a dragged window), and the deferred replay commits the
            // park rect afterwards, so the entry is still correct when it is
            // next read. Dropping it there would lose the park with nothing
            // guaranteed to restore it.
            // The fullscreen-bail visual-delta removal now happens in the
            // visual-delta block itself (fullscreenBailSkippedCommit selects
            // the remove arm there, computed above it), so the entry
            // converges on stable absence with one damage on the transition
            // batch instead of an insert/remove repaint pair per batch.

            // A strip entry never takes the reactive centring pass, whatever
            // KIND of entry it is. The split below only reaches its removal arm
            // for a non-monocle Wayland entry, so a MONOCLE batch on a
            // scrolling screen — which the sibling block below documents as
            // routine during a mode flip, while m_scrollingScreens arrives on
            // its own D-Bus signal — matches neither arm and keeps whatever its
            // earlier autotile placement armed. Shedding both maps here, before
            // the split, covers every kind.
            //
            // m_centeredWaylandZones goes with m_tileTargetZones: the same
            // autotile placement arms both, and it is the map the
            // skipMoveResize short-circuit in the apply above actually reads,
            // so a survivor there silently downgrades a strip resize to a move.
            //
            // Disjoint from the write arm below (that one runs only for a
            // NON-scrolling screen), so this cannot delete an entry the
            // autotile arm is about to install.
            if (isScrollingScreen(snap.screenId)) {
                m_tileTargetZones.remove(snap.windowId);
                m_centeredWaylandZones.remove(snap.windowId);
            }

            if (!snap.isMonocle && snap.window->isWaylandClient()) {
                // windowedFullscreen is excluded like monocle: KWin owns the
                // committed frame during the fullscreen round-trip, and a
                // stale centering target consumed against the ack's frame
                // change would raw-moveResize the window to the column origin
                // at full-output size, bypassing the fullscreen bail and
                // reaping the animation. (The ack branch in
                // slotWindowFullScreenChanged re-commits the column rect.)
                //
                // ORDER NOTE: this write lands AFTER the apply above, so the
                // synchronous frameGeometryChanged that apply emits re-enters
                // slotWindowFrameGeometryChanged while this map still holds the
                // PREVIOUS batch's zone for this window. The reactive centring
                // block there now sits behind the inGeometryApply gate, so the
                // mid-apply re-entry returns before reaching the map; a
                // surviving previous-batch entry is overwritten by the
                // per-entry write below in the same pass, and is otherwise
                // consumed only by the next out-of-bracket frame change, where
                // the window is normally already at that rect and the
                // near-zero delta arm consumes it harmlessly. Moving the
                // write above the apply would close the window entirely; it is
                // left here because the reactive centring is deliberately
                // re-entrant-driven and the reordering has not been exercised.
                if (isScrollingScreen(snap.screenId)) {
                    // A COLUMN-MAXIMIZED Wayland window arms the counter-assert,
                    // which is otherwise X11-only. The exclusion below is about
                    // the X11 SELF-mover — a client repositioning itself through
                    // ConfigureRequests, which no Wayland client can do — but
                    // the maximize mirror introduces a second external mover
                    // that exists on both platforms: KWin re-runs its
                    // maximize-area placement for a MaximizeFull window and
                    // overwrites the column rect with the ungapped work area.
                    // Observed live on a cross-output move: the window sat at
                    // the full output while its column stayed gapped, until the
                    // next batch happened to move a rect.
                    //
                    // The COMMANDED rect is recorded here rather than the
                    // committed frame the X11 leg uses, because a Wayland
                    // commit lands a round trip later and recording the frame
                    // now would capture the pre-commit rect. That substitution
                    // is safe because the rect recorded is the one this
                    // entry actually DELIVERED, captured after the
                    // size-continuity pass. For a maximized column that is the
                    // column rect unchanged: the pass EXCLUDES the three
                    // declared-rect states, so no centred commit can reach
                    // this record and the commanded rect is what the client
                    // was offered. The exclusion is what makes the two agree.
                    // Without it the pass would hand a client's own smaller
                    // answer forward, and the counter-assert would compare the
                    // centred commit against a rect that was never offered.
                    // Not armed mid-gesture, matching the X11 leg's own
                    // deferred-commit predicate: countering a live drag or
                    // resize would fight the user's hand.
                    // fullscreenBailSkippedCommit is the third term for the same
                    // reason the X11 leg and the sibling offered-column write
                    // both carry it: applyWindowGeometry's non-member fullscreen
                    // bail commits NOTHING for this entry, so recording a rect
                    // as commanded would arm the counter-assert against a rect
                    // that was never offered, and it would yank a self-
                    // fullscreened client back into its column repeatedly.
                    // The burst budget caps that at 3 per rolling second
                    // BETWEEN batches only — every fresh batch command re-arms
                    // the pair, so a client that keeps provoking batches is not
                    // bounded at 3/s overall.
                    if (snap.isMaximizedToEdges && !snap.window->isUserMove() && !snap.window->isUserResize()
                        && !fullscreenBailSkippedCommit && !scrollDeliveredRect.isNull()) {
                        m_effect->m_scrollCommandedRects.insert(snap.windowId, {scrollDeliveredRect, 0, 0});
                    } else {
                        // DISARM, the way the X11 and monocle arms both do.
                        // This was the only one of the three that armed
                        // without ever removing, and the entry it left is not
                        // inert: externallyMovable reads ledger membership for
                        // a Wayland window, and membership legitimately
                        // outlives the maximize on the retain-on-fullscreen
                        // arm — so a window that left fullscreen without
                        // floating kept a stale full-column rect armed and got
                        // yanked back to it on any frame change outside an
                        // apply. Dropping the entry disarms the counter until
                        // the next batch commands something.
                        m_effect->m_scrollCommandedRects.remove(snap.windowId);
                    }
                    // A STRIP entry never takes the reactive centring pass.
                    //
                    // That pass is an autotile repair: it re-centres a Wayland
                    // client whose committed frame is smaller than its zone,
                    // and then CLAMPS the result fully inside the output
                    // containing the zone's centre. Both halves are wrong on a
                    // strip. A strip column is routinely meant to sit off the
                    // viewport — parked below every output, or straddling a
                    // screen edge so the effect can crop it — and clamping it
                    // back on screen is exactly the "window slides around the
                    // edge instead of being cropped at it" symptom. It also
                    // reaps the window's animation leg mid-flight and issues
                    // its own moveResize, so the strip's placement and this
                    // pass fight each other every frame.
                    //
                    // Only ever observable for a client whose commit diverges
                    // from its column, because the pass no-ops when the frame
                    // already fills the zone. A window that accepts its column
                    // — nearly all of them — was never touched, which is why
                    // this went unnoticed.
                    //
                    // Removed rather than merely not written: a window that
                    // moves from an autotile screen to a scrolling one would
                    // otherwise keep the entry its autotile placement armed.
                    // The removal itself now happens before this split, so it
                    // also covers the monocle and windowed-fullscreen kinds
                    // that never reach here.
                } else if (!snap.isWindowedFullscreen) {
                    m_tileTargetZones[snap.windowId] = snap.geometry;
                }
            } else if (!snap.isMonocle && isScrollingScreen(snap.screenId)) {
                // X11 leg of the reactive repair, deliberately NOT the
                // Wayland centering machinery (which fights X11's synchronous
                // configures — the ballooning hazard above): remember the
                // rect the apply actually COMMITTED (not the requested batch
                // rect) so slotWindowFrameGeometryChanged can counter an
                // EXTERNAL move. The committed frame differs for
                // size-increment clients (terminals), where applyWindowGeometry
                // constrains and re-centres — storing the request would make
                // the counter-assert misread every later frame event as an
                // external move and burn its burst budget on no-ops forever.
                //
                // ONLY on the paths where the apply above committed
                // synchronously: a mid-user-move apply DEFERS its commit to
                // windowFinishUserMovedResized, and the non-member fullscreen
                // bail commits nothing — capturing frameGeometry there would
                // record the drag-time or pre-apply frame as "commanded" and
                // have the counter yank the window back to it. Dropping the
                // entry instead disarms the counter until the next batch.
                // A fresh command resets the counter-assert burst budget.
                const bool commitDeferredOrBailed =
                    snap.window->isUserMove() || snap.window->isUserResize() || fullscreenBailSkippedCommit;
                if (commitDeferredOrBailed) {
                    m_effect->m_scrollCommandedRects.remove(snap.windowId);
                    // Defensive on this arm, not load-bearing: the offer is only
                    // ever written for a Wayland client, and this is the X11
                    // leg, so the entry is normally absent. Kept because the
                    // Wayland arm now applies this same predicate at its own
                    // record site, and the two should stay legible as one rule;
                    // removing an absent key costs nothing.
                    m_effect->m_scrollOfferedColumn.remove(snap.windowId);
                } else {
                    m_effect->m_scrollCommandedRects.insert(snap.windowId,
                                                            {snap.window->frameGeometry().toRect(), 0, 0});
                }
            } else if (snap.isMonocle) {
                // A monocle window is never counter-asserted — neither arm
                // above records a commanded rect for it — so a surviving
                // entry from an earlier scrolling batch would keep the
                // counter armed against a rect the strip no longer owns,
                // yanking the maximized window back (3 per rolling second
                // between batches, and re-armed by each fresh command, so not
                // bounded at 3/s across a batch storm). Mirrors the
                // commitDeferredOrBailed remove above: dropping the entry is
                // how this pipeline disarms the counter.
                //
                // Reachable despite monocle and scrolling looking disjoint:
                // m_scrollingScreens arrives on its own D-Bus signal,
                // independent of the batch, so during a mode flip a screen
                // reads scrolling for a few ticks while monocle batches are
                // still in flight.
                m_effect->m_scrollCommandedRects.remove(snap.windowId);
                m_effect->m_scrollOfferedColumn.remove(snap.windowId);
            }
        },
        onComplete, startedViewLegs || anyTabSwap || !immediateViewScreens.isEmpty());
}

void TilingHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    Q_UNUSED(oldGeometry)
    // isDeleted: every other entry point bails on a corpse BEFORE the id
    // lookup, explicitly to avoid re-polluting the scrubbed id caches; this
    // slot sees strictly more geometry changes since the counter-assert
    // widened its fast bail.
    if (!w || w->isDeleted()) {
        return;
    }

    // Fast bail: skip getWindowId entirely when no consumer below needs it
    if (m_effect->m_virtualScreenDefs.isEmpty() && m_tileTargetZones.isEmpty()
        && m_effect->m_scrollCommandedRects.isEmpty()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Counter-assert for a scroll-managed window an EXTERNAL mover relocated.
    // X11 clients for the self-mover reason below; column-maximize members on
    // either platform, because KWin's own maximize-area re-assert is an
    // external mover the self-mover reasoning does not cover.
    // Any frame change landing here outside our own apply
    // bracket was not ours: X11 clients can reposition themselves through
    // ConfigureRequests KWin honors, and a Wine game re-asserting its
    // saved window position was seen live pulling its frame back on-screen
    // out of the strip's park and straddle placements — sitting over its
    // neighbour's column until the next user scroll, because the engine's
    // emit-on-change gate had nothing to say. Re-apply the commanded rect,
    // without animation (this is enforcement, not motion). The counter is
    // RATE-LIMITED to 3 per rolling second (the window resets once a second
    // elapses since the burst started, and every fresh batch command
    // re-arms it) — a client that re-asserts on every configure gets
    // countered at most 3 times per rolling second PER COMMAND, so it does
    // not win outright. Note the budget is re-armed by every fresh batch
    // command (the insert resets the pair, deliberately), so on a scrolling
    // strip the effective ceiling is three per batch rather than three per
    // second.
    //
    // User-move/resize terms: DragTracker never tracks an interactive
    // RESIZE at all (its start handler bails on isUserResize), and a mouse
    // drag stays live past forceEnd until all buttons release — in both
    // gaps isDragging() is false while the user is actively manipulating
    // the frame, and the counter would fight the user's own gesture.
    // The commandedRect entry survives a resize that STARTS after the
    // batch (the per-batch disarm only covers one already in flight).
    //
    // Screen gate through scrollTrackedScreenFor, not the raw notified map:
    // the apply loop marks tiled unconditionally but records the screen
    // only for notified windows, so a demoted/rolled-back window is a
    // tiled member with no recorded screen — the helper resolves that
    // (fail-closed either way; the helper just fails closed for the right
    // set).
    // Wayland is excluded for the SELF-mover reason above, with one exception:
    // a column-maximize member has an external mover that exists on both
    // platforms — KWin's own maximize-area re-assert. That population arms a
    // commanded rect at the apply site for exactly this, and nothing else
    // reaches it, so the general exclusion and its reasoning stand.
    // A maximized-to-edges member whose maximize bit is mid-transition is exempt
    // on EVERY platform, XWayland included, and that exemption is what keeps
    // the counter off the user's own restore.
    //
    // KWin emits frameGeometryChanged from INSIDE maximize(), before
    // maximizedChanged, so on a restore click this slot runs first: the frame
    // has already moved to the restore rect while m_scrollCommandedRects still
    // holds the maximized column's rect, and the interception has not yet run,
    // let alone dispatched the toggle the engine will answer with a narrower
    // batch. Unqualified, the counter reads that as an external mover and
    // shoves the window back to full width, which the arriving batch then
    // undoes — a full-width bounce on every restore, seen live at 14ms.
    //
    // Nothing is given up. The mover this arm was added for is KWin's own
    // maximize-area re-assert, which by definition happens while the window IS
    // maximized, so it still passes. What no longer passes is a frame change
    // that arrives with the bit already gone, which is never that mover: it is
    // a maximize/restore transition, and the commanded rect describing the
    // layout the engine is in the middle of replacing has no authority over
    // it. Enforcement resumes as soon as the batch re-arms the entry.
    // The transition test gates the WHOLE predicate, not just the Wayland
    // member arm. XWayland windows are handled inside this Wayland session and
    // report isWaylandClient() false, so a member arm qualified only on the
    // right of the || is unreachable for them: the first disjunct
    // short-circuits true and the restore bounce described above survives
    // untouched on every XWayland scroll-managed window. Hoisting it keeps the
    // X11 arm intact for the mover it exists for (a client moving ITSELF, the
    // Wine case) while exempting the one frame change that is never an
    // external mover on either platform.
    KWin::Window* kwCounter = w->window();
    const bool inMaximizeTransition = kwCounter && m_maximizedToEdgesWindows.contains(windowId)
        && kwCounter->requestedMaximizeMode() != KWin::MaximizeFull;
    const bool externallyMovable = !inMaximizeTransition
        && (!w->isWaylandClient()
            || (kwCounter && m_maximizedToEdgesWindows.contains(windowId)
                && kwCounter->requestedMaximizeMode() == KWin::MaximizeFull));
    if (externallyMovable && !m_effect->m_daemonGate.inGeometryApply && !w->isUserMove() && !w->isUserResize()) {
        const auto cit = m_effect->m_scrollCommandedRects.find(windowId);
        if (cit != m_effect->m_scrollCommandedRects.end() && isScrollingScreen(scrollTrackedScreenFor(windowId))
            && !(m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
                 && windowId == m_effect->m_dragTracker->draggedWindowId())) {
            const QRect actual = w->frameGeometry().toRect();
            {
                // Budget arithmetic is pure and unit-tested
                // (scrolldecisions.h, test_scroll_decisions).
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (ScrollDecisions::shouldCounterAssert(cit->burstStartMs, cit->burstCount, nowMs,
                                                         actual != cit->rect)) {
                    // Copy out of the hash node first: the apply below emits
                    // frameGeometryChanged synchronously on X11 and re-enters
                    // this slot — holding a reference into the node across
                    // re-entrant code is undefined the day anything mutates
                    // the map from inside that window.
                    const QRect commanded = cit->rect;
                    qCInfo(lcEffect) << "Countering external move of scroll-managed window" << windowId << "from"
                                     << actual << "back to" << commanded;
                    // Bracketed like every sibling moveResize site: the
                    // synchronous re-entry must not fall through to the
                    // VS-crossing detector for a move the effect itself made.
                    // Routed through applyWindowGeometry ON PURPOSE, unlike
                    // the fullscreen-ack re-commit (signals.cpp): its
                    // already-at-target skip cannot fire here (this slot runs
                    // because the frame DIFFERS from the commanded rect), and
                    // its user-move defer is wanted — countering a live mouse
                    // drag would fight the user, and the drag machinery owns
                    // the re-insert; a superseding batch mooting the deferred
                    // replay is the correct outcome, not a drop.
                    // Save/restore, not set/clear (nesting-safe), through
                    // qScopeGuard like the four siblings in this file and
                    // signals.cpp. Hand-restoring was correct as written —
                    // applyWindowGeometry does not throw and the return is
                    // immediate — but it is the one site where an early exit
                    // added between the call and the restore would strand the
                    // flag set for the rest of the frame, silently muting the
                    // VS-crossing detector.
                    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
                    m_effect->m_daemonGate.inGeometryApply = true;
                    const auto counterGuard = qScopeGuard([this, prevInApply] {
                        m_effect->m_daemonGate.inGeometryApply = prevInApply;
                    });
                    m_effect->applyWindowGeometry(w, commanded, /*allowDuringDrag=*/false, /*skipAnimation=*/true);
                    return;
                }
            }
        }
    }

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
        && m_effect->m_daemonGate.virtualScreensReady && !m_effect->m_daemonGate.inGeometryApply) {
        // Don't detect VS crossings for the dragged window — the drop handler
        // (callDragStopped / autotile drag end) owns state transitions.
        // Detecting mid-drag would transfer the window before the user drops it.
        // Other windows (e.g., a terminal reflowing) should still get VS crossing checks.
        const bool isDraggedWindow = m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
            && windowId == m_effect->m_dragTracker->draggedWindowId();
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

    // Everything from here down is the reactive centring pass, and it is
    // WAYLAND-ONLY despite reading engine-general: m_tileTargetZones has
    // exactly one writer (the batch apply in this file), and that write sits
    // inside an `isWaylandClient()` arm. An X11 client never reaches this
    // block — constrainTileGeometry pre-centres its frame inside the zone
    // before the apply commits, and an external mover is dealt with by the
    // counter-assert above, not here.
    if (m_tileTargetZones.isEmpty()) {
        return;
    }

    // Never centre from inside the effect's own apply bracket. This is the
    // same gate the two other consumers in this slot take (the counter-assert
    // and the virtual-screen crossing check above), and its absence here was
    // a defect on its own.
    //
    // What it costs us without the gate: `applyWindowGeometry` commits at the
    // top of the batch loop but the target zone is not recorded until the end
    // of it, so a batch cannot trip its own apply — the map is still empty
    // when its moveResize lands. A SECOND batch carrying the same zone can,
    // and the autotile engine emits exactly that on a removal, which it
    // retiles immediately and uncoalesced (AutotileEngine::onWindowRemoved).
    // The first batch's entry is live while the second one applies, so the
    // frame change KWin emits mid-apply — position taken, size still awaiting
    // the client's ack — reaches the pass with the PRE-resize frame. The pass
    // reads that stale size as "the client refused to fill its zone" and
    // issues a competing moveResize at the old size, which supersedes the
    // enlarge the client was still answering. The window is then pinned a
    // zone-width short for the rest of its life, because the centring stamps
    // m_centeredWaylandZones and the redundant-apply skip in
    // slotWindowsTileRequested honours that entry on every later batch.
    //
    // Discussion #1028: a window count dropping 2 -> 1 hands the survivor the
    // ungapped full-screen zone, and losing that enlarge leaves it centred
    // with a dead band down each side that belongs to no tiled window — which
    // is where focus-follows-mouse then finds nothing to focus.
    if (m_effect->m_daemonGate.inGeometryApply) {
        return;
    }

    auto it = m_tileTargetZones.find(windowId);
    if (it == m_tileTargetZones.end()) {
        return;
    }

    const QRect& targetZone = it.value();
    const QRectF actual = w->frameGeometry();

    // Never centre while a resize configure is still in flight. KWin
    // reconciles moveResizeGeometry to the client's COMMITTED size (a
    // smaller-than-requested commit included) before it emits
    // frameGeometryChanged — XdgSurfaceWindow::handleNextWindowGeometry
    // calls maybeUpdateMoveResizeGeometry, then updateGeometry, which emits
    // — so at rest the two sizes agree and a genuinely-refusing client
    // passes this on the very commit that refused. The sizes diverge only
    // while an unacknowledged resize configure is pending, and a frame event
    // that arrives THEN (a move applied synchronously, an earlier commit)
    // still carries the pre-resize size. Centring on it would issue a
    // competing moveResize at that stale size and supersede the resize the
    // client is still answering — the enlarge-loss above, minus the second
    // batch: this closes the same race for a single batch against a slow
    // client, which the inGeometryApply gate cannot see.
    //
    // Skipping does NOT consume the entry: the commit that acknowledges the
    // configure re-fires this slot, agrees with moveResizeGeometry, and the
    // pass runs then. Sizes only — positions legitimately diverge mid-move —
    // and with a tolerance, because moveResizeGeometry holds the requested
    // fractional rect while the frame is snapped to pixels.
    if (KWin::Window* kwPending = w->window()) {
        const QSizeF commanded = QRectF(kwPending->moveResizeGeometry()).size();
        if (qAbs(commanded.width() - actual.width()) > 1.0 || qAbs(commanded.height() - actual.height()) > 1.0) {
            qCDebug(lcEffect) << "Autotile centering: configure in flight for" << windowId << "commanded=" << commanded
                              << "actual=" << actual.size() << "- waiting";
            return;
        }
    }

    constexpr qreal MinCenteringDelta = 3.0;

    const qreal dw = targetZone.width() - actual.width();
    const qreal dh = targetZone.height() - actual.height();

    // Window fills the zone (or close enough) — no centering needed; consume entry
    if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
        qCDebug(lcEffect) << "Autotile centering: matched" << windowId << "dw=" << dw << "dh=" << dh;
        m_tileTargetZones.erase(it);
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
    if (auto* output = KWin::effects ? KWin::effects->screenAt(targetZone.center()) : nullptr) {
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
        m_tileTargetZones.erase(it);
        return;
    }

    KWin::Window* kw = w->window();
    if (!kw) {
        // No KWin::Window — consume stale entry to prevent perpetual lookups
        m_tileTargetZones.erase(it);
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
    m_tileTargetZones.erase(it);
    m_effect->m_windowAnimator->removeAnimation(w);
    kw->moveResize(centered);
}

void TilingHandler::slotFocusWindowRequested(const QString& windowId)
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

    // Engine-driven activation on a scrolling screen usually means the
    // strip just scrolled — pause FFM until the cursor moves deliberately,
    // or a pointer twitch immediately re-focuses whatever column slid
    // under it and undoes this activation.
    if (isScrollingScreen(m_effect->getWindowScreenId(w))) {
        suppressFfmUntilCursorMoves();
    }
    // Re-key to the EFFECT's id for the window we actually resolved, not the
    // daemon's spelling. findWindowById carries a fuzzy same-app fallback, so
    // for a stale pre-restore UUID the two can differ — and the only consumer
    // (the restack arm of a later batch) resolves with findWindowByIdExact,
    // which would then miss and silently skip the raise. Every other resolve
    // site in this file re-keys for the same reason.
    m_pendingAutotileFocusWindowId = m_effect->getWindowId(w);
    if (KWin::effects) {
        KWin::effects->activateWindow(w);
    }
}

void TilingHandler::reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight)
{
    if (minWidth <= 0 && minHeight <= 0) {
        return;
    }

    qCInfo(lcEffect) << "Discovered min size for" << windowId << ":" << minWidth << "x" << minHeight
                     << "- reporting to daemon for future retiles";

    // This is a SECOND writer of windowMinSizeUpdated carrying a per-axis
    // pair with 0 in the axis that did not shrink, and the daemon's store
    // replaces the whole QSize — so a (900, 0) discovery clears a stored
    // height minimum. Evict the last-reported cache rather than recording
    // the half-pair as sent: the next batch's change poll then re-asserts
    // the true declared pair instead of being silenced by its own cache.
    m_effect->m_lastReportedMinSize.remove(windowId);

    // Gate like every other fireAndForget in this handler: with no daemon
    // registered the call only queues a D-Bus error, and the eviction above
    // already ensures the discovery is re-reported after bring-up.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }

    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowMinSizeUpdated"),
        {windowId, minWidth, minHeight}, QStringLiteral("windowMinSizeUpdated"));
}

} // namespace PlasmaZones
