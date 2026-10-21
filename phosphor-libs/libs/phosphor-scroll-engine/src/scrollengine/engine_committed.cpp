// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// What a client's own commits teach the engine about its window's extent along
// the strip (reportCommittedSize), the per-window floor and peek memory kept
// from them (m_committedExtents), and the two things that keep a learned floor
// honest afterwards: the sweep applyLayout runs (settleCommittedFloors) and the
// oversized verdict a floor shares with a declared minimum
// (outgrowsWorkArea). The strip never holds any of this; it reads the floors
// through ScrollLayoutParams::committedFloor, which layoutParamsForKey points
// at committedFloorFor.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

namespace {

/// The half of @p extent a strip running along @p axis reads.
CommittedMainExtent& extentFor(CommittedExtent& extent, const StripAxis& axis)
{
    return axis.isHorizontal() ? extent.horizontal : extent.vertical;
}

const CommittedMainExtent& extentFor(const CommittedExtent& extent, const StripAxis& axis)
{
    return axis.isHorizontal() ? extent.horizontal : extent.vertical;
}

/// The extent along the strip the column holding @p windowId offers that
/// window's tile under @p params (the column's extent less the tab
/// indicator's main-axis reservation), or -1 when @p strip does not hold it.
int tileMainOfferPx(const ScrollStrip& strip, const QString& windowId, const ScrollLayoutParams& params)
{
    const int colIdx = strip.columnOfWindow(windowId);
    if (colIdx < 0) {
        return -1;
    }
    return strip.windowColumnMainExtentPx(windowId, params)
        - ScrollStrip::tabbedMainReservationPx(strip.columns().at(colIdx), params);
}

/// End one orientation's floor outright, with the give-back history that
/// belonged to it. The peek memory stays here: what the client last answered
/// is still true after its column stops being held to it, for as long as the
/// column still offers at least that much (settleCommittedFloors ends it once
/// the column offers less).
void dropFloor(CommittedMainExtent& extent)
{
    extent.floorPx = 0;
    extent.gaveBack = false;
    extent.pinned = false;
}

} // namespace

ScrollLayoutParams ScrollEngine::withoutCommittedFloorOf(const ScrollLayoutParams& params, const QString& windowId)
{
    ScrollLayoutParams masked = params;
    masked.committedFloor = [inner = params.committedFloor, windowId](const QString& id) {
        return (id == windowId || !inner) ? QSize(0, 0) : inner(id);
    };
    return masked;
}

QSize ScrollEngine::committedFloorFor(const QString& windowId) const
{
    // The relayout asks this per tile, several times per pass, and almost
    // every strip holds no floor at all: answer that case without hashing.
    if (m_committedExtents.isEmpty()) {
        return QSize(0, 0);
    }
    const auto it = m_committedExtents.constFind(windowId);
    if (it == m_committedExtents.cend()) {
        return QSize(0, 0);
    }
    return QSize(it->horizontal.floorPx, it->vertical.floorPx);
}

int ScrollEngine::committedPeekPx(const QString& windowId, const StripAxis& axis) const
{
    const auto it = m_committedExtents.constFind(windowId);
    if (it == m_committedExtents.cend()) {
        return 0;
    }
    // The floor too, though every path that sets one sets the memory with it:
    // the peek must never protect less than the column itself is held to.
    const CommittedMainExtent& extent = extentFor(*it, axis);
    return qMax(extent.peekPx, extent.floorPx);
}

void ScrollEngine::forgetCommittedExtents(const QString& windowId)
{
    m_committedExtents.remove(windowId);
}

bool ScrollEngine::dropCommittedForUserResize(const QString& windowId, const StripAxis& axis)
{
    const auto it = m_committedExtents.find(windowId);
    if (it == m_committedExtents.end()) {
        return false;
    }
    CommittedMainExtent& extent = extentFor(*it, axis);
    const bool hadFloor = extent.floorPx > 0;
    extent = CommittedMainExtent{};
    if (it->isEmpty()) {
        m_committedExtents.erase(it);
    }
    return hadFloor;
}

