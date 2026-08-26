// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

// Minimum-size bookkeeping and the client-driven resize it feeds: the clamp a
// window reports, the re-layout its change forces, and the resize echo that has
// to tell an effect-applied rect apart from a user drag. Split from
// engine_lifecycle.cpp on that file's fourth size crossing, on the concern seam
// its earlier splits established — this trio answers "how big may this window
// be" rather than "is it here yet", and the open/close/focus arrivals it left
// behind never call into it except through the engine's own retile scheduling.

QSize ScrollEngine::windowMinimumSize(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (const ScrollState* state = stateForWindow(windowId)) {
        if (state->strip().containsWindow(windowId)) {
            // Verbatim, including one-axis clamps like 900x0: the strip
            // answers (0, 0) only for a window it does not hold.
            return state->strip().windowMinimumSize(windowId);
        }
    }
    // A floated (or, via the effect's minimize-as-float model, minimized)
    // window is not a strip tile, but its clamp is not unknown — the
    // FloatRestore entry carries it. The cross-engine handoff queries this
    // whatever state the window is in, and answering 0x0 hands the receiving
    // engine an unclamped window.
    const auto it = m_floatRestore.constFind(windowId);
    // Unknown window: an INVALID QSize, deliberately, and a divergence from
    // the sibling engines — AutotileEngine answers 0x0 for an unknown window,
    // which is also what an unconstrained known window answers. The two cases
    // are not the same thing here: the handoff asks this whatever state the
    // window is in, and "I have never heard of it" has to be distinguishable
    // from "it reported no minimum", or a receiving engine cannot tell a real
    // 0x0 clamp from a missing answer. Callers that just want a clamp can
    // treat both alike, since an invalid QSize's width/height are -1 and every
    // clamp site takes a qMax against 0.
    return it != m_floatRestore.constEnd() ? QSize(it->minWidth, it->minHeight) : QSize();
}

void ScrollEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // While the window floats there is no tile to write to, and unfloat
    // re-applies the captured clamp — so without this write-through the
    // restore puts back whatever the client reported at float time.
    if (const auto it = m_floatRestore.find(windowId); it != m_floatRestore.end()) {
        // Clamped like seedFloatRestoreForOpen: a negative floor flows from
        // here into insertWindowIntoColumnAt and on to Tile::minWidth/
        // minHeight, and the relayout slack math is not written for one.
        it->minWidth = qMax(0, minWidth);
        it->minHeight = qMax(0, minHeight);
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    // qMax(0, ...): same negative-floor contract as the FloatRestore write
    // above and insertOpenedWindow's boundary clamp — a negative floor flows
    // from here into Tile::minWidth/minHeight, and the relayout slack math
    // is not written for one. (No live crash today; every consumer happens
    // to guard, but this is exported LGPL API and the sibling paths all
    // clamp at the boundary.)
    const int clampedMinWidth = qMax(0, minWidth);
    const int clampedMinHeight = qMax(0, minHeight);
    const bool minChanged = state->strip().setWindowMinimumSize(windowId, clampedMinWidth, clampedMinHeight);
    // Re-run insertOpenedWindow's oversized verdict on the updated clamp. The
    // open path floats a window whose minimum no column slot can honour, but
    // clients that pin their size hints AFTER mapping (a Wine game maps
    // hintless, then pins min to its configured resolution) used to dodge
    // that verdict on timing alone and stay tiled at a size the strip can
    // never satisfy — exactly the stranded state the refused-ack latch in
    // onWindowResized documents waiting on "something else" to heal.
    // Evaluated whatever minChanged says: an adoption path can seat a tiled
    // window with an already-oversized clamp, and the effect's idempotent
    // re-report is then the only revisit this state ever gets.
    // floatWindowInternal owns the whole transition (slot memory for the
    // eventual unfloat, float-focus seeding, context-guarded apply,
    // placementChanged), and the strip write above means the FloatRestore it
    // captures carries the new clamp. The reverse transition is deliberately
    // absent — a min that shrinks back below the work area does not
    // auto-unfloat, because the float may have been rearranged meanwhile and
    // the manual unfloat path already restores the remembered slot.
    if (state->strip().containsWindow(windowId)) {
        const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
        if (params.workArea.isValid()
            && (clampedMinWidth > params.workArea.width() || clampedMinHeight > params.workArea.height())) {
            qCInfo(lcScrollEngine) << "windowMinSizeUpdated:" << windowId << "min" << clampedMinWidth << "x"
                                   << clampedMinHeight << "outgrew work area" << params.workArea.size() << "on"
                                   << key.screenId << "— floating (open-time oversized policy)";
            floatWindowInternal(state, key, windowId, key.screenId);
            return;
        }
    }
    // Background-context guard, the same one windowClosed and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // min-size report for a window on another desktop would relayout a strip
    // this change did not touch. The model write still lands; the switch back
    // retiles the mutated strip.
    if (minChanged && key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                   const QString& screenId)
{
    Q_UNUSED(oldFrame)
    // key.screenId is authoritative below; a mismatched caller value would
    // retile the wrong strip.
    Q_UNUSED(screenId)
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A window under a compositor interactive move: its frames are drag
    // motion, not a size the user settled on. Reconciling them pinned the
    // column's width/height intents to transient drag rects, and the
    // refused-ack arm below re-emitted the slot rect against the move —
    // the ~1 Hz mid-drag teleport fight. The daemon clears the mark before
    // the drop settles, and the drop paths re-apply authoritative geometry.
    if (!m_interactiveDragWindow.isEmpty() && windowId == m_interactiveDragWindow) {
        return;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    // Background-context guard, as windowMinSizeUpdated and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // resize of a window on another desktop must not drive one. The model
    // reconcile still happens — it is the persisted intent — and the switch
    // back retiles.
    const bool currentContext = key == currentKeyForScreen(key.screenId);
    // Reconcile the column to the size the client/user actually settled on;
    // only the owning column relayouts (a resize never reflows neighbours'
    // widths — they just shift). Width intent is only rewritten when the
    // WIDTH moved relative to the last applied rect — a vertical-only
    // resize must not pin a Proportion/Preset column to pixels.
    //
    // With NO last-applied rect there is no baseline to compare against, and
    // treating that as "both changed" pinned BOTH intents to pixels — so a
    // purely vertical resize arriving in the window between an adoption
    // (handoffReceive, the setWindowFloat adoption branch, floatWindowInternal)
    // and its scheduled applyLayout converted a Proportion column to Fixed,
    // which is exactly what the widthChanged gate exists to prevent. Reconcile
    // nothing in that case and let the pending relayout establish the baseline.
    const QRect lastApplied = m_lastAppliedRect.value(windowId);
    if (!lastApplied.isValid()) {
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    // Derived in ROLE terms against the same params the reconcile decodes the
    // acked size with. Comparing physical width/height here while the
    // reconcile reads main/cross would make each guard protect the intent it
    // was not written for.
    const ScrollLayoutParams resizeParams = layoutParamsForScreen(key.screenId);
    const StripAxis resizeAxis = resizeParams.axis;
    const bool mainChanged = resizeAxis.mainSize(lastApplied) != resizeAxis.mainSize(newFrame);
    const bool crossChanged = resizeAxis.crossSize(lastApplied) != resizeAxis.crossSize(newFrame);
    if (state->strip().reconcileWindowSize(windowId, newFrame.size(), mainChanged, crossChanged, resizeParams)) {
        // The reconcile WROTE persisted intent (the column's Fixed width, the
        // tile's Fixed height — both serialized by serializeStripState), and
        // placementChanged is the sole producer of DirtyScrollStrips. Without
        // this emit a resize that is the session's last strip interaction is
        // never saved and the column comes back at its old width.
        // reconcileWindowSize returns true only on a genuine change, so
        // emit-on-change holds.
        Q_EMIT placementChanged(key.screenId);
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    // The strip REFUSED the size (no-op ack): the window
    // is now displaced from the engine's rect, but m_lastAppliedRect still
    // holds it, so the emit-on-change gate would treat the corrective
    // relayout as "nothing moved" and never re-issue the rect. Drop the
    // memory and retile so the authoritative geometry is re-applied.
    //
    // EXCEPT when the displacement is the window's own minimum floor: a
    // client pinned at a min the slot cannot honour (KWin clamps every
    // commit to it) re-asserts the same oversized frame after every
    // corrective re-apply, and the reconcile above keeps refusing because
    // the Fixed intent already records that size — so the retile can never
    // converge and the pair loops at the client's re-assert rate. The known
    // floor is not a displacement to correct, so the SELF-DRIVING retile is
    // skipped — but the gate memory is still dropped: the frame genuinely
    // differs from the engine's rect, and keeping the memory would make the
    // eventual healing relayout (a later min-size drop, a focus move, any
    // scheduled retile) read as "nothing moved" and stay silent, stranding
    // the window oversized forever. With the memory gone each such relayout
    // re-emits once; the client re-asserts once; no retile is scheduled
    // from here, so the pair advances only when something else drives a
    // relayout instead of ping-ponging on its own.
    if (lastApplied != newFrame) {
        const QSize knownMin = state->strip().windowMinimumSize(windowId);
        const bool pinnedAtMinW =
            knownMin.width() > 0 && newFrame.width() == knownMin.width() && newFrame.width() > lastApplied.width();
        const bool pinnedAtMinH =
            knownMin.height() > 0 && newFrame.height() == knownMin.height() && newFrame.height() > lastApplied.height();
        const bool widthExplained = lastApplied.width() == newFrame.width() || pinnedAtMinW;
        const bool heightExplained = lastApplied.height() == newFrame.height() || pinnedAtMinH;
        m_lastAppliedRect.remove(windowId);
        if (widthExplained && heightExplained && lastApplied.topLeft() == newFrame.topLeft()) {
            return;
        }
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
    }
}

} // namespace PhosphorScrollEngine
