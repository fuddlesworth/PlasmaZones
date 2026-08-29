// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

#include <algorithm>

#include <QMetaObject>

namespace PhosphorScrollEngine {

namespace {

/// Upper bound on the edge auto-scroll speed the engine will honour. Taken
/// from the interface rather than spelled again here, so the config schema's
/// matching maximum has a single thing to be pinned against.
constexpr int kDragScrollMaxSpeedCeiling = PhosphorEngine::IScrollSettings::kDragScrollMaxSpeedCeiling;

} // namespace

ScrollEngine::ScrollEngine(PhosphorEngine::IWindowTrackingService* windowTracker,
                           PhosphorScreens::ScreenManager* screenManager, QObject* parent)
    : PhosphorEngine::PlacementEngineBase(parent)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    // Seeded here rather than in-class: kFuzzyClaimGraceMs lives in the
    // engine-internal enginelimits.h, which the exported header cannot
    // include, and mirroring the literal there would be exactly the drift the
    // shared-constant convention exists to prevent.
    , m_fuzzyClaimGraceMs(kFuzzyClaimGraceMs)
{
}

ScrollEngine::~ScrollEngine() = default;

void ScrollEngine::setWindowRegistry(QObject* registry)
{
    m_windowRegistry = qobject_cast<PhosphorEngine::WindowRegistry*>(registry);
}

QString ScrollEngine::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    return rawWindowId;
}

QString ScrollEngine::currentAppIdFor(const QString& anyWindowId) const
{
    if (anyWindowId.isEmpty()) {
        return QString();
    }
    if (m_windowRegistry) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
        const QString fromRegistry = m_windowRegistry->appIdFor(instanceId);
        if (!fromRegistry.isEmpty()) {
            return fromRegistry;
        }
    }
    // Fallback: parse the string. Note this returns the FIRST-seen class for
    // canonical ids; accurate only when the window has never renamed.
    return PhosphorIdentity::WindowId::extractAppId(anyWindowId);
}

// ── Screen ownership ────────────────────────────────────────────────────────

bool ScrollEngine::isActiveOnScreen(const QString& screenId) const
{
    return m_scrollingScreens.contains(screenId);
}

bool ScrollEngine::isEnabled() const noexcept
{
    return !m_scrollingScreens.isEmpty();
}

void ScrollEngine::setActiveScreens(const QSet<QString>& screens)
{
    // Consume the context-switch flag on EVERY entry (both branches), the
    // same discipline as setAutotileScreens: a stale flag would make the
    // NEXT identical-set push claim a desktop switch. The flag matters
    // because TilingAdaptor OR-coalesces isDesktopSwitch across engines in
    // one pass — a false `true` from scroll's no-op re-push would make the
    // effect skip AUTOTILE's geometry/border restore for a screen leaving
    // that engine in the same recompute.
    const bool wasDesktopSwitch = m_isDesktopContextSwitch;
    m_isDesktopContextSwitch = false;
    if (screens == m_scrollingScreens) {
        // Identical-set re-emit contract: a desktop/activity switch that
        // lands on the same set still wakes the compositor effect's
        // catch-scan; an empty identical set has nothing to catch, and a
        // NON-switch re-push (updateEngineScreens re-derive) must not
        // masquerade as one. The retile loop is unconditional — the
        // daemon's per-pass override push depends on it (scrolling.cpp's
        // LOAD-BEARING gate).
        if (!screens.isEmpty()) {
            if (wasDesktopSwitch) {
                QStringList sortedSame(screens.cbegin(), screens.cend());
                sortedSame.sort();
                Q_EMIT scrollingScreensChanged(sortedSame, true);
            }
            for (const QString& screenId : screens) {
                scheduleRetileForScreen(screenId);
            }
        }
        return;
    }

    const bool wasEnabled = isEnabled();
    const QSet<QString> removed = m_scrollingScreens - screens;
    const QSet<QString> added = screens - m_scrollingScreens;
    // A live drag-insert preview whose target or restore-source screen is
    // leaving the set must be unwound BEFORE the state teardown below, while
    // both states still exist (autotile's setAutotileScreens cancels for the
    // same reason).
    if (m_dragInsertPreview
        && (removed.contains(m_dragInsertPreview->targetScreenId)
            || (m_dragInsertPreview->hadPriorState && removed.contains(m_dragInsertPreview->priorKey.screenId)))) {
        cancelDragInsertPreview();
    }
    m_scrollingScreens = screens;

    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    for (const QString& screenId : removed) {
        // Prune ONLY the leaving screen's CURRENT (desktop, activity)
        // context; its windows are released to whichever engine now owns
        // the screen. Autotile parity ("desktop switching is a fast state
        // swap"): with per-context modes a screen leaves this set on EVERY
        // switch to a non-scrolling desktop, and the daemon pushes the new
        // desktop BEFORE re-deriving the sets — so on a plain switch the
        // current key resolves to the NEW desktop, no scroll state matches,
        // and the other desktops' strips (consumed stacks, widths, tabbed
        // flags, their windows' tracking) survive intact for the switch
        // back. Only a genuine mode reassignment of the current context
        // matches and tears down. Sibling contexts are reaped by
        // pruneStatesForDesktop / pruneStatesForActivities /
        // pruneStatesForRemovedScreen when their context or output dies.
        const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);
        // Resolved BEFORE the prune walk: the stash's never-relaid fallback
        // otherwise called layoutParamsForScreen — and through it the
        // injected geometry/gap providers — from inside removeStatesIf's
        // onRemove, mid-iteration over the state map. The in-tree providers
        // are pure, but the provider seam makes no such promise to an
        // embedder, so nothing injected may run inside the walk.
        const PhosphorProtocol::ScrollAxis fallbackAxis = stripAxisForScreen(screenId).axis();
        m_states.removeStatesIf(
            [&currentKey](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
                return key == currentKey;
            },
            [this, &releasedWindows, &releasedScreens, &screenId,
             fallbackAxis](const PhosphorEngine::PlacementStateKey& key, ScrollState* state) {
                // Mode reassignment: remember the strip's structure so a
                // cycle back to Scrolling rebuilds it (stacks, widths,
                // tabbed flags) instead of a default one-window-per-column
                // strip. Captured BEFORE the release strips the state.
                stashStripStructure(key, state, fallbackAxis);
                releaseScreenState(state, releasedWindows);
                // Inside the callback so the payload names only screens that
                // had a MATCHING STATE — the daemon's release handler uses
                // it as a skip filter, and a leaving screen that never built
                // a state widening it would let an unrelated window through
                // (the sibling prune's payload keeps the same contract).
                releasedScreens.insert(screenId);
            });
        // Ownership only — the per-output desktop STAYS. This screen is
        // leaving scrolling, not disappearing, and its desktop is compositor
        // truth this engine cannot re-derive (see releaseScreenOwnership).
        m_context.releaseScreenOwnership(screenId);
        // Even a STATELESS leaving screen (seed pushed before any window
        // arrived) must drop its per-screen bookkeeping — the state-driven
        // sweep in releaseScreenState never ran for it. The tab-strip clear
        // is latched, so a second call is a no-op. m_perScreenOverrides is
        // NOT swept here: the daemon clears a departing screen's overrides
        // itself, right after setActiveScreens in updateScrollingScreens.
        // pruneStatesForRemovedScreen is the output-removal purge.
        m_pendingInitialOrder.remove(screenId);
        m_consumedInitialOrder.remove(screenId);
        // The focus seed is PASS-SCOPED, exactly as the daemon declares it:
        // a focus is only true for the transition that captured it. Its only
        // consumer is the arrival burst, and a screen that leaves without
        // bursting never reaches one — so the drop has to happen at the
        // lifecycle edges like its order twin, or the seed sits armed and
        // re-anchors a view the user has since moved, several transitions later.
        m_pendingInitialFocus.remove(screenId);
        clearTabStripsForScreen(screenId);
    }
    if (!releasedWindows.isEmpty()) {
        const QSet<QString> releasedSet(releasedWindows.cbegin(), releasedWindows.cend());
        m_states.removeWindowsIf([&releasedSet](const QString& windowId, const PhosphorEngine::PlacementStateKey&) {
            return releasedSet.contains(windowId);
        });
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }

    // A screen this engine no longer manages must not keep feeding the
    // hint-less shortcut paths, the same clear pruneStatesForRemovedScreen
    // makes for a departed output. resolveOperationScreen re-checks
    // membership, so this is belt-and-braces, but it keeps the one writer of
    // the field honest about what it means.
    if (!m_activeScreen.isEmpty() && !m_scrollingScreens.contains(m_activeScreen)) {
        m_activeScreen.clear();
    }

    for (const QString& screenId : added) {
        // A screen (re-)entering scrolling is the other moment a re-announce
        // wave legitimately follows (mode round trip, including a same-app
        // window whose uuid regenerated while the screen sat in another
        // mode). Re-open the fuzzy-claim grace on every stash entry this
        // screen holds, in any (desktop, activity) context — the arrival wave
        // lands on whichever context is current when the windows announce.
        for (auto it = m_stripStash.begin(); it != m_stripStash.end(); ++it) {
            if (it.key().screenId == screenId) {
                it->fuzzyClaimWindow.start();
            }
        }
        scheduleRetileForScreen(screenId);
    }

    // Sorted: QSet iteration order is unspecified across runs, and a wire
    // consumer comparing successive payloads must not see phantom changes.
    QStringList sorted(screens.cbegin(), screens.cend());
    sorted.sort();
    // Propagate the consumed context-switch flag (autotile parity): a
    // desktop switch whose per-desktop assignments ALSO change the set must
    // still report isDesktopSwitch=true, or the effect runs its destructive
    // geometry/border restore for the departing screens.
    Q_EMIT scrollingScreensChanged(sorted, wasDesktopSwitch);
    if (wasEnabled != isEnabled()) {
        Q_EMIT enabledChanged(isEnabled());
    }
}

