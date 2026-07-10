// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls

/**
 * @brief Checkable MenuItem that keeps its Menu open when toggled.
 *
 * A stock MenuItem emits triggered() on activation and Menu dismisses on any
 * item trigger, with no opt-out in QtQuick Controls (as of Qt 6.11). Filter
 * menus made of multi-select checkboxes need to survive a toggle, so this
 * item intercepts both activation paths — the mouse click via an overlay
 * MouseArea, the keyboard via Keys — before they reach the AbstractButton
 * internals, flips `checked` itself, and emits toggled(). Consumers keep the
 * stock `onToggled` contract; the menu only closes on Escape, click-outside,
 * or an explicit close.
 */
MenuItem {
    id: root

    checkable: true

    function _isActivationKey(key) {
        // Space is AbstractButton's press key; Return/Enter activate menu
        // items; Select is the activation key on some platform themes.
        return key === Qt.Key_Space || key === Qt.Key_Return || key === Qt.Key_Enter || key === Qt.Key_Select;
    }
    // Programmatic setChecked() does not emit toggled(), so emit it manually —
    // exactly once — after flipping.
    function _toggleWithoutDismiss() {
        if (!enabled)
            return;
        checked = !checked;
        toggled();
    }

    // Swallow the activation keys on press AND release so AbstractButton's
    // internal trigger (which emits triggered() → menu dismiss) never runs.
    // The highlighted item holds active focus inside the Menu, so these
    // handlers see the keys first.
    Keys.onPressed: event => {
        if (_isActivationKey(event.key)) {
            if (!event.isAutoRepeat)
                _toggleWithoutDismiss();
            event.accepted = true;
        }
    }
    Keys.onReleased: event => {
        if (_isActivationKey(event.key))
            event.accepted = true;
    }

    // Sits on top of the item and consumes the click before the MenuItem sees
    // it. Hover is untouched (hoverEnabled off), so the menu's highlight-
    // follows-mouse behavior is unaffected.
    MouseArea {
        anchors.fill: parent
        onClicked: root._toggleWithoutDismiss()
    }
}
