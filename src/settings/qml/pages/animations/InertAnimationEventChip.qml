// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief "This event cannot be driven per window" chip for rule actions.
 *
 * Shown next to any animation rule action whose event is one the compositor
 * resolves WITHOUT a window: the desktop switches, the scrolling strip, the
 * OSDs, the panels, the editor's own widgets, and the whole shell subtree. A
 * rule's animation action is matched against a window, so on one of those
 * events the action is stored, listed, and never consulted.
 *
 * The picker no longer OFFERS such an event, so a rule can only carry one if it
 * was authored before that filter existed (or by hand). The chip is what makes
 * that rule legible rather than merely silent: without it, the row renders a
 * perfectly ordinary-looking action that does nothing, with nothing on screen
 * to say why.
 *
 * Deliberately NOT a ValidationIssue. Those gate `canSave` in RuleEditorBody,
 * so an existing rule naming `desktop.switch` would become unsaveable the
 * moment its owner opened it to rename it or flip its enabled state — a rule
 * that is inert is not broken enough to justify that. An informational chip is
 * the same treatment the stock-animation conflict gets, for the same reason.
 *
 * Shared by the rule editor's action row and the read-only rule summary so the
 * two surfaces cannot drift.
 */
Kirigami.Icon {
    id: chip

    /// The action JSON — `{ type, event, ... }`.
    required property var action
    /// AnimationsPageController, or null while the page is still wiring up.
    property var animationsController: null

    /// Every action type that carries an `event` param resolved through the
    /// rule tier. Spelled here rather than tested as "has an event key",
    /// because a future action could carry an event for some other purpose.
    readonly property var _animationActionTypes: ["overrideAnimationShader", "overrideAnimationTiming", "overrideAnimationCurve"]

    readonly property bool _inert: {
        if (!chip.action || chip._animationActionTypes.indexOf(chip.action.type) === -1)
            return false;

        var event = chip.action.event || "";
        if (event === "")
            return false;

        // No controller means the page is still wiring up. Stay hidden rather
        // than guessing: the alternative is flashing a warning on every action
        // for one frame during load.
        if (!chip.animationsController)
            return false;

        return !chip.animationsController.eventPathAcceptsWindowRules(event);
    }

    visible: _inert
    Layout.alignment: Qt.AlignVCenter
    Layout.preferredWidth: Kirigami.Units.iconSizes.small
    Layout.preferredHeight: Kirigami.Units.iconSizes.small
    source: "dialog-warning"
    Accessible.name: i18n("This event is not driven per window, so the rule cannot change it")
    ToolTip.visible: inertHover.hovered
    ToolTip.delay: Kirigami.Units.toolTipDelay
    ToolTip.text: i18n("Rules match windows, and this event does not belong to a window, so this action never runs. Set the animation for this event on the Animations pages instead, where it applies everywhere.")

    HoverHandler {
        id: inertHover
    }
}