void ScrollEngine::setActiveScreenHint(const QString& screenId)
{
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
}

void ScrollEngine::releaseScreenState(ScrollState* state, QStringList& releasedWindows, bool clearScreenBookkeeping)
{
    const QString screenId = state->screenId();
    const QStringList windows = state->managedWindows();
    // Snapshot each window's scrolling slot into the unified record BEFORE the
    // state is torn down — the record is the single source of truth for
    // cross-mode state, and stashStripStructure covers only the TILED
    // structure, so without this a window floated in scrolling loses its
    // floating slot across a mode round trip and comes back tiled with a stale
    // slot.order. AutotileEngine::releaseScreenStateForTeardown does the same.
    if (m_windowTracker) {
        for (const QString& windowId : windows) {
            if (auto record = capturePlacement(windowId)) {
                m_windowTracker->placementStore().record(*record);
            }
        }
    }
    // Only the unfloat-slot memory dies here. The float markers and the
    // last-applied rects are inputs to the daemon's windowsReleased handler,
    // which has not run yet — see the contract on the declaration.
    // The pending self-activation entries and the declined-open marks go
    // too, for windowClosed's reason: a released window's echo can never be
    // answered while the screen sits in another mode, and a stale entry (or
    // mark) would eat the first genuine focus report when the window comes
    // back to scrolling. The parked-edge and windowed-fullscreen memories go
    // for the eviction symmetry every other exit path holds: neither is an
    // input to windowsReleased, windowClosed cannot sweep them later
    // (stateForWindow answers null after this), and pruneStaleWindows only
    // runs once per session at bring-up.
    for (const QString& windowId : windows) {
        m_floatRestore.remove(windowId);
        m_pendingSelfActivations.removeAll(windowId);
        m_pendingSelfActivationQueuedAt.remove(windowId);
        m_declinedOpenFocus.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedColumnMaximized.remove(windowId);
    }
    releasedWindows.append(windows);
    if (!clearScreenBookkeeping) {
        // A sibling context of this screen is still live and owns these maps
        // (see the declaration): the caller sweeps them through
        // sweepStatelessScreenBookkeeping once the screen has no state left.
        state->deleteLater();
        return;
    }
    // Per-screen bookkeeping dies with the state: a stale seed must not
    // replay on re-entry, and the tab-strip overlay must be told to clear —
    // no relayout will ever run for a departed screen to do it.
    m_pendingInitialOrder.remove(screenId);
    m_consumedInitialOrder.remove(screenId);
    // Same reasoning for the focus seed: it is scoped to the transition that
    // captured it, so a state teardown ends its validity whether or not any
    // burst consumed it.
    m_pendingInitialFocus.remove(screenId);
    // Latch and payload cleared inline (plain containers, safe), but the
    // broadcast is DEFERRED: this function runs from inside
    // PerScreenStates::removeStatesIf's iteration over m_states, and a
    // consumer slot that touched the engine's state map synchronously would
    // invalidate the live iterator. All eight clearTabStripsForScreen call
    // sites are outside the state map's own iteration and emit directly.
    m_lastTabStripPayload.remove(screenId);
    if (m_screensWithTabStrips.remove(screenId)) {
        QMetaObject::invokeMethod(
            this,
            [this, screenId]() {
                // Re-checked at DELIVERY time. If the screen re-acquired a
                // strip between the inline latch clear and this callback (it
                // left scrolling and came back in the same daemon pass, then
                // any synchronous applyLayout ran ahead of the event queue),
                // applyLayout has already re-emitted the live payload and
                // re-set the latch. Firing the stale "[]" last would then be
                // final: m_lastTabStripPayload still holds the live payload,
                // so applyLayout's emit-on-change gate suppresses every
                // re-emit and the tab-strip indicator stays missing until the
                // payload genuinely changes.
                if (!m_screensWithTabStrips.contains(screenId)) {
                    Q_EMIT tabStripsChanged(screenId, QStringLiteral("[]"));
                }
            },
            Qt::QueuedConnection);
    }
    state->deleteLater();
}

