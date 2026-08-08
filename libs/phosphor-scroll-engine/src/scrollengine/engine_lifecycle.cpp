// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

#include <algorithm>
#include <utility>

namespace PhosphorScrollEngine {

void ScrollEngine::seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight)
{
    // windowMinimumSize reads the clamp out of this entry for every window
    // that is floated rather than tiled, and the cross-engine handoff queries
    // it whatever state the window is in: with no entry the answer is the
    // "unknown" one, and the receiving engine gets an unclamped window. That
    // bites hardest on exactly the windows these paths float — the oversized
    // ones, whose clamp is why they could not take a column in the first
    // place.
    const auto existing = m_floatRestore.find(windowId);
    if (existing != m_floatRestore.end()) {
        // A real remembered slot is worth more than a slotless seed; only
        // the clamp is refreshed.
        existing->minWidth = qMax(0, minWidth);
        existing->minHeight = qMax(0, minHeight);
        return;
    }
    FloatRestore restore;
    restore.column = -1; // no slot to go back to; unfloat opens a fresh column
    restore.minWidth = qMax(0, minWidth);
    restore.minHeight = qMax(0, minHeight);
    m_floatRestore.insert(windowId, restore);
}

void ScrollEngine::restoreFloatRecordForOpen(const QString& windowId, const QString& screenId)
{
    const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
    if (!m_windowTracker || appId.isEmpty() || appId == windowId) {
        return;
    }
    // The window floats regardless (the caller already decided that), so only
    // a FLOATING record is consumed — takeForReopen's contract. The accept
    // itself is the store's shared predicate.
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    const auto record = m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, key.screenId);
    if (!record) {
        return;
    }
    // Same gate and screen-local rule as the record-float branch of
    // insertOpenedWindow (which documents both).
    const QString restoreScreen = record->screenId.isEmpty() ? screenId : record->screenId;
    const QRect freeGeo = record->freeGeometryFor(restoreScreen);
    const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
    if (freeGeo.isValid() && restorePosition) {
        Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
    }
}

