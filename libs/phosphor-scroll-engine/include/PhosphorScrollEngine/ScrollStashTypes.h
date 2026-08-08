// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QString>
#include <QVector>

namespace PhosphorScrollEngine {

/// The mode-round-trip strip stash value types (ScrollEngine::m_stripStash).
/// Namespace-level rather than nested so the engine header stays within the
/// file-size ceiling; semantics and lifetime are documented on the engine's
/// m_stripStash member, and StashedTile::stagedFromPersistence below carries
/// the cross-session lease contract.
struct StashedTile
{
    QString windowId;
    WindowHeight height;
    /// Carried for serialization fidelity only — the restore paths do
    /// not re-apply it (the effect re-reports live minimize state).
    ///
    /// It reads false for every tile a production daemon ever stashes:
    /// its source is Tile::minimized, and the only writer of that flag
    /// is ScrollStrip::setWindowMinimized, which is a TEST SEAM (the
    /// daemon models minimize as a float, so a minimized window is not
    /// a strip tile at all). The field exists so the strip model's
    /// minimized domain stays round-trippable if the daemon ever drives
    /// it directly; see the seam note on setWindowMinimized.
    bool minimized = false;
    /// Windowed fullscreen, carried through stash/serialize and RE-APPLIED
    /// on claim (unlike minimized above): the flag is strip-owned state the
    /// compositor mirrors, so a restart must hand it back or the client
    /// stays fullscreen-configured with nothing on record saying so.
    bool windowedFullscreen = false;
    /// True while THIS tile was staged from the persisted blob and has
    /// not been claimed. Per tile, not per entry: a key co-tenanted by a
    /// returning app and a dead one must age the dead tile out while the
    /// returning one keeps claiming, and a claim on one tile must not
    /// expose an unclaimed co-tenant to the aliveness sweep.
    ///
    /// A staged tile names LAST session's window id, which by design
    /// appears in no live alive-set: the cross-session claim in
    /// restoreFromStripStash matches on the appId prefix precisely
    /// because the per-instance half of the id is regenerated every
    /// launch. pruneStaleWindows' sweep must therefore not read "absent
    /// from the alive set" as "closed" while this holds, or the very
    /// first prune after login (the effect fires one at bring-up, right
    /// after the daemon stages the snapshot) would erase it and undo the
    /// structure/focus/anchor restore. Cleared on claim, at which point
    /// the tile is anchored in THIS session's id space and the sweep is
    /// meaningful. A tile whose app never relaunches is aged out by the
    /// unclaimedSessions lease below instead.
    bool stagedFromPersistence = false;
    /// Consecutive logins THIS tile was staged without ever being
    /// claimed. Incremented at serialize while stagedFromPersistence
    /// holds; a claim zeroes it; restoreStripState drops a tile that has
    /// gone kMaxUnclaimedSessions logins unclaimed. The aging exists
    /// because pruneStaleWindows fires exactly ONCE per session (at
    /// bring-up, while the TILE is still sweep-exempt), so no sweep can
    /// ever reach a persisted tile whose app never relaunches — without
    /// the lease it would be re-staged forever and eventually hand an
    /// unrelated same-app window a long-dead slot.
    int unclaimedSessions = 0;
};

struct StashedColumn
{
    QVector<StashedTile> tiles;
    ColumnWidth width;
    ColumnDisplay display = ColumnDisplay::Normal;
    /// The column's ACTIVE tile, by window id — for a Tabbed column
    /// that is the shown tab. Carried because every insert makes the
    /// arriving tile its column's active one, so a restore without it
    /// shows whichever sibling happened to announce last.
    QString activeWindowId;
};

/// One stashed strip: the structural columns plus the focus/view pair
/// whose loss made every mode round trip re-anchor on an arbitrary
/// window (first arrival won the focus).
struct StashedStrip
{
    QVector<StashedColumn> columns;
    QString focusedWindowId;
    int viewAnchor = 0;
    /// Monotonic stamp of when this entry was staged (mode exit or
    /// persistence load), from m_stashSequence. serializeStripState
    /// resolves a window listed by two DIFFERENT stash keys in favour of
    /// the higher stamp, because the reader's alphabetical first-wins
    /// would otherwise let a window's older screen displace its newer
    /// one. This orders the stash entries against each other only. A
    /// stash and a LIVE strip CAN share a key (an entry still waiting on
    /// a window that has not re-announced, beside the strip the others
    /// rebuilt), which serializeStripState resolves by merging rather
    /// than by stamp.
    quint64 sequence = 0;

    bool isEmpty() const
    {
        return columns.isEmpty();
    }
    int tileCount() const
    {
        int total = 0;
        for (const StashedColumn& c : columns) {
            total += c.tiles.size();
        }
        return total;
    }
};

} // namespace PhosphorScrollEngine