StashedStrip
ScrollEngine::buildStashFromState(const ScrollState* state,
                                  std::optional<PhosphorProtocol::ScrollAxis> preResolvedFallbackAxis) const
{
    StashedStrip out;
    if (!state) {
        return out;
    }
    // Spent-ness and the blueprint it counts against are captured BEFORE the
    // no-columns exit, not beside the focus and view below. A strip whose
    // windows have all been floated or minimized away has no columns while its
    // cursor is still live, and taking this after the exit handed such a state
    // back a default-constructed entry — cursor 0, the exact under-count the
    // carry exists to stop. See StashedStrip::blueprintCursor.
    out.blueprintCursor = state->blueprintCursor();
    out.blueprintIdentity = state->blueprintIdentity();
    if (state->strip().isEmpty()) {
        return out;
    }
    for (const Column& col : state->strip().columns()) {
        if (col.tiles.isEmpty()) {
            continue;
        }
        StashedColumn sc;
        sc.width = col.width;
        sc.display = col.display;
        // Rides with the display it belongs to; see StashedColumn.
        sc.heightOwnerId = col.heightOwnerId;
        // Clamped, not value(): an out-of-range activeTileIdx would record an
        // EMPTY active id and the restore's tab re-assertion would silently
        // no-op. Every mutation site clamps today, so this is the belt — but
        // a silent no-op is the wrong failure for the one that does not.
        sc.activeWindowId = col.tiles.at(qBound(0, col.activeTileIdx, col.tiles.size() - 1)).windowId;
        for (const Tile& tile : col.tiles) {
            // Named member assignment, not positional brace-init:
            // StashedTile's next members are lease state that must stay
            // defaulted here, and a future field inserted before
            // windowedFullscreen would mis-bind two same-typed bools
            // silently under a positional init.
            StashedTile st;
            st.windowId = tile.windowId;
            st.height = tile.height;
            st.minimized = tile.minimized;
            st.windowedFullscreen = tile.windowedFullscreen;
            sc.tiles.append(st);
        }
        out.columns.append(sc);
    }
    // Focus and view travel with the structure: without them every round
    // trip re-anchored the strip on whichever window arrived first.
    out.focusedWindowId = state->strip().activeWindowId();
    out.viewAnchor = state->strip().viewAnchor();
    // Both halves or neither: an anchor restored without its detachment is
    // handed straight back to the centering policy (StashedStrip::viewDetached).
    out.viewDetached = state->strip().viewDetached();
    // Stamped so the restore can tell whether that anchor still means
    // anything. The state's RESOLVED axis is the right source: it advances on
    // every relayout, where the applied basis only advances on an emitted
    // batch and would be stale for a strip whose last pass the emit gate
    // suppressed.
    //
    // A state that has never been through applyLayout has no resolved axis
    // yet, and that is reachable — applyLayout is deferred to the end of an
    // arrival burst, so a save landing mid-burst sees exactly this. Resolve
    // the screen's LIVE axis rather than assuming Horizontal, which on a
    // portrait screen would mis-stamp the stash and make the restore drop a
    // view anchor that was in fact still meaningful. Degrades safely either
    // way (a wrong stamp only costs the anchor), so the fallback stays a
    // single resolve with no state of its own.
    // The pre-resolved fallback exists for callers running inside a state-map
    // walk, where resolving live would invoke the injected providers
    // mid-iteration (see setActiveScreens).
    out.axis = state->hasResolvedAxis() ? state->resolvedAxis().axis()
        : preResolvedFallbackAxis       ? *preResolvedFallbackAxis
                                        : stripAxisForScreen(state->screenId()).axis();
    return out;
}

void ScrollEngine::stashStripStructure(const PhosphorEngine::PlacementStateKey& key, const ScrollState* state,
                                       std::optional<PhosphorProtocol::ScrollAxis> preResolvedFallbackAxis)
{
    StashedStrip stash = buildStashFromState(state, preResolvedFallbackAxis);
    // A cursor-only entry is worth storing even with no columns to rebuild.
    // The screen still stands for the blueprint entries it spent — its windows
    // are floated or minimized, and every path that brings them back to the
    // strip (unfloat, unminimize) consumes no entry — so dropping the entry
    // here restarted the blueprint on the next fresh open. Structure-less
    // entries carry nothing else: no tiles to claim, so restoreFromStripStash
    // never matches one, and the cursor reaches the state through the
    // arrival-time raise like any other.
    if (stash.isEmpty() && stash.blueprintCursor <= 0) {
        return;
    }
    // Recency stamp: serializeStripState resolves a window listed under two
    // different keys in favour of the newer entry, and keys do not collide so
    // write order cannot decide it.
    stash.sequence = ++m_stashSequence;
    m_stripStash.insert(key, stash);
    // Fresh capture: nothing consumed yet (a stale consumed set from an
    // earlier round trip must not mask the new stash's ids).
    m_stripStashConsumed.remove(key);
}