void ScrollEngine::dropCommittedForLoweredMinimum(const QString& windowId, const QSize& oldMin, const QSize& newMin)
{
    const auto it = m_committedExtents.find(windowId);
    if (it == m_committedExtents.end()) {
        return;
    }
    // Per orientation, by the axis that is its MAIN one: a horizontal strip's
    // floor is a width, so it answers to the width minimum. A raised minimum
    // takes nothing: the floor it may now sit under still describes the
    // client, and the declared minimum simply outranks it while it does. An
    // unknown old minimum (an invalid size) compares as -1 and lowers nothing.
    // Whether a drop moved anything is the caller's to measure: a floor in
    // the orientation the window's strip does not run, a floating window's
    // floor, or one its column already outgrows changes no layout.
    if (newMin.width() < oldMin.width()) {
        it->horizontal = CommittedMainExtent{};
    }
    if (newMin.height() < oldMin.height()) {
        it->vertical = CommittedMainExtent{};
    }
    if (it->isEmpty()) {
        m_committedExtents.erase(it);
    }
}

bool ScrollEngine::outgrowsWorkArea(const ScrollState& state, const QString& windowId,
                                    const ScrollLayoutParams& params) const
{
    const ScrollStrip& strip = state.strip();
    const int colIdx = strip.columnOfWindow(windowId);
    if (!params.workArea.isValid() || colIdx < 0) {
        return false;
    }
    // The declared minimum on either axis, whatever respectMinimumSize says:
    // the open-time oversized policy (insertOpenedWindow) ignores the flag,
    // and so has every revisit of it.
    const QSize declared = strip.windowMinimumSize(windowId);
    if (declared.width() > params.workArea.width() || declared.height() > params.workArea.height()) {
        return true;
    }
    // A committed floor only under the flag, the only time one is read. A
    // maximized-to-edges column reads none, and its client is answering a
    // declared extent rather than a column.
    const Column& column = strip.columns().at(colIdx);
    if (!params.respectMinimumSize || column.maximizedToEdges) {
        return false;
    }
    const int floorPx = extentFor(m_committedExtents.value(windowId), params.axis).floorPx;
    return floorPx > 0
        && floorPx + ScrollStrip::tabbedMainReservationPx(column, params) > params.axis.mainSize(params.workArea);
}

bool ScrollEngine::floatIfOutgrowsWorkArea(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                           const QString& windowId, const char* reason, bool applyAfter)
{
    if (!state) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForKey(key);
    if (!outgrowsWorkArea(*state, windowId, params)) {
        return false;
    }
    qCInfo(lcScrollEngine) << reason << windowId << "min" << state->strip().windowMinimumSize(windowId)
                           << "committed floor" << committedFloorFor(windowId) << "outgrew work area"
                           << params.workArea.size() << "on" << key.screenId
                           << "— floating (open-time oversized policy)";
    return floatWindowInternal(state, key, windowId, key.screenId, FloatAnnounce::Active, /*fullscreenHold=*/false,
                               applyAfter);
}

