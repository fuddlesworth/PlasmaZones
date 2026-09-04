// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.IdleTile, the keep-awake toggle.
//
// Holds an idle inhibition for as long as the tile is on, so the screen
// does not blank and the machine does not suspend. The classic "keep
// awake" / "caffeine" control.
//
// The tile owns exactly one cookie and must retain it: the service never
// reuses cookies, so a discarded one leaks its inhibition for the whole
// process lifetime and nothing can ever release it. `active` is therefore
// keyed on OUR cookie rather than on the service's `inhibited` property,
// which is true whenever ANY holder has one — a video player's inhibition
// would otherwise light this tile up and, worse, make its first tap look
// like a release that does nothing.

import QtQuick
import Phosphor.ControlCenter
import Phosphor.Service.Idle

Tile {
    id: root

    // Injected by the host so the tile shares the shell's one ladder. A
    // tile-owned IdleService would arm a second, independent ladder.
    required property IdleService service

    // Our inhibition cookie, or -1 when we hold none. Cookies are 1-based,
    // so 0 is not a safe sentinel.
    property int _cookie: -1

    iconName: root._cookie !== -1 ? "my-caffeine-on" : "my-caffeine-off"
    label: qsTr("Keep awake")
    sublabel: {
        if (!root.service || !root.service.supported)
            return qsTr("Unavailable");
        if (root._cookie !== -1)
            return qsTr("Screen stays on");
        // Someone else is holding one. Worth showing, because the screen
        // will not blank and this tile is not the reason.
        if (root.service.inhibited)
            return qsTr("Held by another app");
        return qsTr("Off");
    }
    active: root._cookie !== -1
    available: root.service !== null && root.service.supported

    onToggled: {
        if (!root.service)
            return;
        if (root._cookie === -1) {
            root._cookie = root.service.inhibit();
            return;
        }
        root.service.release(root._cookie);
        root._cookie = -1;
    }

    // Release on teardown. A tile is destroyed on every shell reload, and
    // without this each reload would strand a cookie that keeps the
    // machine awake with nothing left to turn it off.
    Component.onDestruction: {
        if (root.service && root._cookie !== -1)
            root.service.release(root._cookie);
    }
}