bool ScrollEngine::insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId,
                                      int minWidth, int minHeight)
{
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);

    // Fixed-size / oversized windows cannot honour a column slot: float them
    // at their native size instead of forcing a tile (the min size is the
    // only constraint the compositor reports; a rule `float` action covers
    // the rest).
    const bool oversized =
        params.workArea.isValid() && (minWidth > params.workArea.width() || minHeight > params.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId, screenId);
    // Sticky handling gates insertion only: RestoreOnly and IgnoreAll both
    // keep sticky windows out of the strip, because insertion is active
    // management (autotile's shouldTileWindow makes the same collapse). The
    // desktop-pin logic in updateStickyScreenPins stays unconditional — with
    // sticky windows floated, the all-sticky managed set never forms and the
    // pin degrades correctly on its own.
    const bool stickyExcluded = m_stickyWindowHandling != PhosphorEngine::StickyWindowHandling::TreatAsNormal
        && m_windowTracker && m_windowTracker->isWindowSticky(windowId);
    if (oversized || ruleFloated || stickyExcluded) {
        state->addFloating(windowId);
        seedFloatRestoreForOpen(windowId, minWidth, minHeight);
        // Engine-decided float, so it carries the mode marker like every
        // other float this engine makes: isModeSpecificFloated has to answer
        // true or the daemon captures the scroll-mode float into the snap
        // slot at the next mode transition (presaveSnapFloats skips exactly
        // the marked windows).
        m_scrollFloatedWindows.insert(windowId);
        // A floated arrival consumes its seed entry too, or the screen's
        // list never empties and the stale entry survives every later mode
        // transition.
        consumePendingInitialOrder(screenId, windowId);
        // An engine-decided float still consumes its FLOATING placement
        // record and restores the remembered float-back — autotile reaches
        // the same outcome through its record branch (record first, rule
        // float layered on top); this engine floats before ever consulting
        // the store, so the consumption happens here or never.
        restoreFloatRecordForOpen(windowId, screenId);
        Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
        return true;
    }

    // Unified-placement FLOAT restore: a window whose record's scroll slot
    // says floating reopens floating, with the recorded float-back applied
    // through this engine's own gated geometryRestoreRequested emit below
    // (the daemon's passive float-sync arm deliberately restores no
    // geometry).
    // Resolved via the store's takeForReopen so a close/reopen — fresh KWin
    // uuid, appId-FIFO match — restores exactly like a daemon restart's
    // uuid-exact match. peekExact alone covered only the restart case: a
    // reopened floated window missed its record and fell through to a tile
    // insert. FLOATING records only, by the store's contract: a TILED record
    // is not consumed and restores no position — it stays in the store as the
    // exact-final evidence that the window closed tiled (so the reopen must
    // not float it), and the window takes a normal insert below. Column-order
    // restore across close/reopen was removed deliberately: reconstructing
    // strip order from per-window records needed close-burst ledgers and rank
    // anchors that every structural mutation had to invalidate, and the
    // strip stash already restores structure where it matters.
    const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
    if (m_windowTracker && !appId.isEmpty() && appId != windowId) {
        const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);
        if (const auto record =
                m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, currentKey.screenId)) {
            const PhosphorEngine::EngineSlot slot = record->slotFor(engineId());
            if (slot.state == PhosphorEngine::WindowPlacement::stateFloating()) {
                state->addFloating(windowId);
                seedFloatRestoreForOpen(windowId, minWidth, minHeight);
                // The record's SCROLL slot says floating, so this float is
                // this engine's own — marked like the rule-float exit above.
                m_scrollFloatedWindows.insert(windowId);
                consumePendingInitialOrder(screenId, windowId); // same rationale as the rule-float exit
                // The window is marked floating unconditionally above; only
                // the geometry MOVE onto the recorded free spot is gated
                // (daemon-wired scrollingRestoreFloatedWindowsOnLogin
                // setting + per-window RestorePosition rule) — the autotile
                // shape, insert.cpp. SCREEN-LOCAL recorded position only,
                // for autotile's documented reason: a rect captured on a
                // different screen would teleport the window while the
                // float tracking points elsewhere.
                const QString restoreScreen = record->screenId.isEmpty() ? screenId : record->screenId;
                const QRect freeGeo = record->freeGeometryFor(restoreScreen);
                const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
                if (freeGeo.isValid() && restorePosition) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
                return true;
            }
        }
    }

    // Taken off the params rather than re-resolved: layoutParamsForScreen
    // already resolved this exact value against this screen's override map and
    // preset vocabulary, and the screenId overload would parse both again.
    ColumnWidth width = params.defaultColumnWidth;
    ColumnDisplay display = effectiveDefaultColumnDisplay(screenId);
    // "Client decides" is the CONFIG default, so a per-screen rule override
    // outranks it — the header documents these overrides as layering over the
    // config defaults. Overwriting unconditionally meant a
    // SetScrollDefaultColumnWidth rule pinned to a screen never took effect
    // while the global kind was ClientDecides, with no diagnostic. (The
    // per-WINDOW open rule below is applied after this block and wins over
    // both, which is the intended precedence.)
    // The rule channel's bare fraction pins a width outright. The settings
    // channel's kind trio answers BY VALUE through
    // effectiveWidthClientDecides: a per-screen kind of Fixed/Preset/
    // Proportion pins a width (params.defaultColumnWidth above already
    // carries it resolved), while a per-screen kind of ClientDecides means exactly
    // that — testing the kind key's mere PRESENCE here inverted the setting,
    // gating the client-size branch off on precisely the monitors scoped to
    // it.
    const QVariantMap screenOverrides = m_perScreenOverrides.value(screenId);
    const bool rulePinsWidth = screenOverrides.contains(ScrollPerScreenKeys::defaultColumnWidth());
    if (effectiveWidthClientDecides(screenId) && m_windowTracker && !rulePinsWidth) {
        // Open at the client's own size when one is on record; the first
        // client resize reconciles it afterwards.
        if (const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId)) {
            width = ColumnWidth::makeFixed(geo->width());
        }
    }

    // Per-window open rules layer over the context/config defaults.
    ScrollOpenParams openParams;
    if (m_openParamsResolver) {
        openParams = m_openParamsResolver(windowId, screenId);
    }
    if (openParams.widthFraction) {
        width = ColumnWidth::makeProportion(qBound<qreal>(MinColumnWidthFraction, *openParams.widthFraction, 1.0));
    }
    if (openParams.tabbed) {
        display = *openParams.tabbed ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;
    }
    // Falls THROUGH on success rather than returning: the height commit at
    // the tail of this function is the one the openWindowHeight rule lands
    // on, and an early exit here dropped it silently for
    // openColumnPlacement=consume while its config-driven twin (the
    // IntoActiveColumn arm below) applied it.
    bool inserted = false;
    // Mode-round-trip structure restore FIRST — before the consume rule too:
    // a strip stashed at the last reassignment away from Scrolling rebuilds
    // exactly (stacks, widths, display, heights), which is strictly stronger
    // than the order seed's position-only verdict AND than an
    // openColumnPlacement=consume rule (letting the rule outrank the stash
    // left the window's stash tile unconsumed forever: it re-walked on every
    // later open, persisted across sessions, and could hand its slot to an
    // unrelated same-app window through the cross-session claim). The seed
    // entry is still consumed so it cannot linger past the adoption.
    if (restoreFromStripStash(state, currentKeyForScreen(screenId), windowId, params, minWidth, minHeight)) {
        inserted = true;
        consumePendingInitialOrder(screenId, windowId);
    }
    if (!inserted && openParams.consume && *openParams.consume && !state->strip().isEmpty()) {
        const std::optional<ColumnDisplay> displayOverride =
            openParams.tabbed ? std::optional<ColumnDisplay>(display) : std::nullopt;
        if (state->strip().insertWindowIntoActiveColumn(windowId, width, displayOverride, params, minWidth,
                                                        minHeight)) {
            // Consume this id from the mode-transition seed too — leaving
            // it would let a stale entry re-position an unrelated later
            // open (the block below documents exactly that hazard).
            consumePendingInitialOrder(screenId, windowId);
            inserted = true;
        }
    }

    // Deterministic mode-transition seeding: when the previous engine's
    // window order was captured for this screen, insert each arriving window
    // at its recorded relative position instead of next-to-focus. Each id is
    // CONSUMED on use and the entry is dropped once empty — the header's
    // "consumed as windows arrive" contract. Without consumption a stale
    // seed would re-position an unrelated later open that happens to share
    // an id with the captured list.
    const auto pendingIt = m_pendingInitialOrder.constFind(screenId);
    if (!inserted && pendingIt != m_pendingInitialOrder.constEnd()) {
        const int orderIdx = pendingIt->indexOf(windowId);
        // A consumed id must not re-enter the seed branch: a later unrelated
        // open reusing the id would otherwise be re-positioned by the stale
        // entry (the list keeps consumed ids to preserve positions).
        if (orderIdx >= 0 && !m_consumedInitialOrder.value(screenId).contains(windowId)) {
            int columnIdx = 0;
            const QStringList present = state->strip().windowsInOrder();
            for (const QString& earlier : pendingIt->mid(0, orderIdx)) {
                if (present.contains(earlier)) {
                    ++columnIdx;
                }
            }
            inserted = state->strip().insertWindowAt(columnIdx, windowId, width, display, params);
            if (inserted) {
                state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
            }
            // Through the shared consume helper — it drops the screen's entry
            // once the list empties. pendingIt is dangling from here.
            consumePendingInitialOrder(screenId, windowId);
        }
    }
    if (!inserted) {
        // Fresh open with no remembered position: the ONLY site the
        // insert-position setting governs. Restore/seed/unfloat paths above
        // and the re-homing call sites elsewhere keep right-of-active — a
        // "first/last" default teleporting a restored window would read as
        // a lost slot. IntoActiveColumn routes through the consume verb
        // (same shape as the openColumnPlacement rule) and falls through to
        // a positional insert on an empty strip.
        //
        // TEMPLATE blueprint: while the strip holds fewer columns than the
        // context template's blueprint, the materializing column takes the
        // next blueprint entry's width and display. Applied ONLY here (a
        // genuinely new column on the fresh-open path) so restore, seed and
        // stash adoptions keep their remembered shapes, and never
        // retroactively — a template change reshapes nothing that already
        // exists. Precedence: per-window open rules above outrank the
        // blueprint; the blueprint outranks every default, including a
        // client-decides width already resolved into `width`.
        const auto blueprintIt = screenOverrides.constFind(ScrollPerScreenKeys::templateColumns());
        const int columnCount = int(state->strip().columns().size());
        // Bounded at kMaxTemplateEntries, the same library-boundary cap the
        // preset vocabularies get: applyPerScreenConfig is exported LGPL
        // surface and an embedder-supplied blueprint must not be read past
        // it. Tested BEFORE the list conversion so a strip that is already
        // longer than any consumable entry pays nothing on the open path.
        if (blueprintIt != screenOverrides.constEnd() && columnCount < kMaxTemplateEntries) {
            const QVariantList blueprint = blueprintIt->toList();
            if (columnCount < blueprint.size()) {
                const QVariantMap entry = blueprint.at(columnCount).toMap();
                const qreal fraction = entry.value(ScrollPerScreenKeys::templateColumnWidth()).toDouble();
                if (!openParams.widthFraction && fraction >= MinColumnWidthFraction && fraction <= 1.0) {
                    width = ColumnWidth::makeProportion(fraction);
                }
                // Guarded on PRESENCE, mirroring the width arm's fall-through:
                // an entry that carries a width only must leave `display` on
                // the effective default resolved above. Reading an absent key
                // as 0 forced every such column to Normal and silently
                // discarded a Tabbed default (from the settings-channel
                // default, or a SetScrollDefaultColumnDisplay rule) for
                // exactly the first N columns. The in-tree daemon always
                // writes both keys on every entry, so this guard is a
                // public-API belt for embedder-supplied maps rather than a
                // fix for a shipped bug.
                if (!openParams.tabbed && entry.contains(ScrollPerScreenKeys::templateColumnDisplay())) {
                    display = entry.value(ScrollPerScreenKeys::templateColumnDisplay()).toInt() == 1
                        ? ColumnDisplay::Tabbed
                        : ColumnDisplay::Normal;
                }
            }
        }
        const ScrollInsertPosition insertPos = effectiveInsertPosition(screenId);
        if (insertPos == ScrollInsertPosition::IntoActiveColumn && !state->strip().isEmpty()) {
            inserted =
                state->strip().insertWindowIntoActiveColumn(windowId, width, std::nullopt, params, minWidth, minHeight);
        }
        if (!inserted) {
            inserted = state->strip().insertWindow(
                windowId, width, display, params, minWidth, minHeight,
                insertPos == ScrollInsertPosition::IntoActiveColumn ? ScrollInsertPosition::RightOfActive : insertPos);
        }
    }
    if (inserted && openParams.heightFraction && params.workArea.height() > 0) {
        // Per-window open rule wins over every default and remembered
        // height, matching the width/tabbed precedence above. Committed as
        // Fixed pixels against the live work area, the same resolution the
        // adjust verbs use.
        const qreal fraction = qBound<qreal>(MinWindowHeightFraction, *openParams.heightFraction, 1.0);
        state->strip().setWindowHeightIntent(
            windowId, WindowHeight::makeFixed(qMax(1, qRound(fraction * params.workArea.height()))));
    }
    if (!inserted) {
        qCWarning(lcScrollEngine) << "insertOpenedWindow: duplicate window" << windowId;
        // Do not leave a reverse-map key for a window no structure holds —
        // that is the exact inconsistency floatWindowInternal warns about.
        // (Keyed-but-present is fine: the insert also fails when the strip
        // already contains the window, and unkeying a live tile would just
        // create the mirror inconsistency.)
        if (!state->strip().containsWindow(windowId) && !state->isFloating(windowId)) {
            m_states.removeWindow(windowId);
        }
    }
    return inserted;
}

