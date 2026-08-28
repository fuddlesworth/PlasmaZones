// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Float management for the scrolling engine: the internal float/unfloat pair
// that owns the strip-slot memory, and the two public verbs on top of them.
// Split out of engine_lifecycle.cpp, which crossed the file-size ceiling when
// drag re-insert landed. The boundary is the one the file already drew for
// itself with a section banner, so nothing moved that the rest of the
// lifecycle code reads directly.
//
// The slot memory is the load-bearing part: a floated window leaves the strip
// and its column closes up, so unfloat/unminimize needs the remembered column,
// tile index, stack anchor and width/display/height intents to put it back
// where it was rather than opening a fresh column at the end.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

#include <algorithm>
#include <utility>

namespace PhosphorScrollEngine {

// ── Float management ────────────────────────────────────────────────────────

bool ScrollEngine::floatWindowInternal(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                       const QString& windowId, const QString& screenId)
{
    if (state->isFloating(windowId)) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const int columnIdx = state->strip().columnOfWindow(windowId);
    if (columnIdx < 0) {
        // Tracked in the reverse map yet in neither the strip nor the
        // floating set — bookkeeping residue (e.g. a drag-insert preview
        // torn down without restoration). Refusing here made the state
        // permanent: nothing else ever revisits it, so the window could
        // never float OR re-enter the strip again. Heal by adopting it as
        // floating with a slotless restore entry (column -1 = unfloat opens
        // a fresh column), the same shape seedFloatRestoreForOpen writes.
        qCWarning(lcScrollEngine) << "floatWindowInternal:" << windowId
                                  << "tracked but absent from strip and floating set on" << key.screenId
                                  << "— healing by adopting as floating";
        state->addFloating(windowId);
        if (!m_floatRestore.contains(windowId)) {
            m_floatRestore.insert(windowId, FloatRestore{});
        }
        m_scrollFloatedWindows.insert(windowId);
        // Drop the same three per-window memories the main float path below
        // drops: a retained rect that happens to equal the one the strip
        // later resolves defeats applyLayout's emit-on-change gate
        // (reachable — the drag-preview heal that manufactures this residue
        // clears neither memory), a stale park edge would anchor the
        // arrival animation to the wrong side, and the windowed-fs memory
        // goes as a symmetry belt (a stale entry there is a set-vs-bool
        // compare that can only force one redundant emit, never suppress).
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        Q_EMIT windowFloatingChanged(windowId, true, screenId.isEmpty() ? key.screenId : screenId);
        Q_EMIT placementChanged(key.screenId);
        return true;
    }
    FloatRestore restore;
    restore.column = columnIdx;
    const Column& sourceColumn = state->strip().columns().at(columnIdx);
    restore.width = sourceColumn.width;
    restore.display = sourceColumn.display;
    restore.ownedTabbedHeight = sourceColumn.display == ColumnDisplay::Tabbed && sourceColumn.heightOwnerId == windowId;
    const QSize minSize = state->strip().windowMinimumSize(windowId);
    restore.minWidth = minSize.width();
    restore.minHeight = minSize.height();
    // The height intent dies with the tile too, and the minimize path rides
    // this same round trip — without it a minimize/restore silently reset a
    // user-set window height to Auto.
    const int tileIdx = sourceColumn.indexOfWindow(windowId);
    if (tileIdx >= 0) {
        restore.height = sourceColumn.tiles.at(tileIdx).height;
    }
    if (sourceColumn.tiles.size() > 1) {
        restore.tileIndex = tileIdx;
        // Anchor on a surviving sibling so the stack can be re-located even
        // after column indices shift (prefer the neighbour above, else below).
        restore.stackAnchor = sourceColumn.anchorSiblingFor(restore.tileIndex);
    }
    // The strip's active tile is the engine's focus proxy: pulling it into
    // the float layer moves focus THERE without any compositor round trip
    // (the window keeps focus; no report will arrive to record the side
    // change). A non-active float (rules, batch operations) leaves the
    // focus-side memory alone.
    const bool wasActiveTile = state->strip().activeWindowId() == windowId;
    state->strip().takeWindow(windowId, params);
    state->addFloating(windowId);
    if (wasActiveTile) {
        state->setLastFloatingFocus(windowId);
        state->setFloatingHasFocus(true);
    }
    m_floatRestore.insert(windowId, restore);
    m_scrollFloatedWindows.insert(windowId);
    m_lastAppliedRect.remove(windowId);
    // A float leaves the strip, so a remembered park edge is orphaned: the
    // aliveness prune never reclaims it (the window stays alive), and the
    // stale entry would anchor the arrival animation to the wrong side when
    // the window later unfloats back into partial view.
    m_parkedScrollEdge.remove(windowId);
    // Windowed fullscreen dies with the tile (FloatRestore deliberately does
    // not carry it — float and windowed fullscreen are exclusive), so the
    // emit-gate memory goes with it.
    m_lastAppliedWindowedFs.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, true, screenId.isEmpty() ? key.screenId : screenId);
    // Background-context guard: see windowClosed.
    if (key == currentKeyForScreen(key.screenId)) {
        applyLayout(key.screenId, false);
    }
    Q_EMIT placementChanged(key.screenId);
    return true;
}

