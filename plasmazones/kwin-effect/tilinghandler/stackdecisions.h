// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

#include <limits>
#include <utility>

/// How a tile batch restacks overlap layouts, header-only so the rule is
/// unit-testable without a compositor (same pattern as pretiledecisions.h).
/// Templated on an opaque key: the effect passes EffectWindow pointers, the
/// tests pass strings.
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

} // namespace PlasmaZones::StackDecisions
