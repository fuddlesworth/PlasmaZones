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
#include "handlers/dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"
#include "transitions/striptransitionmanager.h"

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

    // A geometry batch on a scrolling screen slides columns under the
    // stationary pointer; pause FFM until the cursor moves deliberately
    // (see suppressFfmUntilCursorMoves) so the next pointer twitch cannot
    // steal focus onto whatever landed under it.
    for (const auto& req : validatedRequests) {
        if (isScrollingScreen(req.screenId)) {
            suppressFfmUntilCursorMoves();
            break;
        }
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
        bool isWindowedFullscreen =
            false; ///< scrolling windowed fullscreen: hold KWin fullscreen state at the column rect
        QString screenId; ///< daemon's TARGET screen for this window (req.screenId)
        QString stacking; ///< overlap z-order policy ("firstOnTop"/"lastOnTop"), empty for non-overlap layouts
        QString scrollEdge; ///< scrolling strip: screen edge to animate from ("left"/"right"), else empty
        int viewDeltaX = 0; ///< scrolling strip: how far the view slid, 0 when this window is not carried by it
        QPoint visualPos; ///< scrolling strip: where a PARKED column really sits, to paint at instead of the commit
        bool hasVisualPos = false;
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
            // Scan all screen buckets (all-bucket reader policy — a VS
            // config change can re-key the window's screen without moving
            // its geometry bucket). Exact resolve, matching the tile lambda's
            // deliberate policy: a fuzzy hit would teleport a same-app
            // SIBLING onto this window's restored rect.
            KWin::EffectWindow* floatWin = m_effect->findWindowByIdExact(floatWindowId);
            if (floatWin) {
                if (const QRectF savedGeo = findPreTileGeometry(floatWindowId); savedGeo.isValid()) {
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
        entry.viewDeltaX =
            qBound(-StripViewAnimator::kMaxViewDeltaPx, req.viewDeltaX, StripViewAnimator::kMaxViewDeltaPx);
        entry.visualPos = req.hasVisualPos ? QPoint(req.visualX, req.visualY) : QPoint();
        entry.hasVisualPos = req.hasVisualPos;
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
        bool isWindowedFullscreen = false;
        QString stacking;
        QString scrollEdge;
        int viewDeltaX = 0;
        QPoint visualPos;
        bool hasVisualPos = false;
    };
    QVector<TileSnap> toApply;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        // Re-key to the RESOLVED window's live id. The disambiguation above
        // can match a candidate whose uuid differs from the daemon-supplied
        // entry id (stale across a KWin restart), and every write this batch
        // performs — tiled tracking, the Wayland centering cache, the
        // pre-autotile capture — must key on the live id the readers use, or
        // the tiling goes untracked and the stale-keyed entries are never
        // reclaimed.
        e.windowId = m_effect->getWindowId(e.window);
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
                        e.isWindowedFullscreen, e.stacking, e.scrollEdge, e.viewDeltaX, e.visualPos, e.hasVisualPos});
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
    // origin branch of the apply lambda treats an entry's viewDeltaX as
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
        for (const TileSnap& s : toApply) {
            if (s.viewDeltaX == 0 || s.screenId.isEmpty() || seededScreens.contains(s.screenId)) {
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
                    if (m_effect->m_stripViewAnimator->isAnimatingOn(out)) {
                        startedViewScreens.insert(s.screenId);
                    }
                    continue;
                }
                seededOutputs.insert(out);
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
                // already live, so this ordering is load-bearing (see its
                // header contract).
                m_effect->m_stripTransition.notifyLeg(out, stripEffectId, stripEffectParams, s.viewDeltaX);
                if (m_effect->m_stripViewAnimator->applyBatchDelta(out, s.viewDeltaX, viewProfile)) {
                    startedViewScreens.insert(s.screenId);
                } else {
                    // The spring declined (animations off, no clock): there
                    // is no leg for the pass to decorate and no offset for
                    // a residual origin to lean on — disarm the pass and
                    // let this screen's entries take the ordinary paths.
                    m_effect->m_stripTransition.notifyLeg(out, QString(), QVariantMap(), 0);
                }
            }
        }
    }
    const bool startedViewLegs = !startedViewScreens.isEmpty();

    // Cascade order follows the direction of travel for a scrolling strip.
    //
    // applyStaggeredOrImmediate below delays entry i by i * interval, so the
    // ORDER of toApply is the order the user watches windows move in. The
    // batch arrives in strip order (left to right) whichever way the strip
    // scrolled, so the cascade ran left-to-right both ways and the two
    // directions did not mirror each other — scrolling one way looked like it
    // led with the near edge, the other like it led with the far one.
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
    const bool isScrollBatch = !startedViewLegs && std::any_of(toApply.cbegin(), toApply.cend(), [](const TileSnap& s) {
        return !s.scrollEdge.isEmpty();
    });
    if (isScrollBatch) {
        QHash<QString, QRect> screenRectCache;
        const auto screenRectFor = [&](const TileSnap& s) -> const QRect& {
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
        // An UNRESOLVED output does not read as arriving: the committed rect
        // of a leaving column is the park, and sorting on it would feed the
        // cascade a coordinate unrelated to what the user watches. The
        // current frame is the visible truth either way.
        const auto isArriving = [&](const TileSnap& s) {
            const QRect& rect = screenRectFor(s);
            return rect.isValid() && rect.intersects(s.geometry);
        };
        const auto visibleX = [&](const TileSnap& s) {
            if (!s.window || isArriving(s)) {
                return s.geometry.x();
            }
            return qRound(s.window->frameGeometry().x());
        };
        // Net travel across the batch decides which end leads, measured on
        // STAYING columns only: an arriving column's current frame is the
        // park, whose x carries no direction (the park is direction-agnostic
        // by design), so its term could swamp every staying column's true
        // delta with an arbitrary sign. Leaving columns contribute zero by
        // construction (visibleX keys them on their own current frame).
        qint64 netDx = 0;
        for (const TileSnap& s : toApply) {
            if (s.window && !isArriving(s)) {
                netDx += visibleX(s) - qRound(s.window->frameGeometry().x());
            }
        }
        bool movingRight = netDx > 0;
        if (netDx == 0) {
            // No staying column moved (all-leaving or all-arriving batch).
            // The scrollEdge is authoritative there: a column LEAVES by the
            // edge the content moves toward, and ARRIVES from the edge it
            // once left by (content moving away from it).
            for (const TileSnap& s : toApply) {
                if (s.scrollEdge.isEmpty()) {
                    continue;
                }
                const bool edgeIsRight = s.scrollEdge != QLatin1String("left");
                movingRight = isArriving(s) ? !edgeIsRight : edgeIsRight;
                break;
            }
        }
        std::stable_sort(toApply.begin(), toApply.end(), [&](const TileSnap& a, const TileSnap& b) {
            return movingRight ? visibleX(a) > visibleX(b) : visibleX(a) < visibleX(b);
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

    const bool hasApplies = !toApply.isEmpty();
    auto onComplete = [this, newTiledByScreen, savedGlobalStack, overlapStackByScreen, gen, genByScreen, hasApplies]() {
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
            const QSet<QString>& newSet = screenIt.value();
            const QSet<QString> previous = TilingStateHelpers::tiledOnScreen(m_border, screenId);
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
                if (!TilingStateHelpers::isTiledWindow(m_border, wid)) {
                    m_tileTargetZones.remove(wid);
                    m_centeredWaylandZones.remove(wid);
                }
            }
        }
        // A batch with NO tile applies (every request resolved away or the
        // batch was floats-only) still owes the untile cleanup above, but
        // must not churn the whole stacking order or spend the one-shot
        // saved-order/pending-focus state — nothing moved, so there is no
        // z-order to repair.
        auto* ws = KWin::Workspace::self();
        if (ws && hasApplies) {
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
            if (KWin::EffectWindow* active = KWin::effects->activeWindow();
                active && overlapMemberScreen.contains(active)) {
                if (KWin::Window* kw = active->window()) {
                    ws->raiseWindow(kw);
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

        // Put the tab-indicator surfaces back at the bottom of their layer.
        // savedGlobalStack is an unfiltered snapshot of the whole stacking
        // order, so it contains them, and the raise loop above replays it — a
        // snapshot taken before the lower and replayed after would restore the
        // pre-lower order and leave the indicators painting across the layout
        // picker and the cheatsheet. A scroll batch is exactly that window,
        // since the daemon announces the surface for the same strip change
        // this batch is applying. Free when no indicator exists: the function
        // returns on an empty id set.
        m_effect->restackScrollTabSurfaces();

        // Wayland centering is handled reactively by slotWindowFrameGeometryChanged
        // as soon as the client commits its constrained size — no deferred timer needed.

        // Refresh the active border for the focused window (tiledWindows may have changed)
        m_effect->updateAllDecorations();
    };

    m_effect->applyStaggeredOrImmediate(
        toApply.size(),
        [this, toApply, gen, genByScreen, startedViewScreens](int i) {
            // Local copy (not const ref) so a stale window pointer can be
            // re-resolved below; the rest of the body reads snap.window.
            TileSnap snap = toApply[i];
            // Drop this apply if superseded by a desktop/screen switch (global
            // epoch) OR by a newer retile of THIS window's screen (per-screen).
            // A batch for a DIFFERENT screen no longer cancels us — that was the
            // cross-output "hole on the source monitor" bug.
            if (m_tileStaggerGeneration != gen
                || m_tileStaggerGenByScreen.value(snap.screenId) != genByScreen.value(snap.screenId)) {
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
                                  << genByScreen.value(snap.screenId);
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
            // A window can only be tile-managed by one screen at a time —
            // markWindowTiled enforces the single-owner sweep itself.
            markWindowTiled(snap.screenId, snap.windowId);
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
            if (snap.hasVisualPos) {
                m_effect->m_scrollVisualPos.insert(snap.windowId, snap.visualPos);
            } else {
                m_effect->m_scrollVisualPos.remove(snap.windowId);
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
                if (snap.isWindowedFullscreen && !inSet) {
                    // Adopt-on-batch: also the effect-restart path, where the
                    // daemon still holds the flag for a window this effect
                    // instance has never seen. The stored rect is what the
                    // committed-ack re-assert in slotWindowFullScreenChanged
                    // applies.
                    m_effect->m_windowedFullscreenWindows.insert(snap.windowId, snap.geometry);
                    if (!kwFs->isFullScreen()) {
                        ++m_suppressFullScreenChanged;
                        kwFs->setFullScreen(true);
                        --m_suppressFullScreenChanged;
                    }
                    applyWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                } else if (snap.isWindowedFullscreen && inSet && !kwFs->isRequestedFullScreen()) {
                    // Flagged, member, yet fullscreen is not even REQUESTED:
                    // the client exited on its own while the daemon gate was
                    // closed, and the exit slot deferred its reconcile to
                    // exactly this moment. Deliver it now — membership
                    // drops, the daemon clears its flag and re-applies —
                    // rather than re-asserting fullscreen against the
                    // user's exit. Requested state, not committed: during
                    // our OWN enter round-trip committed lags behind while
                    // requested is already true, and a batch landing in that
                    // window must not read the lag as an exit.
                    m_effect->m_windowedFullscreenWindows.remove(snap.windowId);
                    restoreWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                    qCInfo(lcEffect) << "Windowed-fullscreen deferred reconcile for" << snap.windowId;
                    if (m_effect->m_daemonGate.serviceRegistered) {
                        PhosphorProtocol::ClientHelpers::fireAndForget(
                            m_effect, PhosphorProtocol::Service::Interface::Scrolling,
                            QStringLiteral("clearWindowedFullscreen"), {snap.windowId},
                            QStringLiteral("clearWindowedFullscreen"));
                    }
                } else if (snap.isWindowedFullscreen && inSet) {
                    // Keep the stored rect current — the strip may have
                    // resized or scrolled the column since the flag went on.
                    m_effect->m_windowedFullscreenWindows.insert(snap.windowId, snap.geometry);
                    // Re-assert the layer demotion too (change-gated in KWin,
                    // free in the steady state): a manual keep-flag toggle
                    // under the hold is re-asserted away on the next batch,
                    // the same ownership KWin rules claim while they match.
                    applyWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                } else if (!snap.isWindowedFullscreen && inSet) {
                    if (kwFs->isFullScreen()) {
                        ++m_suppressFullScreenChanged;
                        kwFs->setFullScreen(false);
                        --m_suppressFullScreenChanged;
                    }
                    m_effect->m_windowedFullscreenWindows.remove(snap.windowId);
                    restoreWindowedFullscreenLayerDemotion(snap.windowId, kwFs);
                }
            }

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
                // intentionally absorbed by the m_daemonGate.inGeometryApply guard
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
                    // Scrolling strip: scrollEdge names the screen edge this
                    // column's motion belongs to. It is NOT recoverable from
                    // the geometry — the park position is direction-agnostic
                    // (below the union of all outputs), so the rect cannot
                    // say which side the user scrolled.
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
                    if (snap.hasVisualPos) {
                        // Parked, but drawn at its real strip position and
                        // carried by the view like every other column. There is
                        // no per-window motion left to describe, so the leg is
                        // deliberately degenerate — the edge-anchored slide-out
                        // below would fight the view offset for the same pixels.
                        originOverride = QRectF(geo);
                    } else if (snap.viewDeltaX != 0 && startedViewScreens.contains(snap.screenId)) {
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
                        // and the offset starts at exactly viewDeltaX. So for
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
                        if (!snap.scrollEdge.isEmpty()) {
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
                            originOverride = QRectF(geo);
                        } else {
                            const KWin::RectF cur = snap.window->frameGeometry();
                            originOverride = QRectF(cur.x() - snap.viewDeltaX, cur.y(), cur.width(), cur.height());
                        }
                    } else if (!snap.scrollEdge.isEmpty()) {
                        const KWin::LogicalOutput* out = m_effect->outputForScreenId(snap.screenId);
                        const QRect screenRect = out ? QRect(out->geometry()) : QRect();
                        if (screenRect.isValid()) {
                            const bool arriving = screenRect.intersects(geo);
                            // Arriving: start from the target's own row and
                            // size. Leaving: keep the rect the window occupies
                            // right now, so it slides out as itself rather
                            // than jumping to the park's row first.
                            QRect atEdge = geo;
                            if (!arriving) {
                                const KWin::RectF cur = snap.window->frameGeometry();
                                atEdge =
                                    QRect(qRound(cur.x()), qRound(cur.y()), qRound(cur.width()), qRound(cur.height()));
                            }
                            if (snap.scrollEdge == QLatin1String("left")) {
                                atEdge.moveLeft(screenRect.left() - atEdge.width());
                            } else {
                                // QRect::right() is x+width-1, so +1 sits one
                                // past the edge. screenRect must stay a QRect
                                // (KWin::Rect::right() is x+width and would
                                // shift the slide by a pixel).
                                atEdge.moveLeft(screenRect.right() + 1);
                            }
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
                        // Edge-less commit of a scroll-tracked window whose
                        // target is entirely off its own output: a vertical
                        // stack-overflow park. There is no side to slide out
                        // by, and animating to a target below the union would
                        // sweep the window down the whole screen. Commit
                        // without an animation. (Autotile batches never take
                        // this branch — their windows are not scroll-tracked
                        // and their targets are on-screen anyway.)
                        const KWin::LogicalOutput* out = m_effect->outputForScreenId(snap.screenId);
                        const QRect screenRect = out ? QRect(out->geometry()) : QRect();
                        if (screenRect.isValid() && !screenRect.intersects(geo)) {
                            skipScrollAnimation = true;
                        }
                    }
                    m_effect->applyWindowGeometry(snap.window, geo, /*allowDuringDrag=*/false, skipScrollAnimation,
                                                  PhosphorAnimation::ProfilePaths::WindowSnapIn, originOverride,
                                                  visualTargetOverride);
                }
            }

            if (!snap.isMonocle && snap.window->isWaylandClient()) {
                m_tileTargetZones[snap.windowId] = snap.geometry;
            }
        },
        onComplete, startedViewLegs);
}

void TilingHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    Q_UNUSED(oldGeometry)
    if (!w) {
        return;
    }

    // Fast bail: skip getWindowId entirely when neither VS detection nor centering needs it
    if (m_effect->m_virtualScreenDefs.isEmpty() && m_tileTargetZones.isEmpty()) {
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
        && m_effect->m_daemonGate.virtualScreensReady && !m_effect->m_daemonGate.inGeometryApply) {
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

    if (m_tileTargetZones.isEmpty()) {
        return;
    }

    auto it = m_tileTargetZones.find(windowId);
    if (it == m_tileTargetZones.end()) {
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
    m_pendingAutotileFocusWindowId = windowId;
    KWin::effects->activateWindow(w);
}

void TilingHandler::reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight)
{
    if (minWidth <= 0 && minHeight <= 0) {
        return;
    }

    qCInfo(lcEffect) << "Discovered min size for" << windowId << ":" << minWidth << "x" << minHeight
                     << "- reporting to daemon for future retiles";

    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowMinSizeUpdated"),
        {windowId, minWidth, minHeight}, QStringLiteral("windowMinSizeUpdated"));
}

} // namespace PlasmaZones