bool ScrollEngine::restoreFromStripStash(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                         const QString& windowId, const ScrollLayoutParams& params, int minWidth,
                                         int minHeight)
{
    const auto it = m_stripStash.find(key);
    if (it == m_stripStash.end()) {
        return false;
    }
    // A consumed id must not re-enter (same reasoning as the order seed's
    // consumed guard: a later unrelated open reusing the id would be
    // re-positioned by the stale entry).
    if (const auto consumedIt = m_stripStashConsumed.constFind(key);
        consumedIt != m_stripStashConsumed.cend() && consumedIt->contains(windowId)) {
        return false;
    }
    StashedStrip& stashStrip = it.value();
    // Spent-ness comes back BEFORE the tile claim below, not after it. Raised
    // rather than assigned, for the same reason readers floor the cursor at
    // the column count: this runs once per ARRIVAL, and a window that opened
    // fresh alongside the restore has already advanced the cursor past what
    // the stash recorded. Ahead of the claim because the claim can fail — an
    // arrival this entry does not name, or an entry with no tiles at all —
    // and the cursor is owed to the state either way.
    state->setBlueprintCursor(qMax(state->blueprintCursor(), stashStrip.blueprintCursor));
    // The blueprint the cursor counts against, handed over only when the stash
    // actually carries one AND the state has none of its own.
    //
    // Both halves matter. A state that has already stamped an identity has
    // consumed an entry or been restored earlier, and that value is the newer
    // account. And a stash identity that is NULL must not be stamped at all:
    // null is what an entry staged from the persisted blob holds (the identity
    // is deliberately not serialized), and establishing it would make the
    // consumption site compare null against the live blueprint, read a swap,
    // and reset the very cursor this restore just carried — the original
    // defect, moved to the restart path. Leaving it unestablished sends that
    // site down its stamp-and-keep arm, which is the right answer for a
    // persisted cursor and identical to what stamping would give for a context
    // that genuinely has no template (its live blueprint is null too).
    if (stashStrip.blueprintIdentity.isValid() && !state->hasBlueprintIdentity()) {
        state->setBlueprintIdentity(stashStrip.blueprintIdentity);
    }
    if (stashStrip.isEmpty()) {
        // Cursor-only entry (see stashStripStructure): no columns, so there is
        // nothing to claim and no tile whose consumption could ever retire it.
        // The cursor above was its entire payload and has now been handed
        // over, so the entry retires HERE and nowhere else: pruneStaleWindows'
        // zero-tile reap deliberately exempts a cursor-only carrier (it must
        // outlive the bring-up to reach this arrival), so without this erase
        // the entry would stand for the process lifetime, re-raising a cursor
        // the state already holds at a hash lookup per arrival.
        m_stripStash.erase(it);
        m_stripStashConsumed.remove(key);
        return false;
    }
    QVector<StashedColumn>& stash = stashStrip.columns;
    int colIdx = -1;
    int tileIdx = -1;
    /// Set when the tile at (colIdx, tileIdx) was matched by the cross-session
    /// appId fallback rather than an exact id: holds the stashed id awaiting
    /// the rename, which is committed only after a successful insert.
    QString claimedCandidate;
    for (int i = 0; i < stash.size() && colIdx < 0; ++i) {
        const int j = [&]() {
            for (int t = 0; t < stash.at(i).tiles.size(); ++t) {
                if (stash.at(i).tiles.at(t).windowId == windowId) {
                    return t;
                }
            }
            return -1;
        }();
        if (j >= 0) {
            colIdx = i;
            tileIdx = j;
        }
    }
    if (colIdx < 0) {
        // Cross-session drift: a login restore stashes LAST session's window
        // ids, whose uuid halves never reappear. Claim the first UNCLAIMED
        // stashed tile of the same app (the id's prefix before '|'), exact-
        // then-fuzzy like the placement store, renaming the stashed tile to
        // the live id so siblings/focus keep matching and two same-app
        // windows map one-to-one (a claimed tile is never re-claimed —
        // claiming rewrites its id to a live one, which later arrivals
        // cannot collide with).
        // extractAppId, NOT currentAppIdFor, unlike the capture and float
        // restore: this prefix is matched against ids stored VERBATIM by a
        // previous session, so it has to be built the same way those ids were
        // spelled. A registry answer would build "ghostty|" and never match a
        // stashed "|olduuid" — and, the case that really separates them, an
        // Electron/CEF app that RENAMED itself would build its new class here
        // while the stash still holds the old one. The accepted cost is that a
        // window whose canonical was frozen before KWin classed it has an
        // empty prefix and skips the cross-session claim entirely: it takes
        // the ordinary insertion-order position instead of reclaiming its
        // column, leaving the stash entry intact for a later arrival. A
        // degraded restore, not a wrong one.
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        const QString appPrefix = appId.isEmpty() ? QString() : appId + QLatin1Char('|');
        // The fuzzy claim only stands in for an arrival WAVE — session
        // restore, or a mode round trip re-announcing this screen's windows.
        // Both waves follow within seconds of the grace being opened (staging
        // / screen re-entry). Outside the grace this is a window the USER
        // opened, and adopting a dead sibling's slot would insert it off-view
        // without focus — seen live as new firefox/ghostty windows landing at
        // the park row for the whole session. Exact-id claims above are
        // untouched: the id match is its own evidence.
        // Half-open [0, grace): elapsed() < grace, deliberately NOT
        // hasExpired(grace). hasExpired compares STRICTLY GREATER, so it
        // holds a zero-length window OPEN for its whole first millisecond.
        // At the shipped 60 s grace that difference is unobservable, but it
        // makes a grace of 0 read as "still open" rather than "already
        // closed" — and 0 is the one value that lets a test reach the refusal
        // arm at all, since a test elapses well under a millisecond.
        //
        // isValid() still gates: an unstarted timer's elapsed() is
        // meaningless, and an entry whose grace was never opened is closed.
        const bool fuzzyGraceOpen =
            stashStrip.fuzzyClaimWindow.isValid() && stashStrip.fuzzyClaimWindow.elapsed() < m_fuzzyClaimGraceMs;
        if (!appPrefix.isEmpty() && !fuzzyGraceOpen) {
            qCDebug(lcScrollEngine) << "restoreFromStripStash: fuzzy-claim grace closed for" << windowId
                                    << "— treating as a fresh open";
        } else if (!appPrefix.isEmpty()) {
            // DELIBERATE: this appId claim is not gated on
            // stagedFromPersistence, so it also fires against an IN-SESSION
            // mode-exit stash — a new same-app window opened before a stashed
            // sibling re-announces can claim (and rename) that sibling's
            // tile, swapping their slots. Accepted as the cost of the case
            // the fallback exists for: a same-session app RESTART while the
            // screen sits in another mode regenerates the uuid, and a
            // persistence gate would strand that window's tile forever.
            const QSet<QString> consumed = m_stripStashConsumed.value(key);
            for (int i = 0; i < stash.size() && colIdx < 0; ++i) {
                for (int t = 0; t < stash.at(i).tiles.size(); ++t) {
                    // Deep copy, NOT a reference: the claim below rewrites
                    // this very tile's id, and comparing the focus id
                    // through an alias of the overwritten field silently
                    // broke the focus hand-over.
                    const QString candidate = stash.at(i).tiles.at(t).windowId;
                    if (!candidate.startsWith(appPrefix) || consumed.contains(candidate)
                        || state->strip().containsWindow(candidate)) {
                        continue;
                    }
                    // STAGE the claim; do not commit it until the insert below
                    // succeeds. Rewriting the tile id here and then returning
                    // false left the rename standing, so containsWindow() was
                    // permanently true for that tile: it could never be claimed
                    // again, the stash entry never completed, and the stashed
                    // focus had been reassigned to a window this restore never
                    // placed.
                    claimedCandidate = candidate;
                    colIdx = i;
                    tileIdx = t;
                    break;
                }
            }
        }
    }
    if (colIdx < 0) {
        return false;
    }
    // Commit a staged cross-session claim only once the tile is really placed.
    const auto commitClaim = [&]() {
        if (claimedCandidate.isEmpty()) {
            return;
        }
        stash[colIdx].tiles[tileIdx].windowId = windowId;
        if (stashStrip.focusedWindowId == claimedCandidate) {
            stashStrip.focusedWindowId = windowId;
        }
        if (stash[colIdx].activeWindowId == claimedCandidate) {
            stash[colIdx].activeWindowId = windowId;
        }
        // Remapped on the same terms as the shown tab: a same-app window
        // inheriting a dead one's slot inherits whether it sized the column.
        if (stash[colIdx].heightOwnerId == claimedCandidate) {
            stash[colIdx].heightOwnerId = windowId;
        }
    };
    // Captured BEFORE the insert, for the view re-assert below: whether the
    // stashed focus has already been claimed (so the restore has landed once
    // already), and the view the user had at this moment, which is the view
    // to hand back once the insert has stolen it. Read before commitClaim
    // renames the focus slot and before this arrival joins the consumed set.
    const bool focusRestoredEarlier = m_stripStashConsumed.value(key).contains(stashStrip.focusedWindowId);
    const QString focusBeforeInsert = state->strip().activeWindowId();
    const int anchorBeforeInsert = state->strip().viewAnchor();
    const bool detachedBeforeInsert = state->strip().viewDetached();
    const StashedColumn& sc = stash.at(colIdx);
    bool inserted = false;
    // A stashed sibling already present re-locates the live column — the
    // stashed column index goes stale as columns arrive and close.
    int liveCol = -1;
    for (const StashedTile& sibling : sc.tiles) {
        if (sibling.windowId == windowId) {
            continue;
        }
        const int c = state->strip().columnOfWindow(sibling.windowId);
        if (c >= 0) {
            liveCol = c;
            break;
        }
    }
    if (liveCol >= 0) {
        // Tile position among the ALREADY-ARRIVED stashed siblings.
        int at = 0;
        const Column& live = state->strip().columns().at(liveCol);
        for (int j = 0; j < tileIdx; ++j) {
            if (live.indexOfWindow(sc.tiles.at(j).windowId) >= 0) {
                ++at;
            }
        }
        inserted = state->strip().insertWindowIntoColumnAt(liveCol, at, windowId, params, minWidth, minHeight);
    } else {
        // New column at its stashed position among the stashed columns
        // that already have a representative on the strip.
        int colAt = 0;
        for (int i = 0; i < colIdx; ++i) {
            for (const StashedTile& t : stash.at(i).tiles) {
                if (state->strip().columnOfWindow(t.windowId) >= 0) {
                    ++colAt;
                    break;
                }
            }
        }
        inserted = state->strip().insertWindowAt(colAt, windowId, sc.width, sc.display, params);
        if (inserted) {
            state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
        }
    }
    if (!inserted) {
        return false;
    }
    commitClaim();
    // Re-read through the container: commitClaim writes stash[colIdx], and a
    // detach there would leave `sc` dangling (the alias hazard the fuzzy-match
    // loop above documents). Every read past this point goes through stash.
    state->strip().setWindowHeightIntent(windowId, stash.at(colIdx).tiles.at(tileIdx).height);
    // Windowed fullscreen is strip-owned state the compositor mirrors, so a
    // claim hands it back (minimized deliberately is not re-applied — the
    // effect re-reports live minimize state; see StashedTile). EXACT-id
    // claims only: the fuzzy appId claim above deliberately lets a NEW
    // same-app window take a dead sibling's slot, and its accepted cost is
    // the slot, width and height — putting a fresh window into fullscreen
    // presentation because a previous instance was is not part of that
    // bargain.
    if (claimedCandidate.isEmpty() && stash.at(colIdx).tiles.at(tileIdx).windowedFullscreen) {
        state->strip().setWindowedFullscreen(windowId, true);
    }
    // Re-assert the column's stashed ACTIVE tile: every insert makes the
    // arriving tile active, so a tabbed column's shown tab would otherwise
    // be whichever sibling announced last.
    if (const QString tab = stash.at(colIdx).activeWindowId;
        !tab.isEmpty() && tab != windowId && state->strip().columnOfWindow(tab) >= 0) {
        state->strip().focusWindow(tab, params);
    }
    // The stashed EXTENT owner, a different question from the shown tab and
    // allowed to name a different window. Skipped until it is on the strip:
    // the restore arrives one window at a time.
    if (const QString owner = stash.at(colIdx).heightOwnerId;
        !owner.isEmpty() && state->strip().columnOfWindow(owner) >= 0) {
        state->strip().setTabbedHeightOwner(owner);
    }
    // The stashed FOCUS follows its window, not the arrival order: without
    // this the first arrival kept the focus it won on the empty strip and
    // every mode round trip re-anchored on an arbitrary window. The anchor
    // is restored after the focus so the user's actual view wins over the
    // focus change's centering-policy reanchor. Restored RAW, by
    // restoreViewAnchor's contract; later structural inserts re-clamp as the
    // strip grows (insertWindowAt's anchor re-clamp).
    //
    // Re-asserted on EVERY arrival of the restore burst once the focused
    // window is on the strip, not only on the arrival that IS it: inserts
    // steal focus (both insert verbs above make the arriving tile active and
    // reanchor) and so does the tab re-assertion, so a later arrival would
    // otherwise leave the restore anchored on an arbitrary window — the
    // regression the stash exists to fix. focusWindow is a no-op once the
    // state already matches.
    //
    // But only UNTIL the stashed focus has been claimed once. A stash entry
    // lives until every listed tile is claimed, and a same-app window can
    // claim a slot hours later; replaying last session's focus and anchor
    // over a view the user has since moved would rewind them to a strip they
    // left long ago. From then on the view to hand back is the one the user
    // had before THIS insert stole it, which is the same job for a late
    // arrival as the stash did for the burst.
    if (focusRestoredEarlier) {
        if (!focusBeforeInsert.isEmpty() && state->strip().containsWindow(focusBeforeInsert)) {
            state->strip().focusWindow(focusBeforeInsert, params);
            state->strip().restoreViewAnchor(anchorBeforeInsert, params);
            state->strip().setViewDetached(detachedBeforeInsert);
        }
    } else if (!stashStrip.focusedWindowId.isEmpty() && state->strip().containsWindow(stashStrip.focusedWindowId)) {
        state->strip().focusWindow(stashStrip.focusedWindowId, params);
        // The anchor is main-axis pixels, so it only means anything if it was
        // captured on THIS axis. Replaying one from the other axis scrolls the
        // restored strip to a nonsense position, and an out-of-range anchor is
        // legitimate under the current axis (restoreViewAnchor refuses to
        // clamp for exactly that reason), so there is no way to tell a good
        // one from a stale one after a flip. Drop it: the focus restore above
        // still lands, and the centering policy re-derives a view around it.
        if (stashStrip.axis == params.axis.axis()) {
            state->strip().restoreViewAnchor(stashStrip.viewAnchor, params);
            // After the anchor, and only in this arm: the focus call above
            // cleared the latch, restoreViewAnchor deliberately leaves it
            // alone, and the axis-mismatch arm that drops the anchor must
            // drop the detachment with it — a latch with no anchor behind it
            // would pin the view to wherever the focus restore landed.
            state->strip().setViewDetached(stashStrip.viewDetached);
        }
    }
    const int total = stashStrip.tileCount();
    // The CLAIMED tile's per-tile lease resets too: it was just consumed, so
    // it is no longer persistence-pending, and serializeStripState must not
    // age it (its window is live and will usually be pruned from the write
    // anyway, but a claimed-then-closed tile writes 0 — a fresh lease — which
    // is right: its app demonstrably comes back).
    stash[colIdx].tiles[tileIdx].stagedFromPersistence = false;
    stash[colIdx].tiles[tileIdx].unclaimedSessions = 0;
    QSet<QString>& consumed = m_stripStashConsumed[key];
    consumed.insert(windowId);
    if (consumed.size() >= total) {
        m_stripStash.remove(key);
        m_stripStashConsumed.remove(key);
    }
    return true;
}

