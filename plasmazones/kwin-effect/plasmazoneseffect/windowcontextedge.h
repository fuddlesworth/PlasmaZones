// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>
#include <QString>

// What one edit to a window's desktop set (or activity set) did, relative to
// the context in view where the window lives. KWin reports every edit through
// the same signal: a move, a set that grew or shrank, a stick and an un-stick.
// The desktop handler's arms are a function of the classification below and of
// the window's tracking state, never of which edit it was, so the same body
// serves both axes. Kept free of KWin types so it is unit-testable on its own.
namespace PlasmaZones::WindowContextEdge {

/// A window's desktop or activity ids. EMPTY is KWin's "every desktop" /
/// "every activity".
using IdSet = QSet<QString>;

/// The per-window record each axis diffs its next edit against.
struct Stamp
{
    IdSet desktops;
    IdSet activities;
};

/// Whether @p set reaches @p id ("every one" reaches all).
inline bool covers(const IdSet& set, const QString& id)
{
    return set.isEmpty() || set.contains(id);
}

/// A genuine move leaves every id the window was on. Going to or from "every
/// one" is never a move (F581).
inline bool isGenuineMove(const IdSet& previous, const IdSet& current)
{
    return !previous.isEmpty() && !current.isEmpty() && !previous.intersects(current);
}

enum class Kind {
    Unclassifiable, ///< unseeded, or no context in view
    StayedHidden, ///< out of view before and after
    Departed, ///< in view before, out of view after
    Arrived, ///< out of view before, in view after
    StayedInView, ///< in view before and after
};

struct Edge
{
    Kind kind = Kind::Unclassifiable;
    bool genuineMove = false; ///< isGenuineMove(previous, current)
    bool leftAnId = false; ///< a concrete set lost an id it had
    bool becameEverywhere = false; ///< a concrete set became "every one" (stuck)
    bool leftEverywhere = false; ///< "every one" became a concrete set (un-stuck)
    bool onlyInView = false; ///< the set is now exactly the id in view
};

/// Classify the edit @p previous → @p current. @p inView is the id the
/// window's own output shows on this axis; @p otherAxisInView says whether the
/// window is in view on the OTHER axis, without which it is never in view.
/// Unclassifiable when the window was never stamped or nothing is in view.
inline Edge classify(const IdSet& previous, bool hadPrevious, const IdSet& current, const QString& inView,
                     bool otherAxisInView)
{
    Edge edge;
    if (!hadPrevious || inView.isEmpty()) {
        return edge;
    }
    edge.genuineMove = isGenuineMove(previous, current);
    edge.leftAnId = !previous.isEmpty() && !current.isEmpty() && !(previous - current).isEmpty();
    edge.becameEverywhere = !previous.isEmpty() && current.isEmpty();
    edge.leftEverywhere = previous.isEmpty() && !current.isEmpty();
    edge.onlyInView = current.size() == 1 && current.contains(inView);
    const bool before = otherAxisInView && covers(previous, inView);
    const bool after = otherAxisInView && covers(current, inView);
    if (before) {
        edge.kind = after ? Kind::StayedInView : Kind::Departed;
    } else {
        edge.kind = after ? Kind::Arrived : Kind::StayedHidden;
    }
    return edge;
}

} // namespace PlasmaZones::WindowContextEdge
