// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include "scrollenginelogging.h"
#include "scrollpark_p.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace PhosphorScrollEngine {

// kParkMargin and kMinVisiblePeekPx moved to scrollpark_p.h alongside the
// placement decision they govern; this TU only needs the margin, to derive
// the park line that separates "was parked last batch" from "was on screen".
// (The self-activation cap that used to sit here is a ScrollEngine class
// constant now — kMaxPendingSelfActivations — shared with the verb TU.)

void ScrollEngine::applyLayout(const QString& screenId, bool focusWindowAfter)
{
    // Membership guard, the same one every retile entry point applies
    // (retile, scheduleRetileForScreen and its queued callback) — but here
    // rather than only there, because applyLayout has direct callers too.
    // The guard matters beyond wasted work: setActiveScreens prunes a
    // departing screen's CURRENT context and releases the engine's ownership
    // of it (m_context.releaseScreenOwnership), which drops this engine's
    // sticky pin. The per-output desktop entry deliberately SURVIVES — it is
    // compositor truth the engine cannot re-derive — but the pin's removal can
    // still move where currentKeyForScreen resolves for a pinned screen. If the
    // resulting key happens to match a surviving background state, an unguarded
    // pass would emit a tile batch — and possibly a focus activation — for a
    // screen this engine no longer owns.
    if (!m_scrollingScreens.contains(screenId)) {
        return;
    }
    ScrollState* state = stateForKey(currentKeyForScreen(screenId), false);
    if (!state) {
        // No state for the CURRENT context (fresh desktop, or the state was
        // just pruned) — the previous context's indicator must not stay
        // painted. The KWin effect that paints the pills is driven solely by
        // tabStripsChanged, so this bail is its only chance to clear.
        clearTabStripsForScreen(screenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        // Screen went away or gaps swallowed it: the indicator must not
        // stay painted, and a scheduled-retile storm must not spam a
        // warning per tick (debug level; the first clear is the signal).
        clearTabStripsForScreen(screenId);
        qCDebug(lcScrollEngine) << "applyLayout: no valid work area for screen" << screenId;
        return;
    }
    // Live drag-insert preview on THIS screen: the view must not move for the
    // rest of the hold. That is the DETACH-ONCE invariant this whole design
    // rests on — the strip settles once when begin detaches the window, and
    // anything that slides the layout under a stationary cursor afterwards is
    // what killed the live-restructure design (see drag_preview.cpp's header).
    // Re-applying the centering policy on an incidental pass mid-drag would do
    // exactly that. screensMatch, not ==, so a virtual-screen id spelling
    // difference cannot fail the guard open.
    const bool dragPreviewSteersView = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId);
    // This apply belongs to the edge auto-scroll heartbeat: the view motion is
    // user-driven and continuous, so the batch is stamped viewImmediate and
    // the effect applies the delta outright instead of animating it. Latched
    // here, beside dragPreviewSteersView, so no later consumer re-reads
    // m_dragInsertPreview across the prune paths this function can reach.
    const bool viewImmediateApply = dragPreviewSteersView && m_dragInsertPreview->autoScrollOwnsTarget;

    // Captured before the axis-flip sweep, not after: that sweep re-centres
    // the active column, and a baseline taken downstream of it would already
    // hold the re-centred value, making the flip's own anchor move invisible
    // to the anchorMoved test below and losing the persist for it.
    const int anchorBefore = state->strip().viewAnchor();

    // AXIS FLIP SWEEP, before anything reads the anchor.
    //
    // Written as a pure function of observed state rather than hooked to a
    // signal, which buys four properties an event hook cannot have: it is
    // IDEMPOTENT (a redundant retile converts nothing, since the predicate is
    // axis inequality against the recorded basis), ORDER-FREE (whichever of
    // the geometry change and the override push lands first, the next
    // applyLayout sees the final params), COALESCING-SAFE (a rotate and
    // rotate-back inside one debounce window collapses to zero work), and it
    // handles BACKGROUND contexts lazily — a background desktop's strip
    // converts when you switch to it, because its own recorded basis still
    // says the old axis until then.
    const bool axisFlipPending = state->hasResolvedAxis() && state->resolvedAxis() != params.axis;
    if (axisFlipPending && !dragPreviewSteersView) {
        // The anchor is main-axis pixels measured against a viewport extent
        // that just changed meaning. Out-of-range anchors are LEGITIMATE under
        // the current axis (a centred anchor implies one by design, which is
        // why restoreViewAnchor refuses to clamp), so after a flip there is no
        // way to tell a legitimate one from garbage. Re-derive instead of
        // converting: centre the active column, which is the one derivation
        // well-defined without the old viewport and puts the focused window
        // where the user is looking.
        state->strip().centerActiveColumn(params);
        // Every remembered departure edge names a side of the OLD axis, so it
        // would anchor the next arrival animation to a side the strip no
        // longer has. Same reasoning as the float, handoff and re-adoption
        // paths that already evict it. Screen-scoped through this state's own
        // windows, because the map is engine-level and keyed by window id.
        const QStringList flipped = state->strip().windowsInOrder();
        for (const QString& windowId : flipped) {
            m_parkedScrollEdge.remove(windowId);
        }
    }
    // The whole sweep — conversion AND basis stamp — defers while a drag
    // preview steers the view, for updateViewForFocus's DETACH-ONCE reason: a
    // non-geometry flip (an axis rule or template pick routing through
    // applyPerScreenConfig with a byte-identical work area) reaches here
    // mid-drag without the geometry path's preview cancel, and re-centring
    // would slide the layout under a stationary cursor. Leaving the recorded
    // basis on the OLD axis is what makes the sweep re-fire and convert on
    // the next non-drag applyLayout; stamping it while skipping the
    // conversion would strand the old-axis anchor forever.
    if (!(axisFlipPending && dragPreviewSteersView)) {
        state->setResolvedAxis(params.axis);
    }

    // Re-apply the centering policy before resolving: a work-area change
    // (resolution, panels, outer gaps) or a centering-settings flip leaves
    // the stored anchor relative to the OLD width, and nothing else
    // re-derives it until the next focus move. Idempotent for a settled
    // strip (a fully-visible column stays put under Never/OnOverflow).
    if (!dragPreviewSteersView) {
        state->strip().updateViewForFocus(params);
    }
    // The anchor is PERSISTED state (serializeStripState) and
    // placementChanged is the only producer of DirtyScrollStrips. The
    // focus-moving verbs all emit it themselves, but the re-anchor here also
    // fires on retile-ONLY entry points (work-area change, a settings flip, a
    // scheduled retile), which have no emit of their own — so without this a
    // re-anchor that is the session's last strip event is never saved and the
    // strip comes back scrolled to the pre-change view. Emitted at the tail,
    // once the geometry has actually been applied.
    const bool anchorMoved = state->strip().viewAnchor() != anchorBefore;
    const ResolvedStrip resolved = state->strip().relayout(params);
    if (resolved.columns.isEmpty()) {
        // The strip just emptied (last window closed / floated / released),
        // or every column is minimized away. The tab-strip clear must still
        // run — returning before it would leave the indicator painted on an
        // empty screen forever — and an anchor the re-anchor above already
        // moved is persisted state that has to be marked dirty on this exit
        // too, or the move is never saved.
        //
        // The view-delta baseline is INVALIDATED, not left standing: this
        // state object survives the empty period, and a baseline captured
        // before it would make the repopulating batch compute a delta
        // against a coordinate nothing on screen occupies — flying the next
        // window in from wherever the old view sat. (The arr-empty
        // interactive-drag bail further down deliberately does NOT clear:
        // its strip still has resolved columns the compositor is showing.)
        //
        // Deliberately no focusWindowAfter activation on this exit either —
        // there is no tile to activate on an empty resolve, and the caller's
        // request dies with the batch it was for.
        state->clearLastAppliedViewOffset();
        // Same empty-strip reasoning applied to the template blueprint: the
        // cursor records which entries this strip's columns already stand
        // for, and a strip with nothing left has no columns to stand for any
        // of them. A screen cleared out and repopulated therefore opens from
        // the top of the blueprint again, which is the "template describes
        // the starting shape" contract in ScrollState::blueprintCursor.
        //
        // Gated on the STRIP being empty, not on this resolve being empty.
        // The two differ whenever the columns are transiently unresolvable
        // while still standing for their entries: every column minimized
        // away (relayout skips a fully-minimized column), the last tiled
        // window floated (floatWindowInternal re-applies synchronously), or
        // the only window detached into a drag-insert preview. Resetting on
        // the resolve handed those entries straight back out on the next
        // open — the same refill the cursor was added to stop. Floating
        // members and a live detached drag window both count as "the screen
        // still holds something", because both return to the strip through
        // paths that consume no blueprint entry.
        // screensMatch, not ==, for the reason the preview guard at the top of
        // this function states: a virtual-screen id spelling difference must
        // not fail the test OPEN and reset a cursor the drag is still holding
        // a window out of. The CONTEXT is compared too, because the preview
        // captured one at begin: a preview detached on another desktop of this
        // same screen says nothing about whether THIS context's strip is
        // genuinely finished, and treating it as a hold left that context's
        // cursor standing forever.
        const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);
        const bool dragHoldsWindowHere = m_dragInsertPreview
            && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetKey.screenId, screenId)
            && m_dragInsertPreview->targetKey.desktop == currentKey.desktop
            && m_dragInsertPreview->targetKey.activity == currentKey.activity;
        if (state->strip().isEmpty() && !state->hasFloating() && !dragHoldsWindowHere) {
            state->resetBlueprintCursor();
        }
        clearTabStripsForScreen(screenId);
        if (anchorMoved) {
            Q_EMIT placementChanged(screenId);
        }
        return;
    }

    // Parking bounds come from the FULL screen geometry, not the work area —
    // a rect just outside the work area could still sit on-screen over a
    // panel. Fall back to the work area when the screen rect is unknown.
    QRect screenRect = m_screenManager ? m_screenManager->screenGeometry(screenId)
                                       : (m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect());
    if (!screenRect.isValid()) {
        screenRect = params.workArea;
    }

    // Parking answers WHERE to put an off-viewport column. It deliberately
    // does NOT answer which way that column appears to move — that travels
    // separately, as the scrollEdge field on each tile request, and the
    // effect anchors the animation to it. That split is what makes ONE park
    // rule possible at all: with direction carried as data, the position
    // only has to be safe, and there is exactly one place that is safe on
    // every monitor topology — below the union of ALL outputs. No point
    // under the union's bottom edge belongs to any monitor, by definition,
    // so no resolver consultation, no per-side preference chain, and no
    // boxed-in degraded case (the old side-picking logic had all three, and
    // a fully surrounded monitor had nowhere safe at all).
    //
    // Why safety is geometry's job: a rect committed inside a neighbouring
    // output is that output's window as far as KWin is concerned — drawn
    // there, taking input there, output reassigned — and no after-the-fact
    // pixel suppression undoes that.
    //
    // The parked rect keeps its x within its OWN screen's horizontal span
    // (as close to the strip-derived x as fits). Note the y is the GLOBAL
    // union bottom, so on a mixed-height topology a short screen's park sits
    // well below its own monitor but only just below the tallest one —
    // "directly below its monitor" holds only for the bottom-most output.
    // The safety argument does not rest on nearest-output attribution; it
    // rests on the region below the union belonging to NO monitor. The
    // modest offset — just past the union, not at extreme coordinates — is
    // the same stuck-off-screen-folklore guard the old parks carried.
    int unionBottom = screenRect.bottom();
    if (m_screenManager) {
        // screens() copies the vector; accepted — this runs per retile, not
        // per frame, and caching it would add a screen-change invalidation
        // path for a handful of small structs.
        const auto allScreens = m_screenManager->screens();
        for (const auto& s : allScreens) {
            unionBottom = qMax(unionBottom, s.geometry.bottom());
        }
    } else if (m_allScreenGeometriesProvider) {
        const QList<QRect> allRects = m_allScreenGeometriesProvider();
        for (const QRect& r : allRects) {
            unionBottom = qMax(unionBottom, r.bottom());
        }
    }
    // A window under a compositor interactive move keeps its geometry with
    // KWin — skip it in the batch (and leave its m_lastAppliedRect memory
    // alone) while neighbours animate. The one live source is the daemon's
    // whole-drag interactive mark, which covers trigger-not-held stretches
    // of the drag, where the window is still modelled as a strip tile the
    // effect merely floats visually. (A drag-insert preview's window never
    // appears in a resolved tile: begin DETACHES it, and commit/cancel
    // reset the preview before re-inserting, so no second filter source is
    // needed.) The daemon clears the mark BEFORE the drop settles, so the
    // finalizing relayout runs unfiltered.
    // How far the VIEW slid since the last emitted batch, as opposed to how
    // far any one window moved. Every carried window in this batch shares it,
    // so the effect can spring it ONCE per output and let the strip ride it
    // rigidly, instead of starting an independent per-window spring each and
    // watching them desync into a shear.
    //
    // Sign: a window's x is `workArea.x - viewOffset + stripX`, so a view that
    // scrolls right (viewOffset grows) moves windows left. The delta below is the
    // translation that puts a window back where it was rendered last time —
    // the effect starts its spring there and rings it out to zero.
    //
    // Zero on the first batch for a context: there is nothing on screen to
    // slide from, so the windows are placed outright.
    //
    // Zero also when the work area MOVED since the baseline was stamped.
    // Column widths are fractions of that area, so a resolution change, a
    // panel appearing or a gap edit rescales every column's strip position and
    // with it the view coordinate — proportionally to how deep the anchor sits
    // on the strip, which on a long strip is thousands of pixels. Subtracting
    // across two bases describes a slide nobody made, and the effect would fly
    // the entire strip in from off-screen to ring it out, once per emitted
    // change while a gap slider is being dragged. The batch that follows a
    // work-area change is placing windows in a new geometry anyway, which is
    // the same situation as the first batch for a context.
    // The AXIS is part of the basis, and its term is NOT redundant with the
    // work-area compare. A rotation changes the rect's shape, so that compare
    // already catches it — but a FLIP WITH NO GEOMETRY CHANGE does not: a
    // settings toggle, an axis rule or a template pick routes through
    // applyPerScreenConfig with a byte-identical work area. Without this term
    // such a flip springs a delta measured along the old axis and the whole
    // strip lurches.
    const bool sameBasis = state->hasLastAppliedViewOffset() && state->lastAppliedWorkArea() == params.workArea
        && state->lastAppliedAxis() == params.axis;
    const int rawViewDelta = sameBasis ? resolved.viewOffset - state->lastAppliedViewOffset() : 0;
    // Zero also when the viewOffset moved WITHOUT carrying anything — a width
    // change to a column LEFT of the active one shifts strip coordinates and
    // the view coordinate by the same amount to keep the anchor put, which
    // the subtraction above reads as a slide nobody made. The test needs
    // POSITIVE evidence in both directions, because the comparands differ:
    // tile.rect here is the RAW resolved rect while m_lastAppliedRect holds
    // the COMMITTED (possibly parked or edge-clamped) rect, so a batch whose
    // every tile straddled last time matches neither pattern and must keep
    // its delta (zeroing a mid-flight slide is worse than a redundant one —
    // the effect never retargets a leg it is not told about). The
    // compensation signature is a tile that STAYED PUT on screen while the
    // view coordinate moved: it was committed unmodified last batch, and a
    // genuine scroll would have carried it. Only when at least one tile
    // shows that signature and none shows the carried signature is this a
    // reflow rather than a scroll.
    int viewDelta = rawViewDelta;
    if (rawViewDelta != 0) {
        bool anyCarried = false;
        bool anyStayedPut = false;
        for (const ResolvedColumn& column : resolved.columns) {
            for (const ResolvedTile& tile : column.tiles) {
                // The dragged window's rect memory is deliberately frozen
                // (the emit loop below skips it), so its stale entry is not
                // evidence in either direction — the two loops must agree
                // about which rects are trustworthy.
                if (!m_interactiveDragWindow.isEmpty() && tile.windowId == m_interactiveDragWindow) {
                    continue;
                }
                const auto lastIt = m_lastAppliedRect.constFind(tile.windowId);
                if (lastIt == m_lastAppliedRect.constEnd()) {
                    continue;
                }
                // Along the MAIN axis: the view only ever slides along the
                // strip, so the carried signature is a rect displaced on that
                // axis alone. Hardcoding x here would make anyCarried
                // unreachable on a vertical strip, and the test below would
                // then zero the delta for EVERY vertical batch — silently
                // dropping the one-spring-per-output slide rather than
                // failing.
                if (tile.rect == params.axis.translatedMain(*lastIt, -rawViewDelta)) {
                    anyCarried = true;
                    break;
                }
                if (tile.rect == *lastIt) {
                    anyStayedPut = true;
                }
            }
            if (anyCarried) {
                break;
            }
        }
        if (!anyCarried && anyStayedPut) {
            viewDelta = 0;
        }
    }

    QJsonArray arr;
    bool anyEntryChanged = false;
    // Whether the emit loop produced at least one entry whose committed rect
    // is a real placement rather than a park. Gates the suppressed-batch
    // baseline advance below: a park's rect is constant regardless of the
    // view, so an all-parked batch can have unchanged rects while the view
    // genuinely moved, and advancing the baseline there would eat the slide
    // the next arrival batch owes the effect.
    bool anyEmittedUnparked = false;
    // Per columnIndex: did EVERY tile this loop emitted for that column end up
    // parked? A column with no emitted tiles at all gets no entry, so the
    // tab-strip loop's lookup default leaves it alone — and a column whose
    // VISIBLE tile was taken by the interactive-drag skip must not tally as
    // fully parked off its hidden siblings alone, or its live tab bar would
    // vanish for the whole drag; the skipped set vetoes the tally below.
    // Consumed at the strip loop.
    QHash<int, bool> columnAllParked;
    QSet<int> columnHadSkippedTile;
    // Resolved ONCE for the whole batch, not per tile: crop mode is a
    // per-SCREEN verdict, and the screenId accessor rebuilds this screen's
    // override map on every call — which, inside the emit loop, is once per
    // window per relayout. Same doctrine as layoutParamsForScreen's single
    // fetch at the top of this file.
    // Resolved once with the rest of the per-screen bools in
    // layoutParamsForScreen — reading it off params keeps this batch from
    // fetching the override map a second time for one value.
    const bool cropStraddlers = params.cropStraddlers;
    // The park's top edge, the one number that separates "was on screen last
    // batch" from "was parked last batch" in m_lastAppliedRect. Every park
    // this pass commits lands exactly here (the park lambda moves only the
    // top), so a remembered rect at or below it was a park and one above it
    // was a real placement.
    //
    // KNOWN TRANSIENT: the remembered rects are classified against THIS
    // batch's park line, and a monitor ADD between batches raises the union
    // bottom without any sweep of the old entries (removal IS swept, at the
    // context prune). For exactly one batch after the add, a rect parked
    // below the OLD line can read as on-screen and mis-source a tab
    // cross-fade; the batch then rewrites every entry and the window heals.
    // Storing the park line per state would close it, at the cost of one
    // more cross-batch memory for a single cosmetic frame — deliberately not
    // taken.
    const int parkTop = unionBottom + 1 + Detail::kParkMargin;
    // BOTH pairing predicates demand POSITIVE evidence, symmetrically: an
    // entry must exist AND sit on the right side of the park line. A missing
    // entry means neither — onWindowResized's refused-ack arm drops a live
    // tile's rect memory on purpose (see the m_parkedScrollEdge contract
    // note below), and reading "no memory" as "was on screen" let such a tab
    // be named as the outgoing half of a swap that never happened, or shadow
    // the genuinely-outgoing sibling in a 3+ tab column.
    const auto wasParked = [&](const QString& windowId) {
        const auto it = m_lastAppliedRect.constFind(windowId);
        return it != m_lastAppliedRect.constEnd() && it->top() >= parkTop;
    };
    const auto wasOnScreen = [&](const QString& windowId) {
        const auto it = m_lastAppliedRect.constFind(windowId);
        return it != m_lastAppliedRect.constEnd() && it->top() < parkTop;
    };
    // Minimum sizes for the whole batch in ONE pass over the model. Asking
    // the strip per tile inside the emit loop costs a columnOfWindow scan
    // plus an indexOfWindow per window, which is quadratic in the strip's
    // window count; walking the columns directly is linear. Only built when
    // the park actually consumes it.
    QHash<QString, QSize> tileMinSizes;
    if (params.respectMinimumSize) {
        for (const Column& column : state->strip().columns()) {
            for (const Tile& tile : column.tiles) {
                tileMinSizes.insert(tile.windowId, QSize(tile.minWidth, tile.minHeight));
            }
        }
    }
    for (const ResolvedColumn& column : resolved.columns) {
        // TAB SWITCH pairing. A tabbed column shows one tile and parks the
        // rest, so activating a tab is two commits that share one rect: the
        // outgoing tab parks and the incoming tab takes the rect it vacated.
        // The compositor cannot pair those on its own — both entries are
        // ordinary rects, and inferring the pair from rect coincidence would
        // also fire on a column that merely re-laid out — so the engine names
        // the outgoing tab and the effect cross-fades one into the other.
        //
        // Derived from m_lastAppliedRect rather than a remembered hidden-set:
        // that map is already swept by every path that drops a window
        // (close, float, handoff, drag), so this cannot strand a pairing
        // against a window that is no longer a tile.
        //
        // The pairing needs BOTH halves to be genuine, which is what keeps it
        // to real switches: a tile that is hidden now but was on screen last
        // batch, and (at the emit below) a tile that is shown now but was
        // parked last batch. Tabbing a column for the FIRST time normally
        // hides tiles that were all on screen while the tile it leaves
        // showing was on screen too, so no pairing is emitted — nothing was
        // swapped. ONE exception, deliberate: under respectMinimumSize a
        // Normal stack can overflow the work area and park its trailing
        // tiles, so first-time tabbing such a column CAN pair (the shown tab
        // was overflow-parked, several siblings depart at once). The
        // first-match pick below is then visually arbitrary and deliberately
        // so — every candidate genuinely just vanished from the column's
        // rect, so any of them is a legitimate cross-fade source and refusing
        // to pick would cost the transition for no correctness gain. A column
        // scrolling back into view has parked hidden tiles, which fails the
        // outgoing half's positive on-screen requirement.
        QString tabFrom;
        if (column.tabbed) {
            for (const ResolvedTile& t : column.tiles) {
                // The dragged window's rect memory is deliberately frozen
                // (the emit loop below skips it), so it is not evidence —
                // the same doctrine the viewDelta evidence loop states: the
                // loops must agree about which rects are trustworthy. Without
                // this skip a tab dragged out of its column mid-switch could
                // be named as the swap's source while visibly floating under
                // the cursor.
                if (!m_interactiveDragWindow.isEmpty() && t.windowId == m_interactiveDragWindow) {
                    continue;
                }
                if (t.hidden && wasOnScreen(t.windowId)) {
                    tabFrom = t.windowId;
                    break;
                }
            }
        }
        // Maximize-to-edges, for the effect to mirror onto KWin's maximize
        // bit. DECLARED state read straight off the resolved column
        // (Column::maximizedToEdges via ResolvedColumn), never measured from
        // the rendered rect. The measuring predecessor (columnMaximized)
        // needed a pinned-by-minimum exclusion and a default-renders-full
        // exclusion to keep incidental full-width columns from latching the
        // titlebar button; a declared flag cannot be set incidentally, so
        // both exclusions die with the measurement. toggleMaximizeColumn is a
        // pure width verb now and has no wire representation at all — a
        // full-width column lights nothing, matching niri's Mod+F.
        const bool maximizedToEdges = column.maximizedToEdges;
        for (const ResolvedTile& tile : column.tiles) {
            if (!m_interactiveDragWindow.isEmpty() && tile.windowId == m_interactiveDragWindow) {
                // The dragged window gets NO entry at all for the whole
                // gesture, columnMaximized included — and since absence is the
                // only encoding of "not maximized", it is worth stating what
                // the effect does with that.
                //
                // Nothing: the effect's batch consumer is a per-entry loop, so
                // a window that appears in no entry is never visited and its
                // claim is neither released nor re-asserted. That is the
                // wanted outcome here. Releasing mid-drag would hand the
                // window's maximize bit back under the user's hand, and
                // re-asserting would fight the drag.
                //
                // The claim is closed at the OTHER end instead:
                // TilingHandler::reconcileMaximizeAfterGesture re-drives it
                // when the gesture finishes, which exists because the engine
                // emits on change and a drag that moves no column schedules
                // no batch to carry the answer.
                columnHadSkippedTile.insert(column.columnIndex);
                continue;
            }
            QRect rect = tile.rect;
            // Where this tile really sits on the strip, kept before any park
            // rewrites the rect. A parked column has to be SEEN travelling
            // while the view slides — during a fast scroll the columns whizzing
            // past are exactly the ones that have parked, and without this the
            // screen goes empty instead of showing the strip move.
            const QRect stripRect = rect;
            Detail::ParkInputs parkIn;
            parkIn.tileRect = rect;
            parkIn.columnRect = column.rect;
            parkIn.workArea = params.workArea;
            parkIn.screenRect = screenRect;
            parkIn.tileMin = params.respectMinimumSize ? tileMinSizes.value(tile.windowId, QSize(0, 0)) : QSize();
            parkIn.axis = params.axis;
            parkIn.parkTop = parkTop;
            parkIn.hidden = tile.hidden;
            parkIn.cropStraddlers = cropStraddlers;
            const Detail::ParkResult parkOut =
                Detail::resolveTilePlacement(parkIn, m_parkedScrollEdge.value(tile.windowId));
            rect = parkOut.rect;
            const bool parkedNow = parkOut.parked;
            // NOTE: a parked tile's committed x is deliberately NOT retained
            // from the last batch here, though an earlier fix did exactly
            // that. The instability it was chasing — a parked column
            // re-committed a few pixels over on every scroll step — is gone at
            // the source: parkRect now aligns to the END of the screen span
            // that the tile's DEPARTURE SIDE implies (ParkAlign), which does
            // not slide with the view, instead of clamping the live strip x,
            // which does.
            //
            // Retaining here on top of that would be strictly worse than
            // redundant. It keyed on m_lastAppliedRect, which some fifteen
            // unrelated paths drop for their own reasons — onWindowResized's
            // refused-ack arm most often, and that fires for exactly the
            // off-screen hidden tabs this was meant to steady — so the
            // retention silently lapsed and the ping-pong came back (seen live
            // as a hidden tab alternating x=1924/1932 while never unparking).
            // And when it did hold it would pin a stale x across a width
            // change, fighting the alignment above.
            anyEmittedUnparked = anyEmittedUnparked || !parkedNow;
            QString scrollEdge = parkOut.emittedEdge;
            // The helper is pure, so applying its verdict to the edge memory is
            // the caller's job. nullopt erases, a value stores — every path
            // through it decides one or the other.
            if (parkOut.rememberedEdge.has_value()) {
                m_parkedScrollEdge.insert(tile.windowId, *parkOut.rememberedEdge);
            } else {
                m_parkedScrollEdge.remove(tile.windowId);
            }

            QJsonObject obj;
            obj[QLatin1String("windowId")] = tile.windowId;
            obj[QLatin1String("screenId")] = screenId;
            obj[QLatin1String("x")] = rect.x();
            obj[QLatin1String("y")] = rect.y();
            obj[QLatin1String("width")] = rect.width();
            obj[QLatin1String("height")] = rect.height();
            // The flag rides the tile UNGATED by presentation: a parked
            // column and a hidden tab keep their client's fullscreen state.
            // The first design suppressed the flag off-canvas, and every
            // scroll past a flagged column then cycled the client's
            // fullscreen presentation off and on — two KWin state flips,
            // a restore/re-apply configure pair, and a decoration flap per
            // pass, all visible as resize flicker on the way in and out
            // (seen live with a flagged terminal). A client holding
            // fullscreen state at its park is inert: the compositor pins
            // flagged windows below the normal layer and exempts them from
            // the fullscreen geometry bail, so the off-canvas rect commits
            // like any other parked tile's.
            const bool windowedFs = tile.windowedFullscreen;
            if (windowedFs) {
                obj[QLatin1String("windowedFullscreen")] = true;
            }
            // Rides every tile of the column, and UNGATED by presentation for
            // the same reason the flag above is: a parked column and a hidden
            // tab keep the state, so scrolling past a maximized column does
            // not cycle its clients' maximize bit off and on. Repeating it per
            // tile rather than per column is what the flat per-window wire
            // shape requires — the effect has no column identity to hang it
            // on, and every tile of a maximized column is genuinely in a
            // maximized column.
            if (maximizedToEdges) {
                obj[QLatin1String("maximizedToEdges")] = true;
            }
            if (!scrollEdge.isEmpty()) {
                obj[QLatin1String("scrollEdge")] = scrollEdge;
            }
            // The incoming half of the tab-switch pairing resolved above: this
            // tile is shown now and was parked last batch, and a sibling made
            // the opposite trip. Named on the ARRIVING entry because that is
            // the window the compositor animates — the outgoing one is already
            // gone by the time anything paints. The !parkedNow term matches
            // the two neighbouring per-entry fields: a switch inside a column
            // that leaves the screen in the SAME batch has an arriving tab
            // with no on-screen rect, and naming a source for it made the
            // effect install a leg and capture a snapshot for a cross-fade
            // nothing can see.
            if (!tabFrom.isEmpty() && !tile.hidden && !parkedNow && wasParked(tile.windowId)) {
                obj[QLatin1String("tabFrom")] = tabFrom;
            }
            // A PARKED tile is not carried by the view. Its committed rect is
            // the park (below the union of all outputs), which no translation
            // can put back on screen, so it keeps the existing edge-anchored
            // slide-out the effect builds from scrollEdge. Zero is the honest
            // encoding of that rather than a second flag: zero means "the view
            // does not carry this window", which is equally true of a batch
            // where the view genuinely did not move.
            //
            // An ARRIVING tile (parked until now, on screen in this batch) DOES
            // ride it, and that is strictly better than the edge origin the
            // effect would otherwise synthesize: translating its final rect
            // back by the delta lands it at its real pre-scroll strip position,
            // which is where it actually was, rather than at a made-up point
            // just outside the screen edge.
            //
            // A tile the edge clamp pinned rides the view like every other
            // on-screen tile. The effect adds the per-output view offset to
            // EVERY scroll-managed window unconditionally, so withholding the
            // field from a pinned tile does not exempt it from the ride — it
            // only denies the effect the residual-origin leg that keeps frame
            // zero on the window, and the pinned peek column then detaches
            // from its edge by a full delta and slides back. With the field,
            // the effect's residual branch handles the pin with no special
            // case: the position leg and the view offset cancel (they start
            // and end together), while the clamped WIDTH change animates as
            // the residual — the exact contract the effect documents on its
            // own branch ("an edge column whose width changed in the same
            // batch keeps a real leg, so its width interpolates while its
            // position rides the strip").
            if (!parkedNow && viewDelta != 0) {
                obj[QLatin1String("viewDelta")] = viewDelta;
                // The edge auto-scroll heartbeat owns this batch's view
                // motion: it commits a fresh delta every ~16 ms, so the
                // per-tick commits ARE the animation and the effect must
                // apply them outright. Animating on top retargets the view
                // leg every tick, which on a stateless (duration) curve
                // resets its clock and zeroes its velocity each time — the
                // painted strip stalls behind the committed geometry, then
                // glides once when the ticks stop, while the drop indicator
                // and tab strips (computed from committed rects) run ahead.
                // Same doctrine as the daemon's un-animated indicator pushes
                // during the scroll.
                if (viewImmediateApply) {
                    obj[QLatin1String("viewImmediate")] = true;
                }
            }
            // A parked column keeps its strip position as a PAINT hint. The
            // commit above stays the park, which is the only rect that cannot
            // stray onto a neighbouring monitor, while the effect translates
            // the drawing back to where the column actually is and adds the
            // view offset — so it travels with the rest of the strip instead of
            // vanishing the instant it leaves the viewport.
            //
            // MAIN-AXIS (strip) parks only, and the departure edge is what says
            // so. Both other parks deliberately carry no edge: a CROSS-AXIS
            // stack-overflow park clears it ("the park is across the strip, so
            // there is no strip side to animate from") because that is layout
            // rather than strip motion, and a hidden tab of an ON-SCREEN tabbed
            // column records none because it is parked to keep it from stealing
            // input, not because the strip carried it away. Painting either back
            // would put a tile on screen that nothing scrolled: the cross-axis
            // one returns from past the stack edge the layout pushed it over, and the
            // hidden tab shares the active tab's rect, so every inactive tab of
            // every tabbed column would be drawn stacked on the visible one,
            // permanently, on a strip that is not even moving.
            //
            // The hidden-tab exclusion is EXPLICIT, not implied by the edge:
            // a hidden tab of an OFF-SCREEN tabbed column does record its
            // column's departure edge (the arrival-animation origin when
            // that tab is activated out of view), and in the tabbed layout
            // every tile of the column shares one stripRect — painting the
            // hint for the hidden tabs too would draw all N tabs stacked on
            // one point while the column travels past. Only the ACTIVE tab
            // rides the strip; the hidden ones stay at their invisible park.
            if (parkedNow && !scrollEdge.isEmpty() && !tile.hidden) {
                obj[QLatin1String("visualX")] = stripRect.x();
                obj[QLatin1String("visualY")] = stripRect.y();
            }
            arr.append(obj);
            if (const auto parkedIt = columnAllParked.find(column.columnIndex); parkedIt != columnAllParked.end()) {
                *parkedIt = *parkedIt && parkedNow;
            } else {
                columnAllParked.insert(column.columnIndex, parkedNow);
            }
            const auto lastIt = m_lastAppliedRect.constFind(tile.windowId);
            if (lastIt == m_lastAppliedRect.constEnd() || *lastIt != rect) {
                anyEntryChanged = true;
            }
            m_lastAppliedRect.insert(tile.windowId, rect);
            // The windowed-fullscreen flag rides the same payload but never
            // moves a rect, so it needs its own leg of the emit-on-change
            // gate — a toggle on an otherwise motionless strip must still
            // reach the compositor.
            if (m_lastAppliedWindowedFs.contains(tile.windowId) != windowedFs) {
                anyEntryChanged = true;
            }
            if (windowedFs) {
                m_lastAppliedWindowedFs.insert(tile.windowId);
            } else {
                m_lastAppliedWindowedFs.remove(tile.windowId);
            }
            // The maximize-to-edges flag needs the same leg: a toggle on a
            // column pinned wider than the raw area by its client minimum
            // moves no rect at all, and the un-maximize half of any toggle
            // can land rect-identical when the stored width still renders
            // full. Without this the batch is suppressed and the compositor
            // keeps asserting a maximize the engine has already dropped.
            if (m_lastAppliedMaximizedToEdges.contains(tile.windowId) != maximizedToEdges) {
                anyEntryChanged = true;
            }
            if (maximizedToEdges) {
                m_lastAppliedMaximizedToEdges.insert(tile.windowId);
            } else {
                m_lastAppliedMaximizedToEdges.remove(tile.windowId);
            }
        }
    }
    if (arr.isEmpty()) {
        // Reachable through the interactive-drag skip above: a strip whose
        // only resolved tile is the marked (dragged) window contributes no
        // entries. The behaviour is the same as the empty-strip bail —
        // clear the indicator, persist a moved anchor, emit nothing.
        // Deliberately no focusWindowAfter activation on this exit either:
        // the only window that could be activated is the one under the
        // user's drag, which already holds focus.
        clearTabStripsForScreen(screenId);
        if (anchorMoved) {
            Q_EMIT placementChanged(screenId);
        }
        return;
    }
    // Emit-on-change: a relayout that resolved every window to the exact
    // rect already applied (focus move under Never-centering, redundant
    // scheduled retile) must not re-feed the compositor's apply path.
    if (anyEntryChanged) {
        state->setLastAppliedViewOffset(resolved.viewOffset, params.workArea, params.axis);
        Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    } else if (anyEmittedUnparked) {
        // The baseline advances on a SUPPRESSED batch too, as long as at
        // least one emitted tile is a real (unparked) placement: unchanged
        // committed rects then mean the compositor is showing exactly the
        // positions this resolve produced, so resolved.viewOffset IS their
        // baseline. The case this closes is a structural change that shifts
        // strip coordinates and the view coordinate by the same amount
        // (closing the sole column LEFT of the view keeps every survivor's
        // rect byte-identical while viewOffset drops by the removed extent)
        // — without the advance, the stale baseline survives every further
        // suppressed batch and the first EMITTING batch ships a delta off by
        // exactly that shift. The one shape that must NOT advance here is an
        // all-parked batch: a park's rect is view-independent, so unchanged
        // rects there do not prove the view held still — anyEmittedUnparked
        // gates it out.
        state->setLastAppliedViewOffset(resolved.viewOffset, params.workArea, params.axis);
    }

    // Tab-strip indicator model: one entry per VISIBLE tabbed column.
    // Change-gated: the payload is re-derived on every relayout, but an
    // unchanged strip set (the common focus-move / plain-retile case) must
    // not re-drive a repaint in the effect — and an empty state is announced
    // exactly once (clearTabStripsForScreen latches on the tracked set).
    QJsonArray strips;
    for (const ResolvedColumn& column : resolved.columns) {
        // A null indicator rect is the single gate for "this column draws no
        // indicator" — it already folds in the master switch and the
        // single-tab skip (TabIndicatorParams::resolvesFor), so the emitter
        // never re-tests those and cannot disagree with the relayout that
        // decided how much space to reserve.
        // Visible at the view this batch resolved, OR at the one it is sliding
        // FROM. The second term is what lets a column scrolling out of view
        // keep its indicator for the length of the leg: the compositor applies
        // the strip's view offset to the pills in the same paint pass that
        // slides the columns, so an indicator dropped the moment its column's
        // final rect left the work area would vanish while the column it
        // labels is still on screen travelling. Translating by +viewDelta
        // undoes the slide, which is where the column was before this batch
        // moved the view.
        //
        // The extra entries cost nothing once at rest: they resolve outside the
        // screen, and the effect clips its painting to the screen's device
        // region.
        const bool visibleNow = column.rect.intersects(params.workArea);
        // Along the MAIN axis: the view only ever slides along the strip.
        const bool visibleBefore =
            viewDelta != 0 && params.axis.translatedMain(column.rect, viewDelta).intersects(params.workArea);
        if (!column.tabbed || column.tabIndicatorRect.isNull() || (!visibleNow && !visibleBefore)) {
            continue;
        }
        // Every tile of this column got parked, so nothing it labels is on
        // screen — the column's TRUE rect still intersects the work area (the
        // test above), which is exactly how a straddling column whose peek fell
        // below the floor left an orphan bar sitting at the edge. The parked
        // tally is read rather than tabIndicatorRect re-derived: re-deriving
        // from the clamped extent would disagree with the reservation the
        // relayout already spent (see the KNOWN LIMIT note below).
        //
        // A DEPARTING column is exempt, and without that exemption the
        // visibleBefore term above could never fire: a column that scrolled out
        // of view has every tile parked by the same work-area test that made
        // visibleNow false, so this skip would drop it before the outgoing leg
        // it was kept for. The orphan-bar case this guard exists for is a
        // column that is still visible NOW, so it is untouched.
        const bool departing = visibleBefore && !visibleNow;
        if (!departing && columnAllParked.value(column.columnIndex, false)
            && !columnHadSkippedTile.contains(column.columnIndex)) {
            continue;
        }
        // A windowed-fullscreen shown tab gets its indicator like any other:
        // the client is committed at the COLUMN rect, not the output (the
        // compositor exempts flagged members from the fullscreen bail and
        // re-applies the stored column rect), so the column is visually
        // exactly where the indicator says. An earlier suppression here
        // assumed a full-output presentation and silently deleted the
        // column's tab bar for the toggle's duration.
        QJsonObject strip;
        // The rect is the INDICATOR's, not the column's: it is what the
        // effect draws, and only this side knows how the position, gap and
        // within-column reservation combined to place it. Height and position
        // ride along so the consumer never re-derives the long axis from the
        // aspect ratio (a one-tab indicator can be square).
        //
        // KNOWN LIMIT, accepted: the rect derives from the column's TRUE
        // extent, while default clamp mode commits the straddling WINDOW
        // clamped at the screen edge — so on a straddling tabbed column the
        // bar can be offset from the window it labels, and a side-positioned
        // bar on the overhanging side clips away with the overhang (the effect
        // clips its painting to the screen, which is also how outward-placed
        // indicators at panel-free edges behave normally).
        // Re-deriving from the clamped extent was tried and rejected: an
        // intersect breaks legitimate outside-the-column placements, and a
        // second indicatorRectFor run against a clamped rect would disagree
        // with the reservation the relayout already spent.
        strip[QLatin1String("x")] = column.tabIndicatorRect.x();
        strip[QLatin1String("y")] = column.tabIndicatorRect.y();
        strip[QLatin1String("width")] = column.tabIndicatorRect.width();
        strip[QLatin1String("height")] = column.tabIndicatorRect.height();
        strip[QLatin1String("position")] = static_cast<int>(column.tabIndicatorPosition);
        QJsonArray tabs;
        // 0 is a safe seed, not a fallback: a resolved tabbed column always
        // carries exactly one non-hidden tile (relayout marks every tile
        // but the column's active one hidden), so the loop below always
        // overwrites it. An all-hidden column cannot be resolved — a
        // fully-minimized column is skipped before ResolvedColumns are
        // built.
        int activeIndex = 0;
        for (int i = 0; i < column.tiles.size(); ++i) {
            tabs.append(column.tiles.at(i).windowId);
            if (!column.tiles.at(i).hidden) {
                activeIndex = i;
            }
        }
        strip[QLatin1String("activeIndex")] = activeIndex;
        strip[QLatin1String("tabs")] = tabs;
        // No view delta rides along, unlike the tile wire above. The effect
        // paints the pills itself and applies the strip's view offset in its
        // own paint pass, so they need nothing but their resolved rect — the
        // offset that moves them is the same one that moves the columns,
        // applied in the same pass.
        strips.append(strip);
    }
    // Deliberately NOT frozen during an owned auto-scroll. A freeze-and-slide
    // scheme (skip the emit, let the compositor translate the stale content)
    // was tried and reverted: the frozen payload holds only the columns
    // visible at freeze time, so a sustained scroll empties the bar (revealed
    // columns were never rendered, departed pills slide off-screen), and no
    // compositor-side re-baseline can tell a fresh-payload commit from an
    // enrichment replay of the stale one. Per-tick pushes are the same
    // mechanism every ordinary scroll uses, and since the indicators became
    // compositor-drawn they cost no DAEMON render hop: this payload and the
    // tile batch leave the same applyLayout on one D-Bus connection, so the
    // effect repaints pills and columns from the same tick's state on the
    // same frame. Effect-side the push is not free — the painter's equality
    // gate compares rect-bearing indicators, so a pure translation still
    // re-rasterises the band and re-uploads its texture per tick; a
    // position-only fast path there (detect uniform translation, re-blit the
    // texture) is the accepted follow-up, deferred because it needs
    // live-verified painter surgery and the band is small.
    if (!strips.isEmpty()) {
        const QString payload = QString::fromUtf8(QJsonDocument(strips).toJson(QJsonDocument::Compact));
        if (m_lastTabStripPayload.value(screenId) != payload) {
            m_lastTabStripPayload.insert(screenId, payload);
            m_screensWithTabStrips.insert(screenId);
            Q_EMIT tabStripsChanged(screenId, payload);
        }
    } else {
        clearTabStripsForScreen(screenId);
    }

    if (focusWindowAfter) {
        const QString active = state->strip().activeWindowId();
        if (!active.isEmpty()) {
            // The engine is about to hand focus to a TILE — the float layer
            // loses it. windowFocused cannot record this itself: the echo
            // filter swallows the report before the float bookkeeping runs.
            // The flag write and the feedback are optimistic — a compositor
            // that drops the activation leaves the flag false until the next
            // genuine focus report heals it. Accepted, same as the
            // switchFocusBetweenFloatingAndTiling twin documents.
            state->setFloatingHasFocus(false);
            // Remember the request so windowFocused can tell this
            // activation's echo apart from genuine user focus (the echo
            // contract on windowFocused's drain). Bounded: an effect-side
            // drop leaves an entry behind until the clear-on-mismatch
            // reclaims it, and the cap keeps a pathological run of drops
            // from growing the queue without limit.
            queueSelfActivation(active);
            Q_EMIT activateWindowRequested(active);
        }
    }
    if (anchorMoved) {
        // See the anchorBefore capture above. Callers that already emit for
        // their own mutation get a second, harmless mark — the dirty flag is
        // idempotent, and gating on a real anchor move keeps this rare.
        Q_EMIT placementChanged(screenId);
    }
}

void ScrollEngine::queueSelfActivation(const QString& windowId)
{
    m_pendingSelfActivations.append(windowId);
    while (m_pendingSelfActivations.size() > kMaxPendingSelfActivations) {
        m_pendingSelfActivations.removeFirst();
    }
}

} // namespace PhosphorScrollEngine