void ScrollEngine::sweepStripStash(const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& stale)
{
    for (auto it = m_stripStash.begin(); it != m_stripStash.end();) {
        it = stale(it.key()) ? m_stripStash.erase(it) : std::next(it);
    }
    for (auto it = m_stripStashConsumed.begin(); it != m_stripStashConsumed.end();) {
        it = stale(it.key()) ? m_stripStashConsumed.erase(it) : std::next(it);
    }
}

void ScrollEngine::clearTabStripsForScreen(const QString& screenId)
{
    // Latch-guarded single clear: only screens that actually showed a strip
    // get the "[]" broadcast, so plain relayouts never spam the effect.
    m_lastTabStripPayload.remove(screenId);
    if (m_screensWithTabStrips.remove(screenId)) {
        Q_EMIT tabStripsChanged(screenId, QStringLiteral("[]"));
    }
}

// ── State resolution ────────────────────────────────────────────────────────

ScrollState* ScrollEngine::stateForKey(const PhosphorEngine::PlacementStateKey& key, bool createIfMissing)
{
    if (!createIfMissing) {
        return m_states.stateForKey(key);
    }
    return m_states.forKey(key, [this, &key]() -> ScrollState* {
        if (!m_scrollingScreens.contains(key.screenId)) {
            return nullptr;
        }
        return new ScrollState(key.screenId, this);
    });
}

