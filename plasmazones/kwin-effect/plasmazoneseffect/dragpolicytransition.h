// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/DragTypes.h>

#include <QString>

#include <optional>

/// What the effect does when it adopts a drag policy from the daemon,
/// header-only so the rules are unit-testable without a compositor (same
/// pattern as tilinghandler/scrolldecisions.h). It decides, it does not act:
/// PlasmaZonesEffect::applyDragPolicyTransition carries the plan out. Two
/// sources adopt a policy, the beginDrag reply (the daemon's answer for the
/// drag's start) and a mid-drag flip (dragPolicyChanged, the cursor reached a
/// screen that places differently), and both go through this one plan.
///
/// The rules it encodes:
/// - The keyboard grab follows the daemon's answer on every adopted policy,
///   both ways, and never while the daemon is gone: a grab swallows every key
///   for the drag, so holding one the daemon declined, or one nothing will
///   answer, takes Escape away from KWin's move for no gain.
/// - Entering an engine-owned screen latches the bypass and sends nothing:
///   the daemon's own flip already hid what the snap path showed, and a
///   cancelSnap there latched the cancel for the rest of the drag, so a
///   return to the snapping screen could never snap.
/// - The bypass is left only toward the canonical snap path (None). A dead
///   reason (snapping or the context disabled, no layout) keeps the latch and
///   only reconciles the grab, the same rule the beginDrag reply always had.
///   The LATCH, not the previous reason, says whether there is a bypass to
///   leave: the drag-start fast path can latch while the policy still holds
///   the conservative default.
/// - Leaving on a flip suspends the tile effect-side and keeps it tracked: the
///   daemon still holds it, and the drop decides (a drop back on the engine
///   screen finds the tile, a snap elsewhere releases it). Leaving on the
///   reply untracks it: the daemon answered that no engine owns the start.
/// - A tile reaching a screen whose engine floats on drag takes its free size
///   at once. A flip floats only a window the effect holds as a tile; the
///   reply corrects a start the effect may have misread, so its answer
///   stands without that test.
namespace PlasmaZones::DragPolicyTransition {

enum class Source {
    Flip, ///< dragPolicyChanged, mid-drag
    BeginDragReply, ///< the daemon's answer for the drag's start
};

enum class Leave {
    None,
    SuspendTile, ///< a flip back to the snap path: keep the tile tracked
    Untrack, ///< the reply: no engine owns the start, drop the stale tracking
};

struct Input
{
    PhosphorProtocol::DragBypassReason oldReason = PhosphorProtocol::DragBypassReason::None;
    PhosphorProtocol::DragPolicy policy;
    Source source = Source::Flip;
    bool bypassLatched = false; ///< m_dragBypassedForEngine
    bool keyboardGrabbed = false; ///< m_keyboardGrabbed
    bool daemonUp = false; ///< the daemon's service is registered
    bool windowLive = false; ///< the dragged window exists and is not deleted
    bool tileHeld = false; ///< the effect tracks the window as a tile
    bool floating = false; ///< the window floats already
    bool floatedThisDrag = false; ///< this drag already floated it
};

struct Plan
{
    bool enterBypass = false;
    /// Leaving the bypass also resets the drag's activation latch.
    Leave leave = Leave::None;
    /// nullopt keeps the bypass screen, an empty string clears it.
    std::optional<QString> bypassScreen;
    bool floatNow = false;
    bool grab = false;
    bool ungrab = false;
};

inline Plan plan(const Input& in)
{
    using R = PhosphorProtocol::DragBypassReason;
    Plan p;
    const bool engine = in.policy.bypassReason == R::EngineOwnedScreen;
    if (engine) {
        p.enterBypass = !in.bypassLatched;
        p.bypassScreen = in.policy.screenId;
    } else if (in.bypassLatched && in.policy.bypassReason == R::None) {
        p.leave = in.source == Source::Flip ? Leave::SuspendTile : Leave::Untrack;
        p.bypassScreen = QString();
    }
    p.floatNow = engine && in.policy.immediateFloatOnStart && in.windowLive && !in.floating && !in.floatedThisDrag
        && (in.tileHeld || in.source == Source::BeginDragReply);
    const bool wantGrab = in.daemonUp && in.policy.grabKeyboard;
    p.grab = wantGrab && !in.keyboardGrabbed;
    p.ungrab = !wantGrab && in.keyboardGrabbed;
    return p;
}

} // namespace PlasmaZones::DragPolicyTransition
