// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Singleton registration is driven by the `QT_QML_SINGLETON_TYPE TRUE`
// source-file property in src/settings/CMakeLists.txt — see the comment on
// CurvePresets.qml for why the CMake property, not this pragma, is the
// load-bearing piece under our generator setup.
pragma Singleton

import QtQuick

/**
 * @brief How many pack previews may run at once, across every host.
 *
 * The cap exists for the ANIMATION family. An animation preview captures its
 * stand-in card into a texture every frame and drives per-frame simulation
 * (the move class's trail ring and wobble lattice) on top of it, so each live
 * one is a continuous capture plus a shader pass. A chain row that let every
 * expanded layer run its own would stack that cost without limit, and the
 * ChainEditor deliberately allows several rows open at once. Decoration and
 * pointer previews are far cheaper — a decoration chain is static once its
 * stages compile, and a pointer pack is one screen-space pass — so they would
 * not need a cap on their own. They share it because a single rule across the
 * three is honest about what a user sees: previews behave the same way
 * whichever family they are looking at.
 *
 * Newest wins. The preview a user just expanded is the one they are looking
 * at, so the oldest request is the one deactivated. A deactivated preview is
 * torn down, not hidden — that is the whole point of the cap — and it composes
 * again from scratch if it becomes the newest once more.
 *
 * A global singleton rather than per-host state, because the hosts are
 * unrelated: a decoration surface card, the pointer page and an animation
 * event card can all be scrolled into view in one session, and the GPU they
 * share does not care which page asked.
 */
QtObject {
    id: gate

    /// Live previews allowed at once.
    readonly property int maxLive: 2

    /// Requesting previews, oldest request first. Plain JS array: it is
    /// mutated by the functions below and never bound to, so no notify is
    /// needed. Each entry is a PackPreview, whose `_capAllowed` this owns.
    property var _queue: []

    /// Ask for a live slot. Idempotent, so a caller may re-request without
    /// losing its place in the queue.
    function request(item) {
        if (!item || gate._queue.indexOf(item) >= 0)
            return;
        gate._queue.push(item);
        gate._apply();
    }

    /// Give a slot back. A no-op for an item that never held one, which is
    /// what lets a destruction handler call it unconditionally.
    function release(item) {
        const at = gate._queue.indexOf(item);
        if (at < 0)
            return;
        gate._queue.splice(at, 1);
        gate._apply();
    }

    /// Grant the newest `maxLive` requests and revoke the rest.
    function _apply() {
        const firstAllowed = Math.max(0, gate._queue.length - gate.maxLive);
        for (var i = 0; i < gate._queue.length; ++i)
            gate._queue[i]._capAllowed = i >= firstAllowed;
    }
}