ScrollState* ScrollEngine::stateForWindow(const QString& canonicalId, PhosphorEngine::PlacementStateKey* outKey) const
{
    return m_states.forWindow(canonicalId, outKey);
}

PhosphorEngine::IPlacementState* ScrollEngine::stateForScreen(const QString& screenId)
{
    return stateForKey(currentKeyForScreen(screenId), false);
}

const PhosphorEngine::IPlacementState* ScrollEngine::stateForScreen(const QString& screenId) const
{
    return m_states.stateForKey(m_context.currentKeyForScreen(screenId));
}

QString ScrollEngine::resolveOperationScreen(const QString& screenId) const
{
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        return screenId;
    }
    if (!m_activeScreen.isEmpty() && m_scrollingScreens.contains(m_activeScreen)) {
        return m_activeScreen;
    }
    if (m_scrollingScreens.isEmpty()) {
        return {};
    }
    // QSet iteration order is unspecified; pick the lexicographic minimum so
    // repeated shortcut presses with no active screen land deterministically.
    QString fallback = *m_scrollingScreens.cbegin();
    for (const QString& candidate : m_scrollingScreens) {
        if (candidate < fallback) {
            fallback = candidate;
        }
    }
    return fallback;
}

// ── Tracking predicates ─────────────────────────────────────────────────────

bool ScrollEngine::isWindowTracked(const QString& windowId) const
{
    return m_states.hasWindow(canonicalizeForLookup(windowId));
}

bool ScrollEngine::isWindowTiled(const QString& windowId) const
{
    const QString id = canonicalizeForLookup(windowId);
    const ScrollState* state = stateForWindow(id);
    return state && state->strip().containsWindow(id);
}

bool ScrollEngine::isWindowManaged(const QString& windowId) const
{
    return isWindowTiled(windowId);
}

QString ScrollEngine::screenForTrackedWindow(const QString& windowId) const
{
    return m_states.keyForWindow(canonicalizeForLookup(windowId)).screenId;
}

QString ScrollEngine::heldScreenForWindow(const QString& windowId) const
{
    // MEMBERSHIP-grade, unlike screenForTrackedWindow above (raw reverse-map
    // key): the screen is answered only when a state genuinely holds the
    // window, tiled or floating — a phantom key from a refused open answers
    // empty. This is the predicate the adaptor's post-reclaim ownership
    // check runs; see IPlacementEngine::heldScreenForWindow for why neither
    // isWindowTracked nor isWindowManaged can serve, and why the answer is
    // scoped to the screen's CURRENT context.
    const QString canonical = canonicalizeForLookup(windowId);
    PhosphorEngine::PlacementStateKey key;
    const ScrollState* state = stateForWindow(canonical, &key);
    if (state && state->containsWindow(canonical) && key == currentKeyForScreen(key.screenId)) {
        return key.screenId;
    }
    return {};
}

QRect ScrollEngine::lastManagedRect(const QString& rawWindowId) const
{
    return m_lastAppliedRect.value(canonicalizeForLookup(rawWindowId));
}

bool ScrollEngine::isWindowFloatingInScroll(const QString& windowId) const
{
    const QString id = canonicalizeForLookup(windowId);
    const ScrollState* state = stateForWindow(id);
    return state && state->isFloating(id);
}

QStringList ScrollEngine::allFloatingWindows() const
{
    QStringList all;
    const auto& states = m_states.states();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        all += it.value()->floatingWindows();
    }
    return all;
}

bool ScrollEngine::isModeSpecificFloated(const QString& windowId) const
{
    return m_scrollFloatedWindows.contains(canonicalizeForLookup(windowId));
}

void ScrollEngine::markModeSpecificFloated(const QString& windowId)
{
    m_scrollFloatedWindows.insert(canonicalizeForLookup(windowId));
}

void ScrollEngine::clearModeSpecificFloatMarker(const QString& windowId)
{
    m_scrollFloatedWindows.remove(canonicalizeForLookup(windowId));
}

// ── Ordering (mode-transition seams) ────────────────────────────────────────

QStringList ScrollEngine::managedWindowOrder(const QString& screenId) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    return state ? state->strip().windowsInOrder() : QStringList();
}

void ScrollEngine::setInitialWindowOrder(const QString& screenId, const QStringList& windowIds)
{
    m_consumedInitialOrder.remove(screenId);
    if (windowIds.isEmpty()) {
        m_pendingInitialOrder.remove(screenId);
    } else {
        m_pendingInitialOrder.insert(screenId, windowIds);
    }
    // A re-seed IS the "new transition" boundary the seed is scoped to, so the
    // focus half of the previous one cannot outlive it. The daemon writes the
    // two under different gates, so without this a fresh order can arrive
    // paired with a focus captured for an earlier flip. Cleared rather than
    // required-to-be-rewritten, because the caller may legitimately have no
    // focus to report; setInitialFocusedWindow then re-arms it.
    m_pendingInitialFocus.remove(screenId);
}

QString ScrollEngine::managedFocusedWindow(const QString& screenId) const
{
    // The strip's active window IS this engine's focus, and the CURRENT
    // context's is the only one a transition can be capturing. Non-creating,
    // matching managedWindowOrder's own lookup so the pair always describes
    // one strip.
    const ScrollState* state = m_states.stateForKey(currentKeyForScreen(screenId));
    return state ? state->strip().activeWindowId() : QString();
}