bool ScrollEngine::settleCommittedFloors(ScrollState& state, const PhosphorEngine::PlacementStateKey& key,
                                         const ScrollLayoutParams& params)
{
    // Off, no strip reads a floor, and dropping one would discard what the
    // client said before the setting comes back.
    if (m_committedExtents.isEmpty() || !params.respectMinimumSize) {
        return false;
    }
    const int workMain = params.axis.mainSize(params.workArea);
    if (workMain <= 0) {
        return false;
    }
    // Every other context a window holds a column in has to be satisfied too
    // before its floor goes: the floor is the window's, read by each of those
    // strips, and a column widened on this desktop says nothing about the
    // column it holds on another.
    const auto satisfiedElsewhere = [this, &key, &params](const QString& windowId, int floorPx) {
        for (const PhosphorEngine::PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
            if (other == key) {
                continue;
            }
            const ScrollState* otherState = m_states.stateForKey(other);
            if (!otherState) {
                continue; // torn down: no column there to hold anything
            }
            // A float there, or a membership whose strip does not hold a
            // tile, has no column to measure, and the floor is waiting for
            // the column it will hold the window to when it comes back.
            const ScrollStrip& otherStrip = otherState->strip();
            const int otherColIdx = otherStrip.columnOfWindow(windowId);
            if (otherColIdx < 0) {
                return false;
            }
            const ScrollLayoutParams otherParams = layoutParamsForKey(other);
            // A strip running the other way never reads this orientation's
            // floor, so it has nothing to satisfy.
            if (otherParams.axis != params.axis) {
                continue;
            }
            // A maximized-to-edges column there is the in-view skip below,
            // seen from here: its extent is declared, so measuring it says
            // nothing about the column the floor holds the window to once it
            // is un-maximized, and the floor waits for that column.
            if (otherStrip.columns().at(otherColIdx).maximizedToEdges) {
                return false;
            }
            if (tileMainOfferPx(otherStrip, windowId, withoutCommittedFloorOf(otherParams, windowId)) < floorPx) {
                return false;
            }
        }
        return true;
    };
    bool layoutChanged = false;
    const ScrollStrip& strip = state.strip();
    for (const Column& column : strip.columns()) {
        // A declared extent takes no floor, so there is nothing to measure one
        // against until the column is un-maximized, and the client's answer is
        // as true afterwards as it was before.
        if (column.maximizedToEdges) {
            continue;
        }
        const int reservation = ScrollStrip::tabbedMainReservationPx(column, params);
        for (const Tile& tile : column.tiles) {
            if (tile.minimized) {
                continue;
            }
            const auto it = m_committedExtents.find(tile.windowId);
            if (it == m_committedExtents.end()) {
                continue;
            }
            CommittedMainExtent& extent = extentFor(*it, params.axis);
            if (extent.floorPx > 0 && extent.floorPx + reservation > workMain) {
                // A floor its column can never offer. reportCommittedSize
                // floats the window over one when the client teaches it, so
                // this is a work area that shrank under a held floor: dropped,
                // the column is offered what fits, and a client that still
                // will not take it answers again and gets the oversized float.
                // Left standing it would pin the column at the work area with
                // no answer able to revise it, and jump it back when the area
                // grows.
                qCInfo(lcScrollEngine) << "settleCommittedFloors:" << tile.windowId << "floor" << extent.floorPx
                                       << "no longer fits the work area's" << workMain << "on" << key.screenId
                                       << "— dropped";
                dropFloor(extent);
                layoutChanged = true;
            } else if (extent.floorPx > 0
                       && tileMainOfferPx(strip, tile.windowId, withoutCommittedFloorOf(params, tile.windowId))
                           >= extent.floorPx
                       && satisfiedElsewhere(tile.windowId, extent.floorPx)) {
                // Satisfied: the column offers the tile at least the floor
                // without it, because the user widened it or a sibling's floor
                // did. The floor changes nothing now, but left standing it
                // would come back into force the moment the column narrows and
                // pin it to an answer the client gave under other conditions.
                // Dropped, the narrower column is offered as asked, and a
                // client that still will not take it answers with a new floor.
                // Layout-neutral by construction.
                dropFloor(extent);
            }
            // With no floor held (none learned, or one just dropped above),
            // the peek memory lasts only while the column still offers the
            // tile at least that much. Once it offers less, the client is
            // being offered a narrower tile than the one the memory records,
            // and a client that takes it says so by reporting nothing (an
            // answer equal to its offer is silent). Kept, the memory would
            // park the column on every straddle for good, on the strength of
            // an answer the client has since stopped giving. Ended here, a
            // client that still draws wider answers the narrower offer and
            // teaches the floor and the memory again. The floor is 0 here,
            // so params already reads the column without it.
            if (extent.floorPx == 0 && extent.peekPx > 0
                && tileMainOfferPx(strip, tile.windowId, params) < extent.peekPx) {
                extent.peekPx = 0;
            }
            if (it->isEmpty()) {
                m_committedExtents.erase(it);
            }
        }
    }
    return layoutChanged;
}