bool ScrollEngine::claimCrossScreenReopen(const QString& rawWindowId, const QString& openingScreenId, int minWidth,
                                          int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // openingScreenId validated here, at the library boundary: the adaptor's
    // dispatch cannot produce an empty one, but this is public engine API and
    // an empty opening screen would defeat the predicate's same-screen bail
    // (empty compares unequal to every recorded screen).
    if (windowId.isEmpty() || openingScreenId.isEmpty() || !m_scrollingModeResolver || !m_windowTracker) {
        return false;
    }
    // First observation only, by MEMBERSHIP: a window this engine already
    // holds anywhere is an in-session move or re-announce, never a session
    // restore — yanking it back to the record's screen would undo the very
    // move that re-announced it. Membership, not the raw reverse-map key
    // (the rule windowOpened's defer gate documents): a phantom key left by
    // a refused earlier open must not veto a legitimate claim.
    if (const ScrollState* tracked = stateForWindow(windowId); tracked && tracked->containsWindow(windowId)) {
        return false;
    }
    // Registry-aware appId, like autotile's twin and like every record
    // producer (captures write the tracker's current appId): parsing the
    // frozen canonical string would look in the wrong bucket after an
    // Electron/CEF class mutation.
    const QString appId = m_windowTracker->currentAppIdFor(windowId);
    if (!PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        return false;
    }
    // peekForReclaim, not peek: the live-instance exclusion is what stops a
    // fresh second instance being pulled onto its OPEN sibling's monitor on
    // the strength of that sibling's live record. Non-consuming either way —
    // consumption stays with windowOpened's own restore machinery (the strip
    // stash claim and takeForReopen), which this claim funnels the window
    // into by re-entering the open path with the RECORDED screen. Only a
    // TILED slot earns the pull — a scroll-floating record is screen-local,
    // matching snap's float doctrine.
    const auto pending = m_windowTracker->placementStore().peekForReclaim(
        windowId, appId, [&](const PhosphorEngine::WindowPlacement& p) {
            return PhosphorEngine::pendingCrossScreenManagedRestore(
                p, PhosphorEngine::WindowPlacement::scrollingEngineId(), PhosphorEngine::WindowPlacement::stateTiled(),
                openingScreenId, [this](const QString& rec, int desktop, const QString& activity) {
                    return m_scrollingModeResolver(rec, desktop, activity);
                });
        });
    if (!pending) {
        return false;
    }
    const QString homeScreen = pending->screenId;
    // The LIVE screen set must agree with the record-context verdict:
    // windowOpened's own m_scrollingScreens gate would otherwise refuse the
    // adoption AFTER this method already answered "claimed", stranding the
    // window untracked (a per-desktop mode override can make the resolver
    // and the live set disagree during a context switch).
    if (!m_scrollingScreens.contains(homeScreen)) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "recorded home" << homeScreen
                               << "is scrolling-mode by record context but absent from the live set — declining";
        return false;
    }
    // Grant-context must be compatible with insert-context. The predicate
    // granted on the RECORD's (desktop, activity); windowOpened inserts into
    // the home screen's CURRENT key. When two CONCRETE contexts disagree,
    // adopting would splice the window into a strip on a desktop it is not
    // visible on (displacing that desktop's real windows) and miss its
    // stashed column outright — the conservative skip leaves the window to
    // the ordinary open path instead. Sticky / unknown-context records
    // (the 0 and empty sentinels) stay eligible; see
    // recordContextMatchesLive.
    const PhosphorEngine::PlacementStateKey homeKey = currentKeyForScreen(homeScreen);
    if (!PhosphorEngine::recordContextMatchesLive(*pending, homeKey.desktop, homeKey.activity)) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "recorded context desktop"
                               << pending->virtualDesktop << "activity" << pending->activity << "differs from"
                               << homeScreen << "current context — declining";
        return false;
    }
    windowOpened(windowId, homeScreen, minWidth, minHeight);
    // Return the REAL outcome, verified by membership (ScrollState-level: a
    // legitimately floated adoption is in the floating set, not the strip).
    // An optimistic true converted every silently-refused re-entry into a
    // window no engine manages — the caller hands a claimed window to no
    // other engine.
    const ScrollState* adopted = stateForWindow(windowId);
    if (!adopted || !adopted->containsWindow(windowId)) {
        qCWarning(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "adoption on" << homeScreen
                                  << "was refused — not claimed";
        return false;
    }
    // The ARRIVAL screen's mode-transition seed still names this id; the
    // arrival-screen windowOpened that would have consumed it never runs on
    // a successful claim (the dispatch returns), and one unconsumed id pins
    // that screen's seed for the whole session. Safe no-op for a screen with
    // no seed.
    consumePendingInitialOrder(openingScreenId, windowId);
    qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "opened on" << openingScreenId
                           << "— reclaimed to recorded scrolling home" << homeScreen;
    return true;
}

void ScrollEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }

    PhosphorEngine::PlacementStateKey oldKey;
    ScrollState* oldState = stateForWindow(windowId, &oldKey);
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (oldState && oldKey == key) {
        // Re-announce of a window we already track here. Still an arrival as
        // far as the mode-transition seed is concerned: the header's
        // "consumed on EVERY outcome" invariant has no exception for this
        // one, and skipping it leaves the screen's seed list unable to empty,
        // so it survives to re-position an unrelated later open. No-op when
        // the screen carries no seed, which is the usual case here.
        consumePendingInitialOrder(screenId, windowId);
        return;
    }

    // Cross-screen restore defer, the reciprocal of SnapEngine's
    // recorded-screen gate and autotile's claimCrossScreenReopen — every
    // engine runs PhosphorEngine::pendingCrossScreenManagedRestore over the
    // same record fields, so a session window snapped on a snapping-mode
    // monitor (or tiled on an autotile-mode monitor) that KWin drops on a
    // scrolling screen at login is claimed by its OWN engine cross-screen
    // and NOT double-claimed into the strip here. Gated on
    // !oldState: a window this engine already tracks anywhere is scroll's
    // own (in-session migration), never a session restore.
    // Membership, not the raw reverse-map key (autotile's gate term for
    // term): a refused earlier open can leave a phantom key, and gating on
    // it would skip the defer while this engine manages nothing.
    const bool trackedHere = oldState && oldState->containsWindow(windowId);
    if (!trackedHere && m_windowTracker && (m_snappingModeResolver || m_autotileModeResolver)) {
        // Registry-aware appId and the reclaim-grade lookup, matching what
        // the CLAIMING side asks. Both halves are load-bearing for the N-way
        // agreement: parsing the frozen canonical string would read a
        // different bucket after an Electron/CEF class mutation (so this gate
        // could miss a record the claim finds — both-claimed), and a plain
        // peek would see a LIVE sibling's record the claim's exclusion
        // rejects (so this gate could defer to an engine that then declines —
        // both-skipped).
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        if (PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
            const auto crossRestorePending = [&](const PhosphorEngine::WindowPlacement& p) {
                if (m_snappingModeResolver
                    && PhosphorEngine::pendingCrossScreenSnapRestore(
                        p, screenId, [this](const QString& rec, int desktop, const QString& activity) {
                            return m_snappingModeResolver(rec, desktop, activity);
                        })) {
                    return true;
                }
                return m_autotileModeResolver
                    && PhosphorEngine::pendingCrossScreenManagedRestore(
                           p, PhosphorEngine::WindowPlacement::autotileEngineId(),
                           PhosphorEngine::WindowPlacement::stateTiled(), screenId,
                           [this](const QString& rec, int desktop, const QString& activity) {
                               return m_autotileModeResolver(rec, desktop, activity);
                           });
            };
            if (m_windowTracker->placementStore().peekForReclaim(windowId, appId, crossRestorePending).has_value()) {
                qCInfo(lcScrollEngine) << "windowOpened:" << windowId << "on scrolling screen" << screenId
                                       << "defers — carries a cross-screen restore for another engine";
                // A deferred arrival is still an arrival: without the
                // consume, this id never reaches insertOpenedWindow and its
                // seed entry lingers on the screen forever.
                consumePendingInitialOrder(screenId, windowId);
                // A refused-first-open phantom key must not survive the defer
                // (autotile's twin does the same): isWindowTracked would keep
                // answering true for a window another engine is about to own,
                // misrouting the daemon's float and handoff dispatch. The
                // enclosing block is already !trackedHere, so a key here is
                // by definition a phantom.
                if (stateForWindow(windowId)) {
                    m_states.removeWindow(windowId);
                }
                return;
            }
        }
    }
    if (oldState) {
        // The window moved context (screen or desktop) — migrate. The old
        // context's per-window bookkeeping goes with it: a stale
        // FloatRestore could re-slot an unfloat on the NEW screen against
        // the OLD strip's geometry, and lastAppliedRect would keep
        // answering for a context that no longer holds the window.
        const ScrollLayoutParams oldParams = layoutParamsForScreen(oldKey.screenId);
        const bool wasFloating = oldState->isFloating(windowId);
        oldState->strip().takeWindow(windowId, oldParams);
        oldState->removeFloating(windowId);
        m_floatRestore.remove(windowId);
        // The mode-float marker goes with the old context too: the window
        // re-enters (usually tiled) on the new screen, and a stale marker
        // would re-float it at the next mode transition.
        m_scrollFloatedWindows.remove(windowId);
        if (wasFloating) {
            // Announce the dropped float bit: signal-driven subscribers
            // (the effect's FloatingCache) would otherwise keep believing
            // the window floats while insertOpenedWindow tiles it below,
            // and resolve the divergence as a float-back. A float RECORD
            // re-float re-announces true immediately afterwards.
            Q_EMIT windowFloatingStateSynced(windowId, false, oldKey.screenId);
        }
        // Background-context guard, the same one windowClosed and the float
        // paths carry: a scheduled retile resolves the screen's CURRENT
        // context, so a migration out of another desktop's state must not
        // drive one. The switch back retiles the mutated strip.
        if (oldKey == currentKeyForScreen(oldKey.screenId)) {
            scheduleRetileForScreen(oldKey.screenId);
        }
        Q_EMIT placementChanged(oldKey.screenId);
    }

    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    // Track BEFORE inserting: insertOpenedWindow's oversized/rule-float
    // paths emit windowFloatingStateSynced, and a synchronous query-back from
    // a subscriber must already see the window as this engine's.
    m_states.setKeyForWindow(windowId, key);
    // Capture the pre-insert focus: with focus-new-windows OFF the
    // compositor keeps focus on the previous window, so the strip must not
    // adopt the arrival as its active column either — a diverged strip
    // makes every later focus-direction verb navigate from the wrong
    // origin, and a consume-open into a tabbed column would park the
    // window the user is actually looking at.
    const QString priorActive = state->strip().activeWindowId();
    // Re-adoption starts from a blank rect memory, unconditionally. The close
    // and release paths deliberately RETAIN m_lastAppliedRect (the daemon's
    // close capture reads it as the float-back poison guard), and this is the
    // second of its two reclaimers — pruneStaleWindows is the other, and it
    // only fires on aliveness. Left standing, a retained rect that happens to
    // equal the one the strip resolves defeats applyLayout's emit-on-change
    // gate, so no windowsTiled batch ever fires for the re-adopted window and
    // a single-window screen sits at the geometry the OTHER mode left it in.
    const QRect priorAppliedRect = m_lastAppliedRect.value(windowId);
    m_lastAppliedRect.remove(windowId);
    // The parked-edge memory follows the rect memory: a re-adopted window
    // never parked here, and a stale side would mis-anchor its first arrival
    // slide. Taken (not dropped) so the refuse branch can put it back — a
    // refused insert means the window is a live tile that may genuinely be
    // parked right now.
    const QString priorParkedEdge = m_parkedScrollEdge.take(windowId);
    if (!insertOpenedWindow(state, windowId, screenId, minWidth, minHeight)) {
        // Every insert refused (the strip already holds the window). Nothing
        // moved and nothing was adopted, so neither the geometry batch nor
        // the dirty mark may fire — and the rect memory goes back, because
        // the window is still a live tile: dropped, lastManagedRect answers
        // null and a later float-back captures the COLUMN rect as free
        // geometry, which is the poison this map exists to prevent.
        if (priorAppliedRect.isValid()) {
            m_lastAppliedRect.insert(windowId, priorAppliedRect);
        }
        if (!priorParkedEdge.isEmpty()) {
            m_parkedScrollEdge.insert(windowId, priorParkedEdge);
        }
        return;
    }

    bool focusNew = true;
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        focusNew = settings->scrollingFocusNewWindows();
    }
    const bool arrivalTookFocus = focusNew && state->strip().activeWindowId() == windowId;
    if (!focusNew && !priorActive.isEmpty() && state->strip().activeWindowId() == windowId
        && state->strip().containsWindow(priorActive)) {
        const ScrollLayoutParams params = layoutParamsForScreen(screenId);
        state->strip().focusWindow(priorActive, params);
    }
    // Only an arrival that actually TAKES focus re-targets the screen-hintless
    // shortcut paths. Writing this on ANY arrival pointed them at whatever
    // monitor a background app last opened a window on — including floated
    // opens and opens under focus-new-windows OFF, neither of which the user
    // is looking at. Focus events own the value the rest of the time.
    if (arrivalTookFocus) {
        m_activeScreen = screenId;
    }
    // Inside an arrival burst (daemon-restart re-announce, mode flip) the
    // apply is deferred to the outermost endArrivalBurst: each arrival here
    // splices into a PARTIAL strip, and applying per arrival marches every
    // already-placed window through N intermediate layouts the user can see
    // even when the restored strip resolves to exactly the pre-restart rects.
    if (m_arrivalBurstDepth > 0) {
        // Keyed by CONTEXT, not bare screen: a desktop/activity switch landing
        // mid-burst must not replay the deferred apply against the new
        // context's strip (the drain below skips a key the screen no longer
        // resolves to).
        auto it = m_burstPendingApplies.find(key);
        if (it == m_burstPendingApplies.end()) {
            m_burstPendingApplies.insert(key, arrivalTookFocus);
        } else {
            it.value() = it.value() || arrivalTookFocus;
        }
    } else {
        applyLayout(screenId, arrivalTookFocus);
    }
    Q_EMIT placementChanged(screenId);
}