bool ScrollEngine::unfloatWindowInternal(ScrollState* state, const QString& windowId, const QString& screenId,
                                         bool applyAfter)
{
    // Captured before any mutation so the heal arm below and the guard at
    // the tail both read the context the window actually belongs to.
    const PhosphorEngine::PlacementStateKey key = m_states.keyForWindow(windowId);
    // The window's OWN context screen, the way floatWindowInternal reads
    // key.screenId throughout: a caller that passes the operation screen
    // instead would resolve the params and the background guard against a
    // strip this window does not live on. The caller's value is the fallback
    // only for a window the reverse map has no key for at all.
    const QString contextScreen = key.screenId.isEmpty() ? screenId : key.screenId;

    // Mirror of floatWindowInternal's focus-side capture: re-tiling the float
    // that holds focus moves focus back to the strip with no compositor
    // report (the window keeps focus, only its layer changes). Read before
    // removeFloating, which clears BOTH focus-memory fields when this window
    // held them — the capture is what lets the insert-refused restore arm
    // put the pair back.
    const bool wasFloatFocus = state->floatingHasFocus() && state->lastFloatingFocus() == windowId;

    if (!state->removeFloating(windowId)) {
        // This engine holds no float for the window — but the SHARED float
        // set can still flag it while the strip holds it as a TILE. Two ways
        // in: another engine floated it (snap's no-zone-match default, which
        // is correct on ITS screen) and the window later joined this strip,
        // or a restore wrote the strip record and the snap float record for
        // the same window. Returning silently latched that contradiction
        // FOREVER — every unfloat route refuses, and a window the shared set
        // calls floating is never adopted into a strip again, so the user
        // has no way back short of closing the window.
        //
        // Announce the engine's real view instead. The passive sync clears
        // the shared flag AND releases the sibling engine's stale tracking,
        // and deliberately restores no geometry: the window is already
        // placed as a tile, so there is nothing to move.
        if (state->strip().containsWindow(windowId)) {
            qCInfo(lcScrollEngine) << "unfloatWindowInternal:" << windowId
                                   << "is a strip tile the shared float set still flags — syncing it clear";
            m_scrollFloatedWindows.remove(windowId);
            Q_EMIT windowFloatingStateSynced(windowId, false, contextScreen);
        }
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(contextScreen);
    // Restore the remembered column slot (minimize/unminimize and float
    // round-trips keep their place); fall back to next-to-focus.
    const bool hadSlot = m_floatRestore.contains(windowId);
    const FloatRestore restore = m_floatRestore.take(windowId);
    bool inserted = false;
    if (hadSlot && restore.tileIndex >= 0) {
        // The window left a SHARED column: return to the surviving stack.
        // The stack is re-located through the surviving-sibling anchor —
        // the remembered index goes stale when columns close during the
        // float and would splice into a stranger's stack. Anchor gone →
        // fall through to a fresh column.
        const int anchoredColumn =
            restore.stackAnchor.isEmpty() ? -1 : state->strip().columnOfWindow(restore.stackAnchor);
        if (anchoredColumn >= 0) {
            inserted = state->strip().insertWindowIntoColumnAt(anchoredColumn, restore.tileIndex, windowId, params,
                                                               restore.minWidth, restore.minHeight);
        }
    }
    if (!inserted && hadSlot && restore.column >= 0) {
        // column >= 0 keeps a SEEDED slot (seedFloatRestoreForOpen writes
        // column = -1 for a window floated at open, which never held a strip
        // position) out of this arm — insertWindowAt would qBound -1 to the
        // leftmost column and stamp the record's default width/display over
        // the configured defaults. A slotless seed falls through to the plain
        // next-to-focus insert below.
        inserted = state->strip().insertWindowAt(restore.column, windowId, restore.width, restore.display, params);
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, params.defaultColumnWidth,
                                               effectiveDefaultColumnDisplay(contextScreen), params);
    }
    if (!inserted) {
        // Every insert refused (an empty id is the only way today). The float
        // set was already given up above, so returning now would leave the
        // window tracked but in NEITHER the strip nor the floating set — the
        // inconsistency floatWindowInternal warns about. Put it back.
        qCWarning(lcScrollEngine) << "unfloatWindowInternal: every insert refused for" << windowId
                                  << "— restoring floating state";
        state->addFloating(windowId);
        if (hadSlot) {
            m_floatRestore.insert(windowId, restore);
        }
        // removeFloating above cleared the focus-memory pair when this window
        // held it; the window is a float again, so put both halves back.
        if (wasFloatFocus) {
            state->setLastFloatingFocus(windowId);
            state->setFloatingHasFocus(true);
        }
        return false;
    }
    {
        // `inserted` is necessarily true here — the !inserted arm above
        // returns — so no second guard.
        //
        // Re-apply the min size the floated tile carried (the fresh-column
        // branches insert without it).
        if (restore.minWidth > 0 || restore.minHeight > 0) {
            state->strip().setWindowMinimumSize(windowId, restore.minWidth, restore.minHeight);
        }
        // Same for the height intent, but only from an entry that captured a
        // real tile: a SEEDED entry (seedFloatRestoreForOpen, column = -1)
        // carries a default-constructed Auto height that never belonged to a
        // tile, and stamping it here overwrote the context default height the
        // fresh insert just seeded — so a window floated at open and then
        // unfloated came back at Auto instead of the configured default.
        if (restore.column >= 0 || restore.tileIndex >= 0) {
            state->strip().setWindowHeightIntent(windowId, restore.height);
            // And the extent ownership, when this window held it. Not implied
            // by the height write: that claims nothing for an Auto intent, and
            // a tab can legitimately own the column while asking for the whole
            // work area. Without this the column keeps the height of whichever
            // tab came on show when this one floated out.
            if (restore.ownedTabbedHeight) {
                state->strip().setTabbedHeightOwner(windowId);
            }
        } else {
            // A seeded entry has no remembered height, so the tile carries the
            // context default the fresh insert seeded. Under "the client
            // decides" that default is the concrete Auto fallback the kind
            // leaves behind, and the commit that gives the window its own
            // height lives on the open path, which an unfloat does not run —
            // so a window floated at open and then unfloated came back sharing
            // its column, the one shape the setting exists to avoid.
            // Post-insert params, for the reason the open path's own call
            // documents: with smart gaps the outer gaps switch off at exactly
            // one column, so an unfloat that takes the strip 0->1 or 1->2
            // changes the work area the bound is measured against, and these
            // are persisted pixels that do not self-heal.
            commitClientDecidedHeight(state->strip(), windowId, contextScreen, overridesForScreen(contextScreen),
                                      layoutParamsForScreen(contextScreen, state->strip().columnCount()));
        }
        state->strip().focusWindow(windowId, params);
        // No setFloatingHasFocus(false) here: removeFloating already cleared
        // the pair when this window held it (the wasFloatFocus capture above
        // exists for the refusal arm's restore, not for a second clear).
    }
    m_scrollFloatedWindows.remove(windowId);
    // contextScreen, not the caller's raw screenId — the same fallback form
    // floatWindowInternal uses, so an empty caller hint cannot mislabel the
    // announcement's screen (both current callers pass a matching screen;
    // this pins the contract).
    Q_EMIT windowFloatingChanged(windowId, false, contextScreen);
    // Batch callers (snapAllWindows) relayout once for the whole batch.
    if (applyAfter) {
        // Background-context guard, same terms as floatWindowInternal:
        // applyLayout resolves the screen's CURRENT context, so an unfloat on
        // another desktop's state would relayout the wrong strip. The
        // placement announcement still fires — the managed set did change.
        if (key == currentKeyForScreen(contextScreen)) {
            applyLayout(contextScreen, false);
        }
        Q_EMIT placementChanged(contextScreen);
    }
    return inserted;
}

