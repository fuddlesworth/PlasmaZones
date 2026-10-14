// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// What the mode-round-trip stash gives up when a window is gone from the
// context a stash entry remembers: the aliveness sweep pruneStaleWindows runs
// for a window that closed, and the leave arms for a window that is still
// open but has moved to another monitor, desktop or activity (F648, F696).
//
// A leave ERASES the window's tile from the entry, as the aliveness sweep does
// for a window that closed. Marking it consumed would not do: a consumed tile
// is still written by serializeStripState, deliberately (a claimed-then-closed
// tile writes a fresh lease, see coTenantClaimDoesNotRenewSiblingLease), so a
// restart would stage the column again. The erase is safe for
// restoreFromStripStash's positional counts for the reason the aliveness sweep
// gives: they test LIVE strip membership, and the window that left is not on
// that strip. Split out of engine_context.cpp, which was at the file-size
// ceiling.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>

#include "evacueepark_p.h"
#include "scrollenginelogging.h"

#include <QPoint>
#include <QRect>

#include <algorithm>

namespace PhosphorScrollEngine {

using PhosphorEngine::PlacementStateKey;

void ScrollEngine::forgetStashedWindowWhere(const QString& windowId,
                                            const std::function<bool(const PlacementStateKey&)>& left)
{
    if (windowId.isEmpty() || !left) {
        return;
    }
    // A readoptParked grant goes on the same leave. No parked-key exemption:
    // the grant is this window's alone, and the window was already returned
    // when it was handed out.
    if (m_evacueePark) {
        m_evacueePark->dropGrantsWhere(windowId, left);
    }
    QSet<QString> touchedScreens;
    for (auto it = m_stripStash.begin(); it != m_stripStash.end();) {
        // A tile a parked context still owes this window is the evacuee
        // park's to give up: KWin moving the window off the dead output is not
        // the user leaving it (dropParked marks it consumed when the window is
        // touched). Per window, not per key: the key stays parked while other
        // windows' grants are out, and a window already returned that leaves
        // gives up its tile like any other.
        if (!left(it.key()) || (m_evacueePark && m_evacueePark->owesUnder(it.key(), windowId))) {
            ++it;
            continue;
        }
        bool erased = false;
        for (auto colIt = it->columns.begin(); colIt != it->columns.end();) {
            erased |= colIt->tiles.removeIf([&windowId](const StashedTile& tile) {
                return tile.windowId == windowId;
            }) > 0;
            if (colIt->activeWindowId == windowId) {
                colIt->activeWindowId.clear();
            }
            if (colIt->heightOwnerId == windowId) {
                colIt->heightOwnerId.clear();
            }
            colIt = colIt->tiles.isEmpty() ? it->columns.erase(colIt) : std::next(colIt);
        }
        if (!erased) {
            ++it;
            continue;
        }
        if (it->focusedWindowId == windowId) {
            it->focusedWindowId.clear();
        }
        touchedScreens.insert(it.key().screenId);
        qCInfo(lcScrollEngine) << "forgetStashedWindowWhere:" << windowId << "gives up its stashed column on desktop"
                               << it.key().desktop << "of" << it.key().screenId;
        // The consumed set stays a subset of the tiles, and the entry retires
        // on the aliveness sweep's terms: once every tile left is consumed, or
        // when none is left.
        auto consumedIt = m_stripStashConsumed.find(it.key());
        if (consumedIt != m_stripStashConsumed.end()) {
            consumedIt->remove(windowId);
        }
        const int remaining = it->tileCount();
        if (remaining == 0 || (consumedIt != m_stripStashConsumed.end() && consumedIt->size() >= remaining)) {
            m_stripStashConsumed.remove(it.key());
            it = m_stripStash.erase(it);
        } else {
            ++it;
        }
    }
    // The saved strip structure lost the tile, so the strips are dirty.
    for (const QString& screenId : std::as_const(touchedScreens)) {
        Q_EMIT placementChanged(screenId);
    }
}

void ScrollEngine::releaseWindowOffScreen(const QString& windowId, const QString& keepScreenId)
{
    PlacementEngineBase::releaseWindowOffScreen(windowId, keepScreenId);
    // The base releases only a TRACKED hold. A window whose strip was stashed
    // by a mode change is tracked nowhere here, and the stash entry is its
    // only memory of that screen: a move to another monitor gives it up, or
    // the strip's return to scrolling would pull the window back into its old
    // column (F648). Compared at the virtual-screen level like the base.
    forgetStashedWindowWhere(canonicalizeForLookup(windowId), [&keepScreenId](const PlacementStateKey& key) {
        return keepScreenId.isEmpty() || key.screenId != keepScreenId;
    });
}

void ScrollEngine::sweepStripStashForAliveness(const QSet<QString>& aliveWindowIds)
{
    // The strip stash needs pruneStaleWindows' treatment too, but keyed on
    // ALIVENESS rather than on its `dead` list — same reasoning as its
    // m_lastAppliedRect sweep. A stash tile is unreachable by the `dead` loop,
    // which only walks the seed lists. (Not because a stashed window is never
    // tracked — one stashed for context K1 can be live and tracked in a
    // different context K2, and would then appear in `dead`. That duplication
    // is exactly what serializeStripState's live-wins filter exists for.) And
    // the stash is keyed by CONTEXT, so the existing sweepStripStash calls
    // (desktop, activity and output prunes) never reach a stash whose context
    // is still perfectly live. Between the two, a window that closes while its
    // screen sits in another mode was reachable by nothing: its tile could
    // never satisfy the all-consumed drop condition, so the entry was immortal
    // — re-walked on every later open for that context, written out by
    // serializeStripState, and, through the cross-session appId claim, able to
    // hand an unrelated same-app window the dead tile's slot, width and display.
    //
    // The empty-alive-set bail protects the STASH specifically — the one
    // collection that cannot be rebuilt once wiped. It is a second, narrower
    // belt: the boundary that actually refuses an empty alive set is the
    // adaptor (WindowTrackingAdaptor::pruneStaleWindows returns before reaching
    // the engine), so in production the destructive dead loop in
    // pruneStaleWindows never runs against an empty set. An embedder calling
    // that exported API directly with an empty set WOULD tear down every strip
    // while the stash survived; hoisting the bail to cover the dead loop is a
    // public-API behaviour change deliberately not taken there.
    //
    // A tile staged straight from the persisted blob is EXEMPT until it is
    // claimed. Its id belongs to last session, so no alive set can contain it
    // and the sweep would read it as closed — wiping the structure/focus/
    // anchor restore on the very first prune after login, since the effect
    // fires one at bringup right after the daemon stages it. The exemption is
    // PER TILE and lives in the removeIf below: an entry-wide lift on the
    // first claim let this prune erase an unclaimed co-tenant's slot, which is
    // precisely the case StashedTile::unclaimedSessions exists to lease out
    // gradually instead. See StashedTile::stagedFromPersistence.
    if (aliveWindowIds.isEmpty()) {
        return;
    }
    // A staged tile whose id IS alive came from a daemon restart, which keeps
    // window ids. Its window was moved to another monitor while the daemon was
    // down when the frame the compositor reported for it lies on another
    // screen, and it gives up that column like a live move would (F648). The
    // effect pushes every window's metadata before its first alive report, so
    // the registry holds the frame by now. A frame on no screen at all is a
    // strip window parked off-canvas, which is still on its own screen.
    const auto frameOnAnotherScreen = [this](const QString& windowId, const QString& screenId) {
        if (!m_windowRegistry) {
            return false;
        }
        const auto meta = m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        if (!meta || !meta->positionX || !meta->positionY || !meta->width || !meta->height) {
            return false;
        }
        const QPoint centre = QRect(*meta->positionX, *meta->positionY, *meta->width, *meta->height).center();
        if (m_screenManager) {
            const QString at = m_screenManager->effectiveScreenAt(centre);
            return !at.isEmpty() && at != screenId;
        }
        const QRect own = m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect();
        if (!own.isValid() || own.contains(centre) || !m_allScreenGeometriesProvider) {
            return false;
        }
        const QList<QRect> all = m_allScreenGeometriesProvider();
        return std::any_of(all.cbegin(), all.cend(), [&centre](const QRect& rect) {
            return rect.contains(centre);
        });
    };
    QSet<QString> movedAwayScreens;
    for (auto stashIt = m_stripStash.begin(); stashIt != m_stripStash.end();) {
        // Captured before the sweep below can empty the entry, because the
        // zero-tile reap needs to tell two look-alike shapes apart: an
        // entry BORN cursor-only (stashStripStructure's all-floated arm,
        // which never had columns) is a live carrier and must survive to
        // the arrival that consumes it, while an entry that DECAYED to
        // zero tiles here is one whose windows are demonstrably gone —
        // that context comes back to an empty strip and is supposed to
        // restart its blueprint, so retaining its cursor would be the bug.
        const bool bornCursorOnly = stashIt->columns.isEmpty();
        const PlacementStateKey key = stashIt.key();
        const bool parked = isEvacueeParkedKey(key);
        // Dead tiles are erased outright, consumed ones included, and so is a
        // staged tile whose live window was moved away (above). Retaining
        // a consumed-then-closed tile to protect restoreFromStripStash's
        // positional math is NOT necessary: both of its counters test LIVE
        // STRIP membership (columnOfWindow / indexOfWindow), so a dead tile
        // answers -1 and contributes 0 either way — erasing it removes a
        // zero-contribution entry and decrements colIdx by the same one.
        // Retaining them only inflates `remaining` below, which is what
        // keeps an entry from ever reaching the all-consumed drop.
        for (auto colIt = stashIt->columns.begin(); colIt != stashIt->columns.end();) {
            colIt->tiles.removeIf([&](const StashedTile& tile) {
                // The per-tile persistence exemption, and the ONLY one:
                // an unclaimed staged tile names last session's id, which
                // no alive set can hold.
                if (!tile.stagedFromPersistence) {
                    return !aliveWindowIds.contains(tile.windowId);
                }
                const bool movedAway = !parked && aliveWindowIds.contains(tile.windowId)
                    && frameOnAnotherScreen(tile.windowId, key.screenId);
                if (movedAway) {
                    movedAwayScreens.insert(key.screenId);
                }
                return movedAway;
            });
            colIt = colIt->tiles.isEmpty() ? stashIt->columns.erase(colIt) : std::next(colIt);
        }
        if (!stashIt->focusedWindowId.isEmpty()) {
            // The focus is dropped only when NO surviving tile still
            // names it — an id that is merely absent from the alive set
            // may belong to a staged tile the sweep just exempted.
            bool focusSurvives = false;
            for (const StashedColumn& col : std::as_const(stashIt->columns)) {
                for (const StashedTile& tile : col.tiles) {
                    if (tile.windowId == stashIt->focusedWindowId) {
                        focusSurvives = true;
                        break;
                    }
                }
                if (focusSurvives) {
                    break;
                }
            }
            if (!focusSurvives) {
                stashIt->focusedWindowId.clear();
            }
        }
        // Keep the consumed set a subset of the (now shorter) tile list so
        // the all-consumed drop condition below stays exact.
        auto stashConsumedIt = m_stripStashConsumed.find(stashIt.key());
        if (stashConsumedIt != m_stripStashConsumed.end()) {
            stashConsumedIt->removeIf([&aliveWindowIds](const QString& consumed) {
                return !aliveWindowIds.contains(consumed);
            });
        }
        const int remaining = stashIt->tileCount();
        // A cursor-only carrier has no tiles by construction, so the
        // zero-tile arm would reap it on the bring-up prune the effect
        // fires before any window has announced — retiring the entry
        // before the arrival it exists for. Scoped to entries that were
        // ALREADY column-less on entry AND actually carry a cursor;
        // everything else reaps exactly as before.
        const bool cursorOnlyCarrier = bornCursorOnly && stashIt->blueprintCursor > 0;
        if (!cursorOnlyCarrier
            && (remaining == 0
                || (stashConsumedIt != m_stripStashConsumed.end() && stashConsumedIt->size() >= remaining))) {
            m_stripStashConsumed.remove(stashIt.key());
            stashIt = m_stripStash.erase(stashIt);
        } else {
            ++stashIt;
        }
    }
    for (const QString& screenId : std::as_const(movedAwayScreens)) {
        Q_EMIT placementChanged(screenId);
    }
}

} // namespace PhosphorScrollEngine