void ScrollEngine::beginArrivalBurst()
{
    ++m_arrivalBurstDepth;
}

void ScrollEngine::endArrivalBurst()
{
    if (m_arrivalBurstDepth == 0 || --m_arrivalBurstDepth > 0) {
        return;
    }
    const QHash<PhosphorEngine::PlacementStateKey, bool> pending = std::move(m_burstPendingApplies);
    // Sorted, not hash order: with focus-taking arrivals on two screens the
    // LAST activation request wins the compositor's focus, and hash order
    // would make that winner vary run to run.
    QList<PhosphorEngine::PlacementStateKey> keys = pending.keys();
    std::sort(keys.begin(), keys.end(),
              [](const PhosphorEngine::PlacementStateKey& a, const PhosphorEngine::PlacementStateKey& b) {
                  return std::tie(a.screenId, a.desktop, a.activity) < std::tie(b.screenId, b.desktop, b.activity);
              });
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(keys)) {
        // The screen may have left the scrolling set mid-burst (mode flip
        // races — applyLayout's own guards no-op that), and a context switch
        // mid-burst means the deferred apply's strip is no longer the one on
        // screen: skip it, the switch-back retile covers the mutated strip.
        if (key != currentKeyForScreen(key.screenId)) {
            continue;
        }
        applyLayout(key.screenId, pending.value(key));
    }
}

