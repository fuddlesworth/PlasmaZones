// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

#include <QMetaObject>

namespace PhosphorScrollEngine {

ScrollEngine::ScrollEngine(PhosphorEngine::IWindowTrackingService* windowTracker,
                           PhosphorScreens::ScreenManager* screenManager, QObject* parent)
    : PhosphorEngine::PlacementEngineBase(parent)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
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
        m_states.removeStatesIf(
            [&currentKey](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
                return key == currentKey;
            },
            [this, &releasedWindows](const PhosphorEngine::PlacementStateKey& key, ScrollState* state) {
                // Mode reassignment: remember the strip's structure so a
                // cycle back to Scrolling rebuilds it (stacks, widths,
                // tabbed flags) instead of a default one-window-per-column
                // strip. Captured BEFORE the release strips the state.
                stashStripStructure(key, state);
                releaseScreenState(state, releasedWindows);
            });
        releasedScreens.insert(screenId);
        m_context.removeScreen(screenId);
        // Even a STATELESS leaving screen (seed pushed before any window
        // arrived) must drop its per-screen bookkeeping — the state-driven
        // sweep in releaseScreenState never ran for it. The tab-strip clear
        // is latched, so a second call is a no-op. m_perScreenOverrides is
        // NOT swept here: the daemon clears a departing screen's overrides
        // itself, right after setActiveScreens in updateScrollingScreens.
        // pruneStatesForRemovedScreen is the output-removal purge.
        m_pendingInitialOrder.remove(screenId);
        m_consumedInitialOrder.remove(screenId);
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

void ScrollEngine::releaseScreenState(ScrollState* state, QStringList& releasedWindows)
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
    for (const QString& windowId : windows) {
        m_floatRestore.remove(windowId);
    }
    releasedWindows.append(windows);
    // Per-screen bookkeeping dies with the state: a stale seed must not
    // replay on re-entry, and the tab-strip overlay must be told to clear —
    // no relayout will ever run for a departed screen to do it.
    m_pendingInitialOrder.remove(screenId);
    m_consumedInitialOrder.remove(screenId);
    // Latch and payload cleared inline (plain containers, safe), but the
    // broadcast is DEFERRED: this function runs from inside
    // PerScreenStates::removeStatesIf's iteration over m_states, and a
    // consumer slot that touched the engine's state map synchronously would
    // invalidate the live iterator. The other seven clearTabStripsForScreen
    // call sites are outside any iteration and emit directly.
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

ScrollEngine::StashedStrip ScrollEngine::buildStashFromState(const ScrollState* state) const
{
    StashedStrip out;
    if (!state || state->strip().isEmpty()) {
        return out;
    }
    for (const Column& col : state->strip().columns()) {
        StashedColumn sc;
        sc.width = col.width;
        sc.display = col.display;
        sc.activeWindowId = col.tiles.value(col.activeTileIdx).windowId;
        for (const Tile& tile : col.tiles) {
            sc.tiles.append({tile.windowId, tile.height, tile.minimized});
        }
        if (!sc.tiles.isEmpty()) {
            out.columns.append(sc);
        }
    }
    // Focus and view travel with the structure: without them every round
    // trip re-anchored the strip on whichever window arrived first.
    out.focusedWindowId = state->strip().activeWindowId();
    out.viewAnchor = state->strip().viewAnchor();
    return out;
}

void ScrollEngine::stashStripStructure(const PhosphorEngine::PlacementStateKey& key, const ScrollState* state)
{
    StashedStrip stash = buildStashFromState(state);
    if (stash.isEmpty()) {
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
    if (m_stripStashConsumed.value(key).contains(windowId)) {
        return false;
    }
    StashedStrip& stashStrip = it.value();
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
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        const QString appPrefix = appId.isEmpty() ? QString() : appId + QLatin1Char('|');
        if (!appPrefix.isEmpty()) {
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
    };
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
    state->strip().setWindowHeightIntent(windowId, sc.tiles.at(tileIdx).height);
    // Re-assert the column's stashed ACTIVE tile: every insert makes the
    // arriving tile active, so a tabbed column's shown tab would otherwise
    // be whichever sibling announced last.
    if (const QString tab = stash.at(colIdx).activeWindowId;
        !tab.isEmpty() && tab != windowId && state->strip().columnOfWindow(tab) >= 0) {
        state->strip().focusWindow(tab, params);
    }
    // The stashed FOCUS follows its window, not the arrival order: without
    // this the first arrival kept the focus it won on the empty strip and
    // every mode round trip re-anchored on an arbitrary window. The anchor
    // is restored after the focus so the user's actual view wins over the
    // focus change's centering-policy reanchor (clamped against the partial
    // strip now; later arrivals re-clamp as the strip grows).
    if (windowId == stashStrip.focusedWindowId) {
        state->strip().focusWindow(windowId, params);
        state->strip().restoreViewAnchor(stashStrip.viewAnchor, params);
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
    // get the "[]" broadcast, so plain relayouts never spam the overlay.
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
    const auto parsePresets = [](const QStringList& raw, const QList<qreal>& fallback) {
        QList<qreal> out;
        for (const QString& entry : raw) {
            bool ok = false;
            const qreal v = entry.trimmed().toDouble(&ok);
            // Same floor as every other proportion producer: a preset below it
            // resolves to a sliver no window can honour, and the preset cycle
            // would silently offer a width the width setter refuses.
            if (ok && v >= MinColumnWidthFraction && v <= 1.0) {
                out.append(v);
            }
        }
        return out.isEmpty() ? fallback : out;
    };
    // KEEP IN SYNC with ScrollLayoutParams' member defaults (ScrollTypes.h).
    const QList<qreal> defaults{1.0 / 3.0, 0.5, 2.0 / 3.0};
    m_presetColumnWidths = parsePresets(settings->scrollingPresetColumnWidths(), defaults);
    m_presetWindowHeights = parsePresets(settings->scrollingPresetWindowHeights(), defaults);

    const int center = settings->scrollingCenterFocusedColumn();
    m_centerFocusedColumn =
        (center >= 0 && center <= 2) ? static_cast<CenterFocusedColumn>(center) : CenterFocusedColumn::Never;
    m_alwaysCenterSingleColumn = settings->scrollingAlwaysCenterSingleColumn();

    const auto widthKind = static_cast<DefaultWidthKind>(settings->scrollingDefaultColumnWidthKind());
    const qreal widthValue = settings->scrollingDefaultColumnWidthValue();
    m_defaultWidthClientDecides = (widthKind == DefaultWidthKind::ClientDecides);
    if (widthKind == DefaultWidthKind::Fixed) {
        m_defaultColumnWidth = ColumnWidth::makeFixed(qMax(1, qRound(widthValue)));
    } else if (widthKind == DefaultWidthKind::Preset) {
        // Preset list is parsed above, so the clamp is against the live
        // list; makePreset re-clamps at relayout if the list later shrinks.
        m_defaultColumnWidth = ColumnWidth::makePreset(
            qBound(0, settings->scrollingDefaultColumnWidthPresetIndex(), int(m_presetColumnWidths.size()) - 1));
    } else {
        m_defaultColumnWidth = ColumnWidth::makeProportion(qBound<qreal>(MinColumnWidthFraction, widthValue, 1.0));
    }
    const int display = settings->scrollingDefaultColumnDisplay();
    m_defaultColumnDisplay = (display == 1) ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;

    // Default window height: the config vocabulary IS WindowHeight::Kind
    // (Auto/Fixed/Preset, see DefaultHeightKind), so a guarded cast is fine.
    const int heightKind = settings->scrollingDefaultWindowHeightKind();
    if (heightKind == static_cast<int>(DefaultHeightKind::Fixed)) {
        m_defaultWindowHeight = WindowHeight::makeFixed(qMax(1, qRound(settings->scrollingDefaultWindowHeightValue())));
    } else if (heightKind == static_cast<int>(DefaultHeightKind::Preset)) {
        m_defaultWindowHeight = WindowHeight::makePreset(
            qBound(0, settings->scrollingDefaultWindowHeightPresetIndex(), int(m_presetWindowHeights.size()) - 1));
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

    // Re-resolve every active strip against the new parameters.
    for (const QString& screenId : std::as_const(m_scrollingScreens)) {
        scheduleRetileForScreen(screenId);
    }
}

// ── Per-context rule overrides ──────────────────────────────────────────────

void ScrollEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    if (m_perScreenOverrides.value(screenId) == overrides) {
        return;
    }
    m_perScreenOverrides.insert(screenId, overrides);
    scheduleRetileForScreen(screenId);
}

void ScrollEngine::clearPerScreenConfig(const QString& screenId)
{
    if (m_perScreenOverrides.remove(screenId) > 0) {
        scheduleRetileForScreen(screenId);
    }
}

CenterFocusedColumn ScrollEngine::effectiveCenterFocusedColumn(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::centerFocusedColumn());
    if (it != overrides.constEnd()) {
        const int mode = it->toInt();
        if (mode >= 0 && mode <= 2) {
            return static_cast<CenterFocusedColumn>(mode);
        }
    }
    return m_centerFocusedColumn;
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QString& screenId) const
{
    // Validate-then-fall-back, like its two siblings: an out-of-range rule
    // value is not silently reshaped into a legal one, because a rule author
    // who wrote 0.001 asked for something this engine cannot do and should
    // get the configured default rather than an arbitrary clamp result. (The
    // per-WINDOW open rule qBounds instead — that value reaches a single
    // column the user is watching open, so nudging it into range is the less
    // surprising answer there.)
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    // Rule channel first (rule > per-screen setting > global): the bare
    // fraction key is written only by the rule cascade.
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidth());
    if (it != overrides.constEnd()) {
        const qreal fraction = it->toDouble();
        if (fraction >= MinColumnWidthFraction && fraction <= 1.0) {
            return ColumnWidth::makeProportion(fraction);
        }
    }
    // Settings channel: the kind-aware trio from the per-screen override map.
    const auto kindIt = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidthKind());
    if (kindIt != overrides.constEnd()) {
        const int kind = kindIt->toInt();
        const qreal value = overrides.value(ScrollPerScreenKeys::defaultColumnWidthValue()).toDouble();
        if (kind == static_cast<int>(DefaultWidthKind::Fixed) && value >= 1.0) {
            return ColumnWidth::makeFixed(qRound(value));
        }
        if (kind == static_cast<int>(DefaultWidthKind::Preset)) {
            return ColumnWidth::makePreset(
                qBound(0, overrides.value(ScrollPerScreenKeys::defaultColumnWidthPresetIndex()).toInt(),
                       int(m_presetColumnWidths.size()) - 1));
        }
        if (kind == static_cast<int>(DefaultWidthKind::Proportion) && value >= 0.05 && value <= 1.0) {
            return ColumnWidth::makeProportion(value);
        }
        // ClientDecides (and malformed pairs) fall through to the global —
        // the open path handles client-decides via screenPinsWidth.
    }
    return m_defaultColumnWidth;
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QString& screenId, const QRect& workArea) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    // Rule channel: a bare work-area fraction, committed as Fixed pixels
    // against the CURRENT work area (same resolution the adjust verbs use).
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeight());
    if (it != overrides.constEnd() && workArea.height() > 0) {
        const qreal fraction = it->toDouble();
        if (fraction > 0.0 && fraction <= 1.0) {
            return WindowHeight::makeFixed(qMax(1, qRound(fraction * workArea.height())));
        }
    }
    // Settings channel: the kind trio.
    const auto kindIt = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeightKind());
    if (kindIt != overrides.constEnd()) {
        const int kind = kindIt->toInt();
        if (kind == static_cast<int>(DefaultHeightKind::Fixed)) {
            const qreal value = overrides.value(ScrollPerScreenKeys::defaultWindowHeightValue()).toDouble();
            if (value >= 1.0) {
                return WindowHeight::makeFixed(qRound(value));
            }
        } else if (kind == static_cast<int>(DefaultHeightKind::Preset)) {
            return WindowHeight::makePreset(
                qBound(0, overrides.value(ScrollPerScreenKeys::defaultWindowHeightPresetIndex()).toInt(),
                       int(m_presetWindowHeights.size()) - 1));
        } else if (kind == static_cast<int>(DefaultHeightKind::Auto)) {
            return WindowHeight{};
        }
    }
    return m_defaultWindowHeight;
}