void ScrollEngine::reportCommittedSize(const QString& rawWindowId, const QSize& offered, const QSize& committed)
{
    // isEmpty rejects the zero and negative sizes a malformed caller can put
    // on the wire; kMaxClientExtentPx bounds the other end below.
    if (offered.isEmpty() || committed.isEmpty()) {
        return;
    }
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // onWindowResized's drag exclusion, for its reason: a frame under an
    // interactive move is motion, not the client's answer to a column.
    if (!m_interactiveDragWindow.isEmpty() && windowId == m_interactiveDragWindow) {
        return;
    }
    // Answered before anything is resolved: an answer no larger than its offer
    // on either axis, from a window the engine has learned nothing about, can
    // teach nothing (only a wider answer creates a floor) and cannot outgrow
    // the work area. Axis-free, so it needs no params.
    if (!m_committedExtents.contains(windowId) && committed.width() <= offered.width()
        && committed.height() <= offered.height()) {
        return;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    ScrollStrip& strip = state->strip();
    const int colIdx = strip.columnOfWindow(windowId);
    // A maximized-to-edges column's extent is declared and reads no floor
    // (columnExtentPx returns before reading one), so nothing a client says
    // about it can change the layout.
    if (colIdx < 0 || strip.columns().at(colIdx).maximizedToEdges) {
        return;
    }
    // The window's OWN context. stateForWindow resolves the membership in
    // view for a window on several desktops, which is the strip whose offer
    // the compositor just sent.
    const ScrollLayoutParams params = layoutParamsForKey(key);
    // Nothing is learned while minimum sizes are not respected, since no
    // strip would read it; turning the setting on forces a fresh offer
    // (refreshConfigFromSettings, applyPerScreenConfig) so the answer can be
    // learned then.
    if (!params.respectMinimumSize || !params.workArea.isValid()) {
        return;
    }
    const StripAxis axis = params.axis;
    const int offeredMain = axis.mainSize(offered);
    const int committedMain = qMin(axis.mainSize(committed), kMaxClientExtentPx);
    // Within the slack the answer IS the offer (fractional-scale rounding).
    const bool wider = committedMain > offeredMain + kCommittedExtentSlackPx;
    const bool narrower = committedMain < offeredMain - kCommittedExtentSlackPx;
    // An answer longer than the work area ACROSS the strip can never fit any
    // tile there. Shorter cross overshoots teach nothing: the relayout centres
    // or clamps them like any cross commit.
    const bool crossOverflow = axis.crossSize(committed) > axis.crossSize(params.workArea);
    const CommittedMainExtent before = extentFor(m_committedExtents.value(windowId), axis);
    // Nothing to learn and nothing to float, answered before the relayout
    // below. The compositor reports again for every new offer whose answer
    // diverges, the same answer included, so these are its steady state: an
    // answer that is the offer along the strip (a cross-only report inside the
    // work area teaches nothing), a wider one repeating the floor already
    // held, a narrower one with no floor to give back and no memory to
    // replace, or a narrower one repeating the memory while no floor can move
    // (none held, or a pinned one).
    if (!crossOverflow) {
        if (!wider && !narrower) {
            return;
        }
        if (wider && committedMain == before.floorPx) {
            return;
        }
        if (narrower && before.floorPx == 0 && before.peekPx == 0) {
            return;
        }
        if (narrower && committedMain == before.peekPx && (before.floorPx == 0 || before.pinned)) {
            return;
        }
    }
    // Whether this answers the tile the strip holds NOW: @p offered against
    // the tile's whole resolved rect. applyLayout commits a clamped rect for a
    // column peeking past the screen edge and for a stack tile overflowing the
    // work area, and a client answering one of those was never offered its
    // column; a report that crossed a newer batch on the wire is refused the
    // same way, and the client's answer to that batch arrives as a report of
    // its own. Gating on m_lastAppliedRect instead would lose a genuine answer
    // whenever one of the many paths that drop that memory ran first.
    QSize tileSize;
    const ResolvedStrip resolved = strip.relayout(params);
    for (const ResolvedColumn& rc : resolved.columns) {
        if (rc.columnIndex != colIdx) {
            continue;
        }
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                tileSize = rt.rect.size();
                break;
            }
        }
        break;
    }
    if (tileSize != offered) {
        return;
    }
    const int extentBefore = strip.windowColumnMainExtentPx(windowId, params);
    CommittedExtent& entry = m_committedExtents[windowId];
    CommittedMainExtent& extent = extentFor(entry, axis);
    if (wider) {
        // A floor the column is held to like a declared minimum, so the next
        // column makes room for the window instead of sitting under it. A
        // wider answer after a give-back means the client does not answer one
        // offer the same way twice; pinning the floor is what stops it
        // resizing its column on every round trip (see CommittedMainExtent).
        //
        // Convergence otherwise rests on the client's extent along the strip
        // not growing with the offer. One that answers every offer a little
        // wider ratchets the floor up one round trip at a time, and the
        // ratchet ends at the work area, where the verdict below floats it.
        if (extent.gaveBack) {
            extent.pinned = true;
        }
        extent.floorPx = committedMain;
        extent.peekPx = committedMain;
    } else if (narrower) {
        // The newest whole-tile answer replaces the peek memory rather than
        // ending it: the client still answers at SOME width past what a clamp
        // would hand it, and this is the latest word on which.
        if (extent.peekPx > 0 || extent.floorPx > 0) {
            extent.peekPx = committedMain;
        }
        // Narrower than an offer a floor had widened gives the widening back:
        // down to the answer while that still overshoots what the column
        // offers without the floor, entirely once it does not. A pinned floor
        // stays.
        if (extent.floorPx > 0 && !extent.pinned) {
            const int offerWithout = tileMainOfferPx(strip, windowId, withoutCommittedFloorOf(params, windowId));
            extent.floorPx = committedMain > offerWithout ? committedMain : 0;
            extent.gaveBack = true;
        }
    }
    const int floorAfter = extent.floorPx;
    if (entry.isEmpty()) {
        m_committedExtents.remove(windowId);
    }
    // The oversized verdict, after the record and ahead of every exit below,
    // so it inherits the gates above (minimum sizes respected, a whole-tile
    // answer) and judges the floor just learned: one its column can never
    // offer, or an answer that cannot fit the work area across the strip,
    // floats the window the way a declared minimum past the work area does.
    if (crossOverflow || outgrowsWorkArea(*state, windowId, params)) {
        qCInfo(lcScrollEngine) << "reportCommittedSize:" << windowId << "offered" << offered << "committed" << committed
                               << "cannot fit the work area" << params.workArea.size() << "on" << key.screenId
                               << "— floating (open-time oversized policy)";
        floatWindowInternal(state, key, windowId, key.screenId);
        return;
    }
    if (strip.windowColumnMainExtentPx(windowId, params) == extentBefore) {
        return;
    }
    qCInfo(lcScrollEngine) << "reportCommittedSize:" << windowId << "offered" << offered << "committed" << committed
                           << "column floor now" << floorAfter << "on" << key.screenId;
    // The strip's layout changed with no verb behind it, and placementChanged
    // is what republishes a strip to its readers (the placement map, the
    // Monitors view, the scroll-cap list) besides marking it dirty. The floor
    // itself is runtime state and never saved.
    Q_EMIT placementChanged(key.screenId);
    // Context-guarded like every other retile this engine schedules.
    if (key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

} // namespace PhosphorScrollEngine
