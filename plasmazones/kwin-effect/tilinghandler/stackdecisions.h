// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

#include <limits>
#include <utility>

/// How a tile batch restacks overlap layouts and replays a saved stacking
/// order, header-only so the rules are unit-testable without a compositor
/// (same pattern as pretiledecisions.h). The restack is templated on an opaque
/// key: the effect passes EffectWindow pointers, the tests pass strings.
namespace PlasmaZones::StackDecisions {

/// The raise sequence that puts each overlap group, in its declared
/// bottom-to-top order, at the stacking slot of its lowest member, and keeps
/// every other window's relative order as it is now: raise the group, then
/// re-raise, in their current order, the windows that were above its lowest
/// member. Groups are placed lowest first, each against the order the
/// previous one left. A window the user raised during the cascade is above
/// the group's slot and stays above it (F329). Members absent from
/// @p currentBottomToTop are ignored; a group with none places nothing.
template<typename Key>
QList<Key> overlapRaiseSequence(QList<Key> currentBottomToTop, QList<QList<Key>> groups)
{
    QList<Key> sequence;
    while (!groups.isEmpty()) {
        // The remaining group whose lowest live member sits lowest now.
        int pick = -1;
        qsizetype pickLowest = std::numeric_limits<qsizetype>::max();
        for (int g = 0; g < groups.size(); ++g) {
            for (const Key& member : std::as_const(groups.at(g))) {
                const qsizetype index = currentBottomToTop.indexOf(member);
                if (index >= 0 && index < pickLowest) {
                    pickLowest = index;
                    pick = g;
                }
            }
        }
        if (pick < 0) {
            break; // no group has a live member
        }
        QList<Key> members;
        for (const Key& member : std::as_const(groups.at(pick))) {
            if (currentBottomToTop.contains(member) && !members.contains(member)) {
                members.append(member);
            }
        }
        groups.removeAt(pick);
        QList<Key> above;
        for (qsizetype i = pickLowest + 1; i < currentBottomToTop.size(); ++i) {
            if (!members.contains(currentBottomToTop.at(i))) {
                above.append(currentBottomToTop.at(i));
            }
        }
        sequence += members;
        sequence += above;
        QList<Key> next = currentBottomToTop.mid(0, pickLowest);
        next += members;
        next += above;
        currentBottomToTop = next;
    }
    return sequence;
}

/// One window of a screen's saved autotile stacking order, as a tile batch for
/// that screen reads it when it completes.
struct SavedOrderWindow
{
    /// still open
    bool alive = false;
    /// on the batch's screen, on the desktop its output shows and on the
    /// current activity
    bool inView = false;
    /// on several desktops or activities, or all of them, so it reads in view
    /// from any of those contexts (spansContexts)
    bool spansContexts = false;
};

/// What a tile batch does with a screen's saved stacking order.
struct SavedOrderReplay
{
    /// raise the in-view windows in the saved order
    bool raise = false;
    /// drop the saved order: replayed, or nobody left to replay it for
    bool spent = false;
};

/// The saved stacking order is keyed by screen only, but it was saved for the
/// desktop and activity in view at the toggle, so only a batch of THAT context
/// replays and spends it. A batch from another context (a desktop switched to
/// since) leaves it for the round trip back (F444).
///
/// A window that lives in one context alone tells the contexts apart: in view,
/// the batch is the saved context's. A window spanning several reads in view
/// from any of them, so it tells nothing while some saved window lives in one
/// context alone, and a batch that finds only spanning windows in view is not
/// taken for the saved context. When every live saved window spans contexts,
/// none can tell them apart, and any in-view one is taken as the context, as
/// it was before the distinction, rather than leaving the order unspent until
/// those windows close. An order whose windows are all closed is spent.
inline SavedOrderReplay savedOrderReplay(const QList<SavedOrderWindow>& windows)
{
    bool anyAlive = false;
    bool anySingleContextAlive = false;
    bool anyInView = false;
    bool singleContextInView = false;
    for (const SavedOrderWindow& w : windows) {
        if (!w.alive) {
            continue;
        }
        anyAlive = true;
        anySingleContextAlive = anySingleContextAlive || !w.spansContexts;
        anyInView = anyInView || w.inView;
        singleContextInView = singleContextInView || (w.inView && !w.spansContexts);
    }
    const bool savedContext = anySingleContextAlive ? singleContextInView : anyInView;
    return SavedOrderReplay{savedContext, savedContext || !anyAlive};
}

} // namespace PlasmaZones::StackDecisions
