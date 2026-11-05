// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// What a client's own commits teach the engine about its window's extent along
// the strip and across it (reportCommittedSize), the per-window floors and
// peek memory kept from them (m_committedExtents), and the two things that keep
// a learned floor honest afterwards: the sweep applyLayout runs
// (settleCommittedFloors) and the oversized verdict a floor shares with a
// declared minimum (outgrowsWorkArea). applyLayout's second sweep
// (endStalePeeks) ends a peek memory, and the extent with it, once no column
// the window holds offers it. The strip never holds any of this; it reads the
// floors through ScrollLayoutParams::committedFloor and committedCrossFloor,
// which layoutParamsForKey points at committedFloorFor and
// committedCrossFloorFor.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"
#include "scrollpark_p.h"

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

/// The cross-axis half of @p extent a strip running along @p axis reads.
CommittedCrossExtent& crossExtentFor(CommittedExtent& extent, const StripAxis& axis)
{
    return axis.isHorizontal() ? extent.horizontalCross : extent.verticalCross;
}

const CommittedCrossExtent& crossExtentFor(const CommittedExtent& extent, const StripAxis& axis)
{
    return axis.isHorizontal() ? extent.horizontalCross : extent.verticalCross;
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
/// is still true after its column stops being held to it, for as long as a
/// column still offers at least that much (endStalePeeks ends it once every
/// column the window holds offers less, on a pass that offers this one whole).
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
    masked.committedCrossFloor = [inner = params.committedCrossFloor, windowId](const QString& id) {
        return (id == windowId || !inner) ? 0 : inner(id);
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

int ScrollEngine::committedCrossFloorFor(const QString& windowId, const StripAxis& axis) const
{
    // committedFloorFor's fast path, for its reason.
    if (m_committedExtents.isEmpty()) {
        return 0;
    }
    const auto it = m_committedExtents.constFind(windowId);
    if (it == m_committedExtents.cend()) {
        return 0;
    }
    return crossExtentFor(*it, axis).floorPx;
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

bool ScrollEngine::dropCommittedCrossForUserResize(const QString& windowId, const StripAxis& axis)
{
    const auto it = m_committedExtents.find(windowId);
    if (it == m_committedExtents.end()) {
        return false;
    }
    CommittedCrossExtent& cross = crossExtentFor(*it, axis);
    const bool hadFloor = cross.floorPx > 0;
    cross = CommittedCrossExtent{};
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
    // The cross floors the same way, by the axis that is their CROSS one: a
    // horizontal strip's cross floor is a height, so it answers to the
    // height minimum.
    if (newMin.height() < oldMin.height()) {
        it->horizontalCross = CommittedCrossExtent{};
    }
    if (newMin.width() < oldMin.width()) {
        it->verticalCross = CommittedCrossExtent{};
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
    // Past the work area by more than the rounding slack: a floor a pixel over
    // is a fractional-scale answer to a column the work area capped, and the
    // relayout holds it at the work area without stranding anything.
    // settleCommittedFloors drops on the same test, so a floor this keeps
    // tiled is one that sweep keeps too.
    const CommittedExtent committed = m_committedExtents.value(windowId);
    const int floorPx = extentFor(committed, params.axis).floorPx;
    if (floorPx > 0
        && floorPx + ScrollStrip::tabbedMainReservationPx(column, params)
            > params.axis.mainSize(params.workArea) + kCommittedExtentSlackPx) {
        return true;
    }
    // Across the strip, the most the column can ever give the tile while its
    // siblings keep their own floors: the stack can never hold a floor past
    // that, however its split is asked for. A minimized tile is laid out
    // nowhere and reads no capacity. When two tiles' floors cannot fit the
    // column together, whichever client teaches its floor SECOND is the one
    // judged here against the other's, so that window floats: the floor is
    // read as that window's claim on a column already holding its sibling's.
    const int crossFloorPx = crossExtentFor(committed, params.axis).floorPx;
    if (crossFloorPx <= 0) {
        return false;
    }
    const int capacityPx = strip.tileCrossCapacityPx(windowId, params);
    return capacityPx >= 0 && crossFloorPx > capacityPx + kCommittedExtentSlackPx;
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
                           << "committed floor" << committedFloorFor(windowId) << "cross"
                           << committedCrossFloorFor(windowId, params.axis) << "outgrew work area"
                           << params.workArea.size() << "on" << key.screenId
                           << "— floating (open-time oversized policy)";
    // Passive, the open-time oversized float's announcement (finishFloatedOpen
    // emits windowFloatingStateSynced): the engine decided this float, not the
    // user, so the daemon mirrors the state with no float OSD and no restore
    // to the window's free geometry, which for a window too large for any
    // column is a size its client already refused.
    return floatWindowInternal(state, key, windowId, key.screenId, FloatAnnounce::Passive, /*fullscreenHold=*/false,
                               applyAfter);
}

bool ScrollEngine::everyOtherColumnOffers(const QString& windowId, const PhosphorEngine::PlacementStateKey& key,
                                          const StripAxis& axis, const std::function<bool(int)>& accepts,
                                          OfferAxis offerAxis) const
{
    for (const PhosphorEngine::PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
        if (other == key) {
            continue;
        }
        const ScrollState* otherState = m_states.stateForKey(other);
        if (!otherState) {
            continue; // torn down: no column there to hold anything
        }
        // A float there, or a membership whose strip does not hold a tile,
        // has no column to measure, and what the client said is waiting for
        // the column the window comes back to.
        const ScrollStrip& otherStrip = otherState->strip();
        const int otherColIdx = otherStrip.columnOfWindow(windowId);
        if (otherColIdx < 0) {
            return false;
        }
        const ScrollLayoutParams otherParams = layoutParamsForKey(other);
        // A strip running the other way never reads this orientation's
        // extent, so it has nothing to say about it.
        if (otherParams.axis != axis) {
            continue;
        }
        // A maximized-to-edges column there is the in-view skip of both
        // sweeps, seen from here: its extent is declared, so measuring it
        // says nothing about the column it holds the window to once it is
        // un-maximized, and what the client said waits for that column.
        if (otherStrip.columns().at(otherColIdx).maximizedToEdges) {
            return false;
        }
        // A minimized tile there is laid out nowhere, so its cross share
        // reads -1, which no floor accepts.
        const ScrollLayoutParams without = withoutCommittedFloorOf(otherParams, windowId);
        const int offerPx = offerAxis == OfferAxis::Main ? tileMainOfferPx(otherStrip, windowId, without)
                                                         : otherStrip.windowTileCrossExtentPx(windowId, without);
        if (!accepts(offerPx)) {
            return false;
        }
    }
    return true;
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
            // Both tests allow the rounding slack, outgrowsWorkArea's terms:
            // a floor a pixel past the work area is one the relayout caps
            // harmlessly, and an offer a pixel short of the floor satisfies it,
            // since the client's answer to that offer would read as the offer.
            if (extent.floorPx > 0 && extent.floorPx + reservation > workMain + kCommittedExtentSlackPx) {
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
            } else if (const int offerWithout = extent.floorPx > 0
                           ? tileMainOfferPx(strip, tile.windowId, withoutCommittedFloorOf(params, tile.windowId))
                           : -1;
                       extent.floorPx > 0 && offerWithout >= extent.floorPx - kCommittedExtentSlackPx
                       && everyOtherColumnOffers(tile.windowId, key, params.axis,
                                                 [floorPx = extent.floorPx](int offerPx) {
                                                     return offerPx >= floorPx - kCommittedExtentSlackPx;
                                                 })) {
                // Satisfied: the column offers the tile at least the floor
                // without it, because the user widened it or a sibling's floor
                // did, and so does every other column the window holds. The
                // floor is the window's, read by each of those strips, and a
                // column widened on this desktop says nothing about the column
                // it holds on another. The floor changes nothing now, but left
                // standing it would come back into force the moment the column
                // narrows and pin it to an answer the client gave under other
                // conditions. Dropped, the narrower column is offered as
                // asked, and a client that still will not take it answers with
                // a new floor. Layout-neutral by construction, except for an
                // offer within the rounding slack SHORT of the floor, whose
                // column narrows by that much and is reported.
                if (offerWithout < extent.floorPx) {
                    layoutChanged = true;
                }
                dropFloor(extent);
            }
            if (settleCommittedCrossFloor(strip, key, params, tile.windowId, crossExtentFor(*it, params.axis))) {
                layoutChanged = true;
            }
            if (it->isEmpty()) {
                m_committedExtents.erase(it);
            }
        }
    }
    return layoutChanged;
}

bool ScrollEngine::settleCommittedCrossFloor(const ScrollStrip& strip, const PhosphorEngine::PlacementStateKey& key,
                                             const ScrollLayoutParams& params, const QString& windowId,
                                             CommittedCrossExtent& cross) const
{
    if (cross.floorPx <= 0) {
        // Give-back history with no floor left (CommittedCrossExtent::gaveBack)
        // lasts while the tile is offered the width it was given back at. A
        // new width is new conditions for a client that picks its cross
        // extent from its width, and history kept past it would pin the next
        // floor that client teaches as though it were a re-teach.
        if (cross.gaveBack && qAbs(tileMainOfferPx(strip, windowId, params) - cross.mainPx) > kCommittedExtentSlackPx) {
            cross = CommittedCrossExtent{};
        }
        return false;
    }
    const int floorPx = cross.floorPx;
    // A floor the stack can no longer hold: a sibling joined the column or
    // raised its own floor, or the work area shrank. reportCommittedSize
    // floats a window whose client teaches such a floor, so this is a column
    // that changed under a held one. Dropped, the stack is split as asked,
    // and a client that still will not take its share answers again and gets
    // the oversized float. Left standing, the relayout would clamp it at the
    // column's budget and lay the siblings past the work area. The rounding
    // slack is outgrowsWorkArea's, so a floor that verdict keeps tiled is one
    // this keeps too.
    const int capacityPx = strip.tileCrossCapacityPx(windowId, params);
    if (capacityPx >= 0 && floorPx > capacityPx + kCommittedExtentSlackPx) {
        qCInfo(lcScrollEngine) << "settleCommittedFloors:" << windowId << "cross floor" << floorPx
                               << "no longer fits its column's" << capacityPx << "on" << key.screenId << "— dropped";
        cross = CommittedCrossExtent{};
        return true;
    }
    // The premise moved: the tile is offered another extent along the strip
    // than the one the client answered with this floor. A client that picks
    // its cross extent from its main one answers the new width differently,
    // and is held to an answer it gave another width for as long as the floor
    // stands. Dropped, the tile is offered its share, and a client still
    // taller than that answers with a new floor.
    //
    // The cost is a round trip per width change the client did not cause: a
    // user's width change, or a sibling's, drops the floor before the client
    // has answered the new width, the stack re-splits without it for one
    // pass, and a client still taller re-teaches it. A width change the
    // client's own answer caused re-stamps the premise instead
    // (reportCommittedSize), so it costs nothing, but only while that answer
    // leaves a floor along the strip standing or itself moves the cross
    // floor. An answer that gives the floor along the strip back entirely,
    // with nothing new to say across it, leaves the premise at the old width
    // and pays the round trip like a width change it did not cause.
    //
    // Judged against THIS context's column only, like every drop in this
    // sweep, while the floor is the window's: a window on several desktops
    // whose columns there have different widths drops the floor on every
    // switch to a desktop whose width is not the premise, and its client
    // teaches it again there. Accepted, since holding one desktop's answer
    // over another desktop's column is the stale floor this drop exists to
    // prevent, and per-context floors would be a structural change to the
    // per-window record (CommittedExtent).
    const int mainOfferPx = tileMainOfferPx(strip, windowId, params);
    if (qAbs(mainOfferPx - cross.mainPx) > kCommittedExtentSlackPx) {
        qCInfo(lcScrollEngine) << "settleCommittedFloors:" << windowId << "cross floor" << floorPx << "learned at"
                               << cross.mainPx << "along the strip, now offered" << mainOfferPx << "on" << key.screenId
                               << "— dropped";
        cross = CommittedCrossExtent{};
        return true;
    }
    // Satisfied: the tile's share without its own floor is at least the
    // floor (its siblings closed, or the user grew it), here and in every
    // other column the window holds. Kept, it would come back into force the
    // moment a sibling joins and hold the tile to an answer the client gave
    // under other conditions. Dropped, the next split is offered as asked.
    // Unlike the floor along the strip, whose satisfied drop moves the layout
    // only for an offer within the slack short of it, this can move it
    // further: the floor also lowers the tile's slack in the stack's
    // rebalance, so a column whose siblings' minimums overflow it can split
    // differently without it, and that is measured rather than assumed.
    //
    // A share within the rounding slack of the floor satisfies it, the
    // floor-along-the-strip arm's terms.
    const ScrollLayoutParams without = withoutCommittedFloorOf(params, windowId);
    const int shareWithoutPx = strip.windowTileCrossExtentPx(windowId, without);
    if (shareWithoutPx < floorPx - kCommittedExtentSlackPx
        || !everyOtherColumnOffers(
            windowId, key, params.axis,
            [floorPx](int offerPx) {
                return offerPx >= floorPx - kCommittedExtentSlackPx;
            },
            OfferAxis::Cross)) {
        return false;
    }
    const int shareWithPx = strip.windowTileCrossExtentPx(windowId, params);
    cross = CommittedCrossExtent{};
    return shareWithPx != shareWithoutPx;
}

void ScrollEngine::endStalePeeks(const ScrollStrip& strip, const ResolvedStrip& resolved,
                                 const PhosphorEngine::PlacementStateKey& key, const ScrollLayoutParams& params,
                                 const QRect& screenRect, int parkTop)
{
    // Off, nothing reads the memory, and ending it would discard what the
    // client said before the setting comes back.
    if (m_committedExtents.isEmpty() || !params.respectMinimumSize) {
        return;
    }
    for (const ResolvedColumn& column : resolved.columns) {
        // A declared extent: the client's answer is as true once the column
        // is un-maximized as it was before.
        if (column.maximizedToEdges) {
            continue;
        }
        for (const ResolvedTile& tile : column.tiles) {
            // Under an interactive move the window is committed nothing this
            // pass, so it is offered nothing either, whatever its strip rect
            // says. The memory is judged again on the drop's own relayout,
            // which runs once the mark is gone.
            if (!m_interactiveDragWindow.isEmpty() && tile.windowId == m_interactiveDragWindow) {
                continue;
            }
            const auto it = m_committedExtents.find(tile.windowId);
            if (it == m_committedExtents.end()) {
                continue;
            }
            // With no floor held (none learned, or one settleCommittedFloors
            // dropped this pass), the memory lasts only while the column
            // still offers the tile at least that much. Once it offers less,
            // a client that takes the narrower tile on screen says so by
            // reporting nothing (an answer equal to an on-screen offer is
            // silent; a parked one's is not, see below), and a memory kept
            // past that point would park the column on every straddle for
            // good, on the strength of an answer the client has since
            // stopped giving. The floor is 0 here, so params already reads
            // the column without it.
            CommittedMainExtent& extent = extentFor(*it, params.axis);
            const int peekPx = extent.peekPx;
            if (extent.floorPx > 0 || peekPx == 0 || tileMainOfferPx(strip, tile.windowId, params) >= peekPx) {
                continue;
            }
            // Ended only on a pass that offers the tile its WHOLE rect once
            // the memory is gone: on screen, parked, or kept whole by crop
            // mode. Ending it on a pass that clamps the tile at the screen
            // edge would hand the client a sliver, and a client that still
            // draws wider answers that sliver across the edge with nothing
            // to re-teach it, since reportCommittedSize refuses an answer to
            // anything but the whole tile. Kept, the straddler parks on the
            // memory, its park is the whole tile, and the memory ends on the
            // first pass that offers the tile whole without it. A tile the
            // cross-axis stack clamp shortens is kept for the same reason
            // (its clamped offer teaches nothing either), though nothing
            // parks on the memory there until the tile also straddles a
            // screen edge. The tile's minimum without the memory is its
            // declared one, the same input the emit loop's park reads once
            // the memory is gone.
            //
            // Not ended here on the evidence of a park alone: the park at this
            // size is a whole offer, but the silence after one cannot tell a
            // client that took it from one whose wider answer has not arrived
            // yet. Ending the memory on the pass after a park (its rect
            // already whole at this size) would offer a sliver one frame
            // later on a pixel pan, the client would then answer the sliver
            // instead of the park, and nothing would re-teach it. The memory
            // ends on a park only once the client answers the park itself:
            // the compositor reports a parked tile's settled commit even when
            // it equals the offer along the strip, and reportCommittedSize
            // ends the memory on that matched answer. Nothing ever ends it on
            // silence, so the pixel-pan hazard stays shut, and a matched
            // answer that crossed a newer batch is refused at its whole-tile
            // gate like any other. A tile the stack clamp shortens keeps the
            // memory for as long as the overflow clamps it.
            const Detail::ParkInputs whatIf = Detail::parkInputsFor(tile, column, params, screenRect, parkTop,
                                                                    strip.windowMinimumSize(tile.windowId));
            if (Detail::resolveTilePlacement(whatIf, QString()).rect.size() != tile.rect.size()) {
                continue;
            }
            // The memory is the window's, read by the peek floor of every
            // strip it holds a column in, and one column offering less says
            // nothing about another that still offers the memory: that one
            // is not stale, and ending the memory from here would clamp it
            // to a sliver the next time it straddles. Kept until every
            // column the window holds offers less.
            if (!everyOtherColumnOffers(tile.windowId, key, params.axis, [peekPx](int offerPx) {
                    return offerPx < peekPx;
                })) {
                continue;
            }
            // The whole extent ends, the give-back history with it: with no
            // floor left and nothing remembered, nothing the client said
            // still stands, and history kept on its own would pin the next
            // floor it teaches as though that were a re-teach.
            extent = CommittedMainExtent{};
            if (it->isEmpty()) {
                m_committedExtents.erase(it);
            }
        }
    }
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
    // teach nothing (only a longer answer creates a floor, along the strip or
    // across it), ends no memory, and cannot outgrow the work area. Axis-free,
    // so it needs no params.
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
    // tile there. A shorter cross overshoot is a tile in a stack answering
    // longer than its share of the column, and teaches a floor on that share
    // (CommittedCrossExtent), so the stack's split makes room for the window
    // instead of laying the tile below it underneath. Past the work area by
    // more than the slack, like every compare here: a lone tile offered the
    // whole cross extent answers a pixel over it on a fractional scale, and
    // reading that as "cannot fit" floated a window that took its tile.
    const bool crossOverflow = axis.crossSize(committed) > axis.crossSize(params.workArea) + kCommittedExtentSlackPx;
    const int offeredCross = axis.crossSize(offered);
    const int committedCross = qMin(axis.crossSize(committed), kMaxClientExtentPx);
    const bool taller = !crossOverflow && committedCross > offeredCross + kCommittedExtentSlackPx;
    // Shorter than the offer across the strip: what gives a held cross floor
    // back, the narrower answer's twin. A client holding an aspect ratio
    // answers a full-height column a little short on every placement, and
    // with no cross floor held that teaches nothing.
    const bool shorter = committedCross < offeredCross - kCommittedExtentSlackPx;
    const CommittedExtent beforeEntry = m_committedExtents.value(windowId);
    const CommittedMainExtent before = extentFor(beforeEntry, axis);
    const CommittedCrossExtent crossBefore = crossExtentFor(beforeEntry, axis);
    // An answer that IS the offer along the strip, from a tile holding a peek
    // memory longer than that offer and no floor: the client took a whole
    // offer narrower than the memory, a park included, so what it said
    // earlier no longer stands. The compositor sends a parked tile's matched
    // answer for exactly this.
    //
    // The no-floor term is load-bearing, not a readability guard: the arm it
    // gates resets the whole extent, floor included. Every write keeps the
    // memory at or under the floor, and a held floor normally holds the offer
    // at or over it, but not when the work area caps the column. A floor up to
    // kCommittedExtentSlackPx past the work area stays tiled
    // (outgrowsWorkArea, settleCommittedFloors), its column is capped below
    // it, and a matched answer to that capped offer then reads as a memory
    // longer than the offer. Without the term it would wipe the floor.
    const bool matched = !wider && !narrower;
    const bool mayEndPeek = matched && before.floorPx == 0 && before.peekPx > offeredMain;
    // Nothing to learn and nothing to float, answered before the relayout
    // below. The compositor reports again for every new offer whose answer
    // diverges, the same answer included, so these are its steady state.
    // Along the strip: an answer that is the offer with no memory it could
    // end, a wider one repeating the floor already held, a narrower one with
    // no floor to give back and no memory to replace, or a narrower one
    // repeating the memory while no floor can move (none held, or a pinned
    // one). Across it: a longer answer repeating the cross floor already
    // held, or any answer no longer than the offer that cannot give a cross
    // floor back (none held, or a pinned one). Only an answer that is steady
    // on both axes returns here; one steady along one axis alone learns only
    // along the other below, so a repeated answer cannot pin a floor on the
    // way.
    const bool mainNoop = !crossOverflow
        && ((matched && !mayEndPeek) || (wider && committedMain == before.floorPx)
            || (narrower && before.floorPx == 0 && before.peekPx == 0)
            || (narrower && committedMain == before.peekPx && (before.floorPx == 0 || before.pinned)));
    const bool crossNoop =
        taller ? committedCross == crossBefore.floorPx : !(shorter && crossBefore.floorPx > 0 && !crossBefore.pinned);
    if (mainNoop && crossNoop) {
        return;
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
    // An answer steady along the strip learns nothing along it: only its
    // cross half below is new.
    if (wider && !mainNoop) {
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
    } else if (narrower && !mainNoop) {
        // The newest whole-tile answer replaces the peek memory rather than
        // ending it: the client still answers at SOME width past what a clamp
        // would hand it, and this is the latest word on which.
        if (extent.peekPx > 0 || extent.floorPx > 0) {
            extent.peekPx = committedMain;
        }
        // Narrower than an offer a floor had widened gives the widening back:
        // down to the answer while that still overshoots what the column
        // offers without the floor, entirely once it does not. A pinned floor
        // stays. "Overshoots" by more than the slack: a floor kept a pixel
        // past what the column offers without it is one settleCommittedFloors
        // could never find satisfied, and it would hold the column a pixel
        // wide for good.
        if (extent.floorPx > 0 && !extent.pinned) {
            const int offerWithout = tileMainOfferPx(strip, windowId, withoutCommittedFloorOf(params, windowId));
            extent.floorPx = committedMain > offerWithout + kCommittedExtentSlackPx ? committedMain : 0;
            extent.gaveBack = true;
        }
    } else if (mayEndPeek) {
        // The client took the whole offer it was given, narrower than the
        // memory, and said so: the answer is the settled commit to that very
        // offer (the whole-tile gate above refuses one that crossed a newer
        // batch), not the silence endStalePeeks cannot read, so the memory
        // ends here even when the offer was a park. The whole extent ends, the
        // give-back history with it, endStalePeeks' reason. Kept while
        // another column the window holds still offers the memory, which is
        // endStalePeeks' rule too: that one is not stale. A held floor never
        // reaches here, so a matched answer never touches one. (mayEndPeek
        // already holds the memory longer than this offer.)
        const int peekPx = extent.peekPx;
        if (everyOtherColumnOffers(windowId, key, axis, [peekPx](int offerPx) {
                return offerPx < peekPx;
            })) {
            extent = CommittedMainExtent{};
        }
    }
    const int floorAfter = extent.floorPx;
    // Read before the entry can be erased below: whether the peek memory
    // moved, which the exit for an unchanged extent lays out again for.
    const bool peekMoved = extent.peekPx != before.peekPx;
    // Across the strip: a tile answering longer than its share of the stack,
    // inside the work area, holds that share at the answer, so the split
    // gives the tile at least this and shrinks its siblings. Its premise is
    // the offer along the strip read AFTER the floor along it is recorded,
    // since that is the extent the tile is offered from here on. A longer
    // answer after a give-back pins the floor, the rule along the strip.
    CommittedCrossExtent& cross = crossExtentFor(entry, axis);
    if (taller && !crossNoop) {
        if (cross.gaveBack) {
            cross.pinned = true;
        }
        cross.floorPx = committedCross;
        cross.mainPx = tileMainOfferPx(strip, windowId, params);
    } else if (shorter && cross.floorPx > 0 && !cross.pinned) {
        // Shorter than a share a floor had lengthened gives the lengthening
        // back, the narrower answer's rule across the strip: down to the
        // answer while that still overshoots the share the tile gets without
        // the floor, entirely once it does not.
        const int shareWithout = strip.windowTileCrossExtentPx(windowId, withoutCommittedFloorOf(params, windowId));
        cross.floorPx = committedCross > shareWithout + kCommittedExtentSlackPx ? committedCross : 0;
        cross.gaveBack = true;
        cross.mainPx = tileMainOfferPx(strip, windowId, params);
    } else if (cross.floorPx > 0 && floorAfter > 0 && floorAfter != before.floorPx) {
        // This answer moved the tile's offer along the strip (its floor there
        // changed), and the cross extent in the same commit was given at that
        // width, so the held cross floor's premise is the new offer.
        // Otherwise the next sweep would read a premise this answer itself
        // moved, drop the floor, and wait a round trip for the client to
        // teach it again.
        cross.mainPx = tileMainOfferPx(strip, windowId, params);
    }
    const int crossFloorAfter = cross.floorPx;
    if (entry.isEmpty()) {
        m_committedExtents.remove(windowId);
    }
    // The oversized verdict, after the record and ahead of every exit below,
    // so it inherits the gates above (minimum sizes respected, a whole-tile
    // answer) and judges the floors just learned: one along the strip its
    // column can never offer, one across it the stack can never hold beside
    // its siblings' own floors, or an answer that cannot fit the work area
    // across the strip, floats the window the way a declared minimum past the
    // work area does.
    if (crossOverflow || outgrowsWorkArea(*state, windowId, params)) {
        qCInfo(lcScrollEngine) << "reportCommittedSize:" << windowId << "offered" << offered << "committed" << committed
                               << "cannot fit the work area" << params.workArea.size() << "on" << key.screenId
                               << "— floating (open-time oversized policy)";
        // Passive, floatIfOutgrowsWorkArea's reason: the engine decided it.
        floatWindowInternal(state, key, windowId, key.screenId, FloatAnnounce::Passive);
        return;
    }
    // A cross floor moves the stack's split, which the column's extent along
    // the strip does not show, so it counts as a change on its own.
    if (strip.windowColumnMainExtentPx(windowId, params) == extentBefore && crossFloorAfter == crossBefore.floorPx) {
        // A peek memory that ended or was replaced moves no model state, only
        // which of the park and the screen-edge clamp the next pass picks for
        // a straddling tile, so it is laid out again without being
        // republished. A narrower answer that lowered the memory counts too:
        // a straddler parked on the old memory would otherwise stay parked on
        // it until something else retiled.
        if (peekMoved && key == currentKeyForScreen(key.screenId)) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    qCInfo(lcScrollEngine) << "reportCommittedSize:" << windowId << "offered" << offered << "committed" << committed
                           << "column floor now" << floorAfter << "cross floor now" << crossFloorAfter << "on"
                           << key.screenId;
    // The strip's layout changed with no verb behind it, and placementChanged
    // is what republishes a strip to its readers (the placement map, the
    // Monitors view, the scroll-cap list) besides marking it dirty. The
    // floors themselves are runtime state and never saved.
    Q_EMIT placementChanged(key.screenId);
    // Context-guarded like every other retile this engine schedules.
    if (key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

} // namespace PhosphorScrollEngine