void ScrollEngine::setWindowFloat(const QString& rawWindowId, bool shouldFloat, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);

    if (!state) {
        if (shouldFloat) {
            return; // never tracked — nothing to pull out of a strip
        }
        // Unfloat a window the engine has not adopted yet (e.g. floated
        // before the screen switched to scrolling): adopt into the
        // authoritative screen's current strip.
        const QString targetScreen = resolveOperationScreen(screenId);
        if (targetScreen.isEmpty()) {
            return;
        }
        // Look up WITHOUT creating first: the two bail-outs below must not
        // leave a freshly created empty state behind — an empty state at a
        // key makes desktopsWithActiveState report the desktop and keeps the
        // stateless-bookkeeping sweep from reclaiming the screen.
        ScrollState* target = stateForKey(currentKeyForScreen(targetScreen), false);
        if (target && target->containsWindow(windowId)) {
            return;
        }
        if (!target) {
            target = stateForKey(currentKeyForScreen(targetScreen), true);
            if (!target) {
                return;
            }
        }
        const ScrollLayoutParams params = layoutParamsForScreen(targetScreen);
        // Same as handoffReceive: the retained close/release rect only has to
        // outlive the capture window, and carrying it into a re-adoption would
        // gate away the first windowsTiled batch for this window. Parked-edge
        // and windowed-fullscreen memories are dropped for the same
        // re-adoption reason (the fs drop is a symmetry belt — a stale entry
        // there could only force one redundant emit, never suppress one).
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        // Whatever clamp is on record for the window while it floats — the
        // seed a float-at-open left, or a live windowMinSizeUpdated
        // write-through. This route inserts without min sizes, so without the
        // re-apply the adopted tile relayouts unclamped until the client
        // happens to re-report, which for a fixed-size window is never.
        const FloatRestore adopted = m_floatRestore.value(windowId);
        if (target->strip().insertWindow(windowId, params.defaultColumnWidth,
                                         effectiveDefaultColumnDisplay(targetScreen), params, adopted.minWidth,
                                         adopted.minHeight)) {
            // The tile owns the clamp from here; a refused insert keeps the
            // entry so a later attempt still has it.
            m_floatRestore.remove(windowId);
            // Third unfloat route: clear the mode-transition float marker
            // like unfloatWindowInternal/handoffRelease do.
            m_scrollFloatedWindows.remove(windowId);
            m_states.setKeyForWindow(windowId, currentKeyForScreen(targetScreen));
            Q_EMIT windowFloatingStateSynced(windowId, false, targetScreen);
            applyLayout(targetScreen, false);
            Q_EMIT placementChanged(targetScreen);
        }
        return;
    }

    if (shouldFloat) {
        floatWindowInternal(state, key, windowId, screenId);
    } else {
        unfloatWindowInternal(state, windowId, key.screenId);
    }
}

void ScrollEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
{
    // setWindowFloat canonicalizes its own input; canonicalizeForLookup is
    // idempotent, so passing the resolved id through is a single-pass
    // pipeline, not a second translation.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    ScrollState* state = stateForWindow(windowId);
    if (!state) {
        // Report instead of absorbing the press silently: a toggle aimed at
        // an untracked window would resolve floating=false below and
        // setWindowFloat's no-state arm silently rejects the float. Every
        // other navigation shortcut produces feedback, and a silent
        // shortcut reads as broken — mirrors SnapEngine::toggleWindowFloat
        // and the autotile facade's not_managed report. Direct callers only:
        // toggleFocusedFloatAs pre-empts this arm with its own PER-VERB
        // token (a "Restore" press must not render the Float failure copy),
        // so this "float" token is never seen through that route. Screen
        // resolved like every sibling failure emit — the raw hint can be
        // empty and the OSD would land on the cursor/primary fallback.
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("not_managed"), windowId, QString(),
                                  resolveOperationScreen(screenId));
        return;
    }
    const bool floating = state->isFloating(windowId);
    setWindowFloat(windowId, !floating, screenId);
}

} // namespace PhosphorScrollEngine