ScrollInsertPosition ScrollEngine::effectiveInsertPosition(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::insertPosition());
    if (it != overrides.constEnd()) {
        const int pos = it->toInt();
        if (pos >= static_cast<int>(ScrollInsertPosition::RightOfActive)
            && pos <= static_cast<int>(ScrollInsertPosition::IntoActiveColumn)) {
            return static_cast<ScrollInsertPosition>(pos);
        }
    }
    return m_insertPosition;
}

bool ScrollEngine::effectiveRespectMinimumSize(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::respectMinimumSize());
    if (it != overrides.constEnd()) {
        return it->toBool();
    }
    return m_respectMinimumSize;
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QString& screenId) const
{
    // Validate-then-fall-back, same terms as the two siblings. Reading "any
    // value that is not 1" as Normal meant a garbage override (or a future
    // display kind this build does not know) silently overrode the user's
    // configured default with Normal instead of leaving it alone.
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnDisplay());
    if (it != overrides.constEnd()) {
        const int display = it->toInt();
        if (display == static_cast<int>(ColumnDisplay::Normal) || display == static_cast<int>(ColumnDisplay::Tabbed)) {
            return static_cast<ColumnDisplay>(display);
        }
    }
    return m_defaultColumnDisplay;
}

void ScrollEngine::retile(const QString& screenId)
{
    if (screenId.isEmpty()) {
        for (const QString& sid : std::as_const(m_scrollingScreens)) {
            applyLayout(sid);
        }
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
