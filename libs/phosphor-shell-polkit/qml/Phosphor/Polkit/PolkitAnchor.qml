// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Polkit.PolkitAnchor, where the prompt hangs from.
//
// The prompt is attached to the window that asked (A3 §9a): the
// requester's pid is looked up through the window tracker, and the
// window's cell rect through the screen's placement map. Both are
// duck-typed and optional:
//
//   windowTracking   an object with findWindowByPid(pid) -> window id
//                    (empty when unknown). Left null, or lacking the
//                    method, the prompt falls back to the screen edge.
//   placementMap     PlacementMapScreen or anything with cellRect(id)
//                    and changed(); the rect is in that screen's pixels.
//
// `anchored` is false whenever any link is missing: no pid, no tracker,
// no window, no cell. `epoch` re-reads the rect; the host bumps it on
// the map's changed() so the card follows a window that moves.
//
//   PolkitAnchor { pid: PolkitRegistry.requesterPid; placementMap: map }

import QtQuick

QtObject {
    id: anchor

    property int pid: 0
    property var windowTracking: null
    property var placementMap: null
    property int epoch: 0

    readonly property bool trackingAvailable: !!anchor.windowTracking && typeof anchor.windowTracking.findWindowByPid === "function"

    readonly property string windowId: {
        if (anchor.pid <= 0 || !anchor.trackingAvailable)
            return "";
        const id = anchor.windowTracking.findWindowByPid(anchor.pid);
        return id === undefined || id === null ? "" : String(id);
    }

    // The window's rect on this screen, or null.
    readonly property var rect: {
        // A binding dependency, so a map change re-reads the cell.
        const generation = anchor.epoch;
        const m = anchor.placementMap;
        if (generation < 0 || anchor.windowId === "" || !m || typeof m.cellRect !== "function")
            return null;
        const r = m.cellRect(anchor.windowId);
        return r && r.width > 0 && r.height > 0 ? r : null;
    }

    readonly property bool anchored: anchor.rect !== null
}