void ScrollEngine::setInitialFocusedWindow(const QString& screenId, const QString& windowId)
{
    // Empty clears rather than storing a blank: an outgoing engine with no
    // focus to report must not leave a previous transition's seed armed for
    // the next flip to apply.
    if (windowId.isEmpty()) {
        m_pendingInitialFocus.remove(screenId);
    } else {
        m_pendingInitialFocus.insert(screenId, windowId);
    }
}

// ── Persistence + settings ──────────────────────────────────────────────────

void ScrollEngine::saveState()
{
    if (m_persistSaveFn) {
        m_persistSaveFn();
    }
}

void ScrollEngine::loadState()
{
    if (m_persistLoadFn) {
        m_persistLoadFn();
    }
}

void ScrollEngine::refreshConfigFromSettings()
{
    auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings());
    if (!settings) {
        // Every cached tuning value silently keeps its previous (or default)
        // reading, so a mis-wired settings object looks like settings that
        // simply never take effect.
        qCWarning(lcScrollEngine) << "refreshConfigFromSettings: engine settings object is not an IScrollSettings — "
                                     "keeping the cached configuration";
        return;
    }
    const auto parsePresets = [](const QStringList& raw, qreal minFraction, const QList<qreal>& fallback) {
        QList<qreal> out;
        for (const QString& entry : raw) {
            bool ok = false;
            const qreal v = entry.trimmed().toDouble(&ok);
            // Same floor as every other proportion producer on the same
            // axis: a preset below it resolves to a sliver no window can
            // honour, and the preset cycle would silently offer a value the
            // setter refuses. Each axis passes ITS OWN floor — the height
            // constant exists precisely so a height caller cannot silently
            // follow a later width-only change (ScrollTypes.h's note).
            if (ok && v >= minFraction && v <= 1.0) {
                out.append(v);
            }
        }
        return out.isEmpty() ? fallback : out;
    };
    // KEEP IN SYNC with ScrollLayoutParams' member defaults (ScrollTypes.h).
    const QList<qreal> defaults{1.0 / 3.0, 0.5, 2.0 / 3.0};
    m_presetColumnWidths = parsePresets(settings->scrollingPresetColumnWidths(), MinColumnWidthFraction, defaults);
    m_presetWindowHeights = parsePresets(settings->scrollingPresetWindowHeights(), MinWindowHeightFraction, defaults);

    const int center = settings->scrollingCenterFocusedColumn();
    m_centerFocusedColumn =
        (center >= 0 && center <= 2) ? static_cast<CenterFocusedColumn>(center) : CenterFocusedColumn::Never;
    m_alwaysCenterSingleColumn = settings->scrollingAlwaysCenterSingleColumn();
    m_cropStraddlers = settings->scrollingCropStraddlers();
    // The TRI-STATE intent, kept as-is. Auto is deliberately NOT collapsed
    // here: there is no work area at this point, and collapsing it would
    // freeze one screen's verdict for every screen.
    const int axisIntent = settings->scrollingStripAxis();
    m_stripAxis = (axisIntent >= 0 && axisIntent <= 2) ? axisIntent : 0;

    // Edge auto-scroll. Bounded rather than raw: a zero or negative trigger
    // width would divide by zero in the ramp, and a non-positive speed would
    // arm a scroll that can never move (the delay would latch and the drop
    // target would stay locked to the edge slot forever). The upper bound
    // matters for the same reason the lower one does — the config schema
    // clamps these, but an IScrollSettings that is not the daemon's Settings
    // does not, and an unbounded speed teleports the strip in one tick. The
    // trigger width needs no ceiling here because the ramp re-clamps it to a
    // third of the work area, which is the only bound that means anything.
    m_dragScrollEnabled = settings->scrollingDragScrollEnabled();
    m_dragScrollTriggerWidth = qMax(1, settings->scrollingDragScrollTriggerWidth());
    m_dragScrollDelayMs = qMax(0, settings->scrollingDragScrollDelayMs());
    m_dragScrollMaxSpeed = std::clamp(settings->scrollingDragScrollMaxSpeed(), 1, kDragScrollMaxSpeedCeiling);

    // Guarded cast, matching every sibling enum in this function (center,
    // insertPos, sticky, indicator position): the shared value key can hold
    // a figure from the OTHER kind in a hand-edited config, and an
    // out-of-range kind falling through to the Proportion arm would clamp
    // a Fixed pixel figure to 1.0 — every new column full-width.
    const int widthKindRaw = settings->scrollingDefaultColumnWidthKind();
    const auto widthKind = (widthKindRaw >= static_cast<int>(DefaultWidthKind::Proportion)
                            && widthKindRaw <= static_cast<int>(DefaultWidthKind::Preset))
        ? static_cast<DefaultWidthKind>(widthKindRaw)
        : DefaultWidthKind::Proportion;
    const qreal widthValue = settings->scrollingDefaultColumnWidthValue();
    m_defaultWidthClientDecides = (widthKind == DefaultWidthKind::ClientDecides);
    if (widthKind == DefaultWidthKind::Fixed) {
        // Bounded before the round, symmetric with the Proportion arm's qBound
        // below: ISettings is an injected interface an embedder implements, so
        // the value is untrusted, and qRound of a double past int's range is
        // undefined.
        m_defaultColumnWidth = ColumnWidth::makeFixed(qRound(qBound(1.0, widthValue, kMaxFixedExtentPx)));
    } else if (widthKind == DefaultWidthKind::Preset) {
        // Config stays index-based (the spin names a slot in the list the
        // user edits on the same page); the VALUE anchor is resolved here,
        // against the freshly parsed list (guaranteed non-empty), and
        // relayout snaps it into whatever vocabulary a screen ends up with.
        m_defaultColumnWidth = ColumnWidth::makePreset(m_presetColumnWidths.at(
            qBound(0, settings->scrollingDefaultColumnWidthPresetIndex(), int(m_presetColumnWidths.size()) - 1)));
    } else {
        m_defaultColumnWidth = ColumnWidth::makeProportion(qBound<qreal>(MinColumnWidthFraction, widthValue, 1.0));
    }
    const int display = settings->scrollingDefaultColumnDisplay();
    m_defaultColumnDisplay =
        (display == static_cast<int>(ColumnDisplay::Tabbed)) ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;

    // Default window height. Translated with explicit ifs rather than cast:
    // the config vocabulary is DefaultHeightKind, whose ClientDecides has no
    // WindowHeight::Kind counterpart (see the enum). The client-sized case is
    // a flag the OPEN path reads, exactly like its width twin above, and the
    // height itself falls through to Auto so every consumer that needs a
    // concrete default (relayout, the reset verb's fallback) still has one.
    const int heightKind = settings->scrollingDefaultWindowHeightKind();
    m_defaultHeightClientDecides = (heightKind == static_cast<int>(DefaultHeightKind::ClientDecides));
    if (heightKind == static_cast<int>(DefaultHeightKind::Fixed)) {
        // Bounded before the round, for the width twin's reason.
        m_defaultWindowHeight = WindowHeight::makeFixed(
            qRound(qBound(1.0, settings->scrollingDefaultWindowHeightValue(), kMaxFixedExtentPx)));
    } else if (heightKind == static_cast<int>(DefaultHeightKind::Preset)) {
        // Same idx-to-value resolution as the width twin above.
        m_defaultWindowHeight = WindowHeight::makePreset(m_presetWindowHeights.at(
            qBound(0, settings->scrollingDefaultWindowHeightPresetIndex(), int(m_presetWindowHeights.size()) - 1)));
    } else {
        m_defaultWindowHeight = WindowHeight{};
    }

    const int insertPos = settings->scrollingInsertPosition();
    m_insertPosition = (insertPos >= static_cast<int>(ScrollInsertPosition::RightOfActive)
                        && insertPos <= static_cast<int>(ScrollInsertPosition::IntoActiveColumn))
        ? static_cast<ScrollInsertPosition>(insertPos)
        : ScrollInsertPosition::RightOfActive;

    const int sticky = settings->scrollingStickyWindowHandling();
    m_stickyWindowHandling = (sticky >= static_cast<int>(PhosphorEngine::StickyWindowHandling::TreatAsNormal)
                              && sticky <= static_cast<int>(PhosphorEngine::StickyWindowHandling::IgnoreAll))
        ? static_cast<PhosphorEngine::StickyWindowHandling>(sticky)
        : PhosphorEngine::StickyWindowHandling::TreatAsNormal;
    m_respectMinimumSize = settings->scrollingRespectMinimumSize();
    m_smartGaps = settings->scrollingSmartGaps();
    // Bounded like every other cast/derived read here: the value is derived
    // daemon-side from the animation duration, but nothing stops a future
    // implementor handing back garbage, and a multi-second hold would read
    // as the strip hanging after every close.
    m_closeReflowDelayMs = qBound(0, settings->scrollingCloseReflowDelayMs(), kMaxCloseReflowDelayMs);

    // Tab-indicator geometry. The numeric fields are taken as-is: the config
    // schema already clamps every one of them, and re-clamping here with a
    // second set of literals is exactly the drift the ConfigDefaults asserts
    // exist to prevent. The POSITION is the exception — it is cast to an enum,
    // so it gets the same validate-then-fall-back guard the other cast enums
    // above carry, and an unknown value leaves the configured default alone.
    m_tabIndicator.enabled = settings->scrollingTabIndicatorEnabled();
    m_tabIndicator.hideWhenSingleTab = settings->scrollingTabIndicatorHideWhenSingleTab();
    m_tabIndicator.placeWithinColumn = settings->scrollingTabIndicatorPlaceWithinColumn();
    m_tabIndicator.gap = settings->scrollingTabIndicatorGap();
    m_tabIndicator.width = settings->scrollingTabIndicatorWidth();
    m_tabIndicator.lengthProportion = settings->scrollingTabIndicatorLengthProportion();
    const int indicatorPos = settings->scrollingTabIndicatorPosition();
    if (indicatorPos >= static_cast<int>(TabIndicatorPosition::Left)
        && indicatorPos <= static_cast<int>(TabIndicatorPosition::Bottom)) {
        m_tabIndicator.position = static_cast<TabIndicatorPosition>(indicatorPos);
    }

    // Re-resolve every active strip against the new parameters.
    for (const QString& screenId : std::as_const(m_scrollingScreens)) {
        scheduleRetileForScreen(screenId);
    }
}