void ScrollEngine::windowClosed(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // Before any state mutation: a preview whose dragged window just closed
    // must not survive to restore a dead id (autotile's
    // dropClosedWindowFromDragPreview twin).
    dropClosedWindowFromDragPreview(windowId);
    // A pending self-activation for a window that closes before its echo
    // lands can never be answered; without this a later genuine focus of a
    // reused id would be eaten as that echo.
    m_pendingSelfActivations.removeAll(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    const bool wasActive = state->strip().activeWindowId() == windowId;
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const bool inStrip = state->strip().removeWindow(windowId, params);
    // Unconditional, not gated on the strip removal failing: the two sets are
    // meant to be disjoint, but a window that somehow sits in BOTH would keep
    // its floating entry forever under the gated form — nothing else ever
    // revisits a closed window's floating membership. A no-op for the normal
    // case, which is the whole point.
    state->removeFloating(windowId);
    m_states.removeWindow(windowId);
    // m_lastAppliedRect is deliberately RETAINED through the close: the
    // daemon's close capture consults lastManagedRect DURING windowClosed
    // (the engines hear the close before WindowTracking does), and the
    // live frame is still the strip rect at that moment — without the
    // memory, the column rect becomes the reopen float-back geometry (the
    // float-back tile-rect poison, autotile's twin retains for the same
    // reason). pruneStaleWindows reclaims the entry independently.
    m_floatRestore.remove(windowId);
    m_scrollFloatedWindows.remove(windowId);

    if (inStrip && key == currentKeyForScreen(key.screenId)) {
        // Background-context guard: applyLayout resolves the screen's
        // CURRENT context, so a close on another desktop's state would
        // relayout the wrong strip (the mutated one must stay silent
        // until its desktop returns).
        applyLayout(key.screenId, wasActive && !state->strip().activeWindowId().isEmpty());
    }
    Q_EMIT placementChanged(key.screenId);
}

void ScrollEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
    // Self-activation echo filter (the m_pendingSelfActivations doc): a
    // report answering this engine's own activateWindowRequested carries no
    // new information — the strip already reflects it, or has legitimately
    // moved past it on a rapid focus scroll, and focusWindow below would
    // rewind the active column to the stale echo. Entries ahead of the match
    // go with it: their echoes were dropped by the effect and can never
    // arrive after this one on the ordered connection.
    if (const int selfIdx = m_pendingSelfActivations.indexOf(windowId); selfIdx >= 0) {
        m_pendingSelfActivations.erase(m_pendingSelfActivations.begin(),
                                       m_pendingSelfActivations.begin() + selfIdx + 1);
        return;
    }
    // A genuine focus report implies every previously-sent echo already
    // landed, so whatever is left in the queue was dropped — reclaim it.
    m_pendingSelfActivations.clear();
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    if (state->strip().focusWindow(windowId, params)) {
        // The focus change may scroll the viewport; never re-activate here
        // (the compositor initiated this focus). Background-context guard:
        // see windowClosed.
        if (key == currentKeyForScreen(key.screenId)) {
            applyLayout(key.screenId, false);
        }
        // Focus and view anchor are persisted (serializeStripState), and
        // placementChanged is the only thing that marks DirtyScrollStrips.
        // Emitted for a background context too: the strip that changed is
        // serialized whether or not it is the one on screen right now.
        Q_EMIT placementChanged(key.screenId);
    }
}

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
    // Background-context guard, the same one windowClosed and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // min-size report for a window on another desktop would relayout a strip
    // this change did not touch. The model write still lands; the switch back
    // retiles the mutated strip.
    if (state->strip().setWindowMinimumSize(windowId, minWidth, minHeight)
        && key == currentKeyForScreen(key.screenId)) {
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
    const bool widthChanged = lastApplied.width() != newFrame.width();
    const bool heightChanged = lastApplied.height() != newFrame.height();
    if (state->strip().reconcileWindowSize(windowId, newFrame.size(), widthChanged, heightChanged)) {
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
    if (lastApplied != newFrame) {
        m_lastAppliedRect.remove(windowId);
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
    }
}

// ── Cross-engine handoff ────────────────────────────────────────────────────

void ScrollEngine::handoffRelease(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A preview naming this window must not survive its tracking: commit
    // would re-insert into a strip another engine has since adopted the
    // window from (shared contract gap with autotile's twin, closed here).
    dropClosedWindowFromDragPreview(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    // Tracking-only clear: the receiving engine places the window; this
    // screen's remaining columns close up on the scheduled retile.
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    state->strip().takeWindow(windowId, params);
    state->removeFloating(windowId);
    m_states.removeWindow(windowId);
    // The durable slot goes with the tracking: a released window is one this
    // engine knowingly gave up, and a stale scrolling TILED slot left in the
    // unified record is not memory but a false home — paired with a stale
    // record-level screenId (which an engine-miss capture can leave behind),
    // the cross-screen reclaim would later yank the window back out from
    // under its new engine, and that engine's defer gate would read the same
    // stale record and stand down. Ordinary close deliberately KEEPS the
    // slot; only the handoff clears it.
    if (m_windowTracker) {
        m_windowTracker->releaseEngineSlot(windowId, engineId());
    }
    // A released window's queued echo can never be answered — the stale
    // entry would eat the first genuine focus when the window comes back
    // (releaseScreenState documents the same sweep).
    m_pendingSelfActivations.removeAll(windowId);
    // m_lastAppliedRect deliberately retained (same rationale as
    // windowClosed: a close/capture racing the handoff still needs the
    // poison-guard memory; pruneStaleWindows reclaims it).
    m_floatRestore.remove(windowId);
    // The mode-transition float marker must not outlive this engine's
    // tracking: the receiving engine owns the float bit from here, and a
    // stale entry would keep isModeSpecificFloated answering true.
    m_scrollFloatedWindows.remove(windowId);
    // Same orphan rule as the float path: the window leaves this engine
    // alive, so the park-edge memory has to go here or it survives to
    // mis-anchor the first arrival after a later re-adoption.
    m_parkedScrollEdge.remove(windowId);
    // Background-context guard, as windowClosed and the float paths carry: a
    // release out of another desktop's state must not retile the strip that
    // is on screen right now. The switch back retiles the mutated one.
    if (key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::handoffReceive(const HandoffContext& ctx)
{
    const QString windowId = canonicalizeForLookup(ctx.windowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(ctx.toScreenId)) {
        return;
    }
    // Same preview hygiene as handoffRelease: an arriving window that a
    // live preview still names would be double-placed at commit.
    dropClosedWindowFromDragPreview(windowId);
    PhosphorEngine::PlacementStateKey key = currentKeyForScreen(ctx.toScreenId);
    if (ctx.toDesktop > 0) {
        key.desktop = ctx.toDesktop;
    }
    // Defence-in-depth single-owner guard: the daemon releases the source
    // first on every current path, but a window still tracked in ANOTHER
    // scroll context here would end up held by two states with the reverse
    // map pointing at only one. Migrate it out (same sweep as
    // windowOpened's context migration) before inserting.
    PhosphorEngine::PlacementStateKey staleKey;
    if (ScrollState* staleState = stateForWindow(windowId, &staleKey); staleState && staleKey != key) {
        const ScrollLayoutParams staleParams = layoutParamsForScreen(staleKey.screenId);
        const bool staleWasFloating = staleState->isFloating(windowId);
        staleState->strip().takeWindow(windowId, staleParams);
        staleState->removeFloating(windowId);
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        if (staleWasFloating) {
            // Same announcement as windowOpened's migration: a silently
            // dropped float bit leaves signal-driven subscribers believing
            // the window floats while the receive tiles it (the
            // wasFloating branch below re-announces true when it applies).
            Q_EMIT windowFloatingStateSynced(windowId, false, staleKey.screenId);
        }
        // Background-context guard, same terms as the sibling sites: the
        // stale context is usually NOT the one on screen.
        if (staleKey == currentKeyForScreen(staleKey.screenId)) {
            scheduleRetileForScreen(staleKey.screenId);
        }
        Q_EMIT placementChanged(staleKey.screenId);
    }
    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    if (state->containsWindow(windowId)) {
        // Already here — nothing to insert, but the reverse map may still
        // name the stale context the migration above just emptied, which
        // would leave the window tracked at a key that no longer holds it.
        m_states.setKeyForWindow(windowId, key);
        return;
    }
    // Re-adoption starts from a blank rect memory: handoffRelease/windowClosed
    // only retain m_lastAppliedRect long enough to survive the close/capture
    // window, and a leftover entry would defeat applyLayout's emit-on-change
    // gate so no windowsTiled batch ever fires for the re-adopted window. A
    // leftover parked edge is equally foreign to the adopting strip.
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    if (ctx.wasFloating) {
        state->addFloating(windowId);
        // The window arrives floating and so is never a strip tile here: the
        // FloatRestore entry is the only place its clamp can live, and the
        // source engine just handed it over in ctx.minSize. Without the seed
        // this engine answers "unknown" for a window it manages, and a later
        // unfloat re-inserts it unclamped.
        seedFloatRestoreForOpen(windowId, ctx.minSize.width(), ctx.minSize.height());
        // The float is scroll-managed from here (autotile's receive marks the
        // same way, through the daemon's passive float sync): without the
        // marker a later mode transition treats it as a snap float and
        // poisons the snap slot with the arrival frame.
        m_scrollFloatedWindows.insert(windowId);
        m_states.setKeyForWindow(windowId, key);
        Q_EMIT windowFloatingStateSynced(windowId, true, ctx.toScreenId);
        // The screen's placement changed too (managed set grew), even
        // though no strip geometry moved.
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(ctx.toScreenId);
    ColumnWidth width = effectiveDefaultColumnWidth(ctx.toScreenId);
    if (ctx.sourceGeometry.isValid()) {
        width = ColumnWidth::makeFixed(ctx.sourceGeometry.width());
    }
    // Entry position comes from the CALLER: the cross-mode dispatcher
    // derives insertIndex from the crossing direction (0 when entering from
    // the strip's left edge), and -1 appends at the right end. This
    // function has no direction of its own to derive an edge from.
    const int columnIdx = (ctx.insertIndex >= 0) ? ctx.insertIndex : state->strip().columnCount();
    if (state->strip().insertWindowAt(columnIdx, windowId, width, effectiveDefaultColumnDisplay(ctx.toScreenId),
                                      params)) {
        // Seed the source engine's last-known min size so the first relayout
        // clamps correctly instead of waiting a refuse/re-discover round-trip.
        if (ctx.minSize.width() > 0 || ctx.minSize.height() > 0) {
            state->strip().setWindowMinimumSize(windowId, ctx.minSize.width(), ctx.minSize.height());
        }
        m_states.setKeyForWindow(windowId, key);
        const bool isCurrentContext = key == currentKeyForScreen(ctx.toScreenId);
        if (isCurrentContext) {
            state->strip().focusWindow(windowId, params);
            applyLayout(ctx.toScreenId, false);
        }
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    // The insert refused (an empty id, or a window this strip already holds —
    // both ruled out above, so this is a real inconsistency). Every sibling
    // insert site logs its refusal; a silent one here leaves the window
    // released by the source engine and adopted by nobody, with no trace.
    qCWarning(lcScrollEngine) << "handoffReceive: insert refused for" << windowId << "on" << ctx.toScreenId
                              << "— the window is released by its source engine, unadopted here, and its"
                              << "tracking key has been dropped";
    // Do not leave a reverse-map key for a window no structure holds — the
    // stale-context migration above already ran takeWindow, so the key would
    // name a context that no longer contains it. Same removal, and the same
    // reason, as insertOpenedWindow's refusal path.
    m_states.removeWindow(windowId);
}

// ── Unified placement capture ───────────────────────────────────────────────

std::optional<PhosphorEngine::WindowPlacement> ScrollEngine::capturePlacement(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // A window mid-drag-insert has NO capturable placement, and answering
    // anyway silently destroys the one it had. Under DETACH-ONCE, begin drops
    // the window from the floating set and out of the strip while KEEPING it
    // tracked — so the else arm below would read isFloating()==false, take the
    // tiled branch, and record columnOfWindow() == -1. A pre-drag FLOATING
    // window would have its floating record overwritten with tiled/order=-1;
    // insertOpenedWindow's restore ladder then never reaches the floating arm,
    // its `order >= 0` test fails too, and the window reopens tiled with its
    // remembered float-back gone.
    //
    // This is reachable on an ordinary hold: the save timer is restarted by
    // markDirty, which this engine's own placementChanged triggers, and begin
    // emits that when the neighbours close up. Returning nullopt leaves the
    // pre-drag record intact, which is the same answer the adaptor already
    // documents for an unmanaged window.
    if (m_dragInsertPreview && m_dragInsertPreview->windowId == windowId) {
        return std::nullopt;
    }

    PhosphorEngine::PlacementStateKey key;
    const ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return std::nullopt;
    }
    PhosphorEngine::WindowPlacement placement;
    placement.windowId = windowId;
    placement.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
    placement.screenId = key.screenId;
    placement.virtualDesktop = key.desktop;
    placement.activity = key.activity;

    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(windowId)) {
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
    } else {
        slot.state = PhosphorEngine::WindowPlacement::stateTiled();
        // The COLUMN index at capture time, recorded as context only. Nothing
        // consumes it for placement: the reopen path takes floating slots
        // only, and a tiled slot's job is to stand as the exact-final
        // evidence that the window closed tiled.
        slot.order = state->strip().columnOfWindow(windowId);
    }
    placement.engines.insert(engineId(), slot);
    return placement;
}

} // namespace PhosphorScrollEngine
