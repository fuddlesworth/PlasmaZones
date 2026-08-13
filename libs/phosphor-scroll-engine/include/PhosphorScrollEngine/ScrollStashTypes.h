// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollTypes.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementEngine.h>

#include <PhosphorProtocol/ScrollAxisEnum.h>

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
    /// on exact-id claims only (unlike minimized above; the fuzzy appId
    /// claim deliberately does not — see restoreFromStripStash): the flag
    /// is strip-owned state the compositor mirrors, so a restart must hand
    /// it back or the client stays fullscreen-configured with nothing on
    /// record saying so.
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
    /// The axis this stash was captured under.
    ///
    /// The stash is a THIRD carrier of axis-dependent state, alongside the
    /// live strip and the persisted blob, and the easiest of the three to
    /// forget: a scrolling -> snap -> scrolling round trip that spans a flip
    /// replays whatever was captured. The anchor is the piece that matters —
    /// it is main-axis pixels, so replaying one captured on the other axis
    /// scrolls the restored strip to a nonsense position.
    PhosphorProtocol::ScrollAxis axis = PhosphorProtocol::ScrollAxis::Horizontal;
    /// The strip's template blueprint cursor, carried across the round trip.
    ///
    /// Restoring the columns re-inserts them through paths that consume no
    /// blueprint entry, so the far side would otherwise recover spent-ness
    /// only as far as the qMax(cursor, columnCount) floor reaches — which is
    /// the LIVE column count. A strip that had opened four columns and closed
    /// two came back believing it had spent two, and handed entries 2 and 3
    /// out for a second time. Travelling with the structure is what makes the
    /// round trip lossless, exactly as focusedWindowId and viewAnchor do for
    /// focus and view.
    int blueprintCursor = 0;
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

/// What a floated/minimized window's column held, so unfloat restores the
/// slot AND the user's width/display intent (a Proportion/Preset column must
/// not come back as the default width). Namespace-level rather than nested in
/// ScrollEngine for the same file-size-ceiling reason as the stash types
/// above; lifetime and ownership are documented on the engine's
/// m_floatRestore member.
struct FloatRestore
{
    int column = -1;
    ColumnWidth width;
    ColumnDisplay display = ColumnDisplay::Normal;
    /// The tile slot inside a SHARED column (-1 when the window had its
    /// own column). A stacked tile's float round-trip re-enters its
    /// surviving stack instead of spawning a new column at the index.
    int tileIndex = -1;
    /// A surviving SIBLING of the shared column, used to re-locate the
    /// stack at restore time — the bare column index goes stale when
    /// columns close while the window floats, and a stale index would
    /// splice the window into a stranger's stack.
    QString stackAnchor;
    /// Client-reported minimum size at float time — the tile that held
    /// it dies with takeWindow, and dropping it would strip the
    /// relayout clamps until the compositor happens to re-report.
    /// Kept CURRENT while the window floats: windowMinSizeUpdated has no
    /// tile to write to then, and without the write-through the unfloat
    /// re-applies whatever the client reported at float time.
    int minWidth = 0;
    int minHeight = 0;
    /// The tile's height INTENT at float time. Same reasoning as the
    /// width/display above: without it a float round trip (which the
    /// effect's minimize machinery also drives) silently reset a
    /// user-set window height to Auto, while a mode round trip — which
    /// stashes the intent — preserved it.
    WindowHeight height;
    /// Windowed fullscreen, captured by the DRAG paths only
    /// (captureDragSlot): an Escape cancel is an exact restore, and the
    /// commit re-seats the tile, so both hand the flag back. The float
    /// capture (floatWindowInternal) deliberately leaves this false —
    /// float and windowed fullscreen are exclusive by design, and a float
    /// round trip is supposed to drop the flag.
    bool windowedFullscreen = false;
};

/// Live drag-insert preview state (drag_preview.cpp). See the engine's
/// m_dragInsertPreview member for the signal-silence and both-endings
/// announcement contract; hoisted here with FloatRestore, which it embeds.
struct DragInsertPreview
{
    QString windowId;
    QString targetScreenId;
    /// The context the preview inserted into, captured at begin so the
    /// prune paths can tell whether a dying context strands it.
    PhosphorEngine::PlacementStateKey targetKey;
    /// The most recent hit-tested drop target, stored verbatim —
    /// nothing structural happens until commit applies it.
    PhosphorEngine::IPlacementEngine::DragInsertTarget lastTarget;
    /// The window's OWN begin-time width/display/height/min-size
    /// intents. Never refreshed mid-drag: reading them from a transient
    /// host column stamped foreign widths across columns in the abandoned
    /// live-restructure design.
    ///
    /// How much of it commit applies depends on the drop. A NEW-COLUMN
    /// drop applies all of it. A JOIN discards width and display, because
    /// the window becomes a tile of a host column that already owns both,
    /// and only the height and min-size intents survive. That is a
    /// property of what a join means rather than an oversight, but the
    /// word "applied at commit" read as though the whole struct always
    /// made it through.
    FloatRestore carried;
    // ── cancel restoration ──
    /// Set when begin's defensive block took the window out of the
    /// TARGET strip despite it having no reverse-map entry (a stale
    /// forward state). That take is a real structural edit made with
    /// hadPriorState false, so cancel's "fresh adoption never touched
    /// anything" early return would abandon the window: out of the strip
    /// AND untracked, gone from the engine entirely. The slot it held is
    /// in defensiveSlot.
    bool defensivelyDetached = false;
    FloatRestore defensiveSlot;
    bool hadPriorState = false;
    PhosphorEngine::PlacementStateKey priorKey;
    /// Whole-key comparison (screen AND desktop AND activity): a
    /// same-screen/different-desktop prior context reads false.
    bool priorSameKey = false;
    bool priorFloating = false;
    /// The tiled slot at begin time (valid when !priorFloating).
    FloatRestore priorSlot;
    /// The m_floatRestore entry begin consumed when it silently
    /// unfloated the window; re-inserted verbatim on cancel.
    bool hadFloatRestoreEntry = false;
    FloatRestore floatRestoreEntry;
    bool wasScrollFloated = false;
};

} // namespace PhosphorScrollEngine