// ── Per-context rule overrides ──────────────────────────────────────────────
// The map's WRITERS live here; the effective* readers that layer it over the
// cached config defaults live in engine_overrides.cpp.

void ScrollEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    // Keyed by the screen's CURRENT context: the producer resolved these
    // overrides for (screen, desktop, activity), so storing them under the
    // screen alone would let one desktop's template overwrite another's.
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    const QVariantMap previous = m_perScreenOverrides.value(key);
    if (previous == overrides) {
        return;
    }
    m_perScreenOverrides.insert(key, overrides);
    // Deliberately no cursor reset here. A blueprint SWAP is noticed at the
    // consumption site by comparing ScrollState::blueprintIdentity against
    // the blueprint actually in force, which is the only place that can tell
    // a genuine template change from this map merely being rewritten. Every
    // desktop switch drops and re-pushes these overrides with the template
    // unchanged, and resetting on the write made that ordinary event refill
    // entries the strip's columns already stood for.
    scheduleRetileForScreen(screenId);
}

void ScrollEngine::clearPerScreenConfig(const QString& screenId)
{
    // Every context on the screen, not just the current one: this is the
    // whole-SCREEN door, and the caller is telling us the screen has left
    // scrolling entirely, so no context's overrides survive it. A caller that
    // means "this CONTEXT resolved no overrides" must not come here — it
    // pushes an empty map through applyPerScreenConfig instead, which reads
    // identically at every effective* reader without touching the sibling
    // contexts. The in-tree caller is updateScrollingScreens' departing-screen
    // loop, which runs after setActiveScreens has already dropped the screen.
    bool removed = false;
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        if (it.key().screenId == screenId) {
            it = m_perScreenOverrides.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) {
        // Deliberately no cursor reset. The strips on this screen survive a
        // trip out of scrolling — only the CURRENT context's state is torn
        // down — so their columns still stand for the blueprint entries they
        // took. Zeroing the cursor here meant a desktop switch away and back
        // handed those entries out a second time, which is the exact refill
        // the cursor exists to prevent. A template that genuinely changed
        // while the screen was away is caught by the identity compare at the
        // consumption site.
        scheduleRetileForScreen(screenId);
    }
}

void ScrollEngine::retile(const QString& screenId)
{
    if (screenId.isEmpty()) {
        for (const QString& sid : std::as_const(m_scrollingScreens)) {
            applyLayout(sid);
        }
        return;
    }
    // Membership guard, matching scheduleRetileForScreen and the queued
    // callback: a caller naming a screen this engine does not manage would
    // otherwise pay a full layoutParamsForScreen resolve and, if that
    // screen still carries a tab-strip latch, emit a tabStripsChanged for a
    // screen the engine does not own.
    if (!m_scrollingScreens.contains(screenId)) {
        return;
    }
    applyLayout(screenId);
}

void ScrollEngine::scheduleRetileForScreen(const QString& screenId)
{
    if (screenId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }
    if (m_pendingRetiles.contains(screenId)) {
        return;
    }
    m_pendingRetiles.insert(screenId);
    QMetaObject::invokeMethod(
        this,
        [this, screenId]() {
            if (m_pendingRetiles.remove(screenId) && m_scrollingScreens.contains(screenId)) {
                applyLayout(screenId);
            }
        },
        Qt::QueuedConnection);
}

} // namespace PhosphorScrollEngine
