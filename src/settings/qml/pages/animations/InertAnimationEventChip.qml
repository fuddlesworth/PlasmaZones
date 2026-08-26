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
 * WHICH REMEDY THE TOOLTIP OFFERS. Not every windowless event can be set on the
 * Animations pages either, so the wording branches. Of the events this chip can
 * appear on, the `editor.*`, `widget.*`, `panel.*`, `cursor.*` and `shader.*`
 * families plus `osd.pop` carry no shader leg at all — `shaderConsumedLeafEventPaths`
 * excludes them and AnimationProfileEditor hides its whole shader section on
 * them. Telling the owner of such a rule to go and set the shader there would
 * send them to a page with no shader picker on it, so on that combination the
 * tooltip says the event takes no shader instead of pointing anywhere.
 *
 * Deliberately NOT a ValidationIssue. Those gate `canSave` in RuleEditorBody,
 * so an existing rule naming `desktop.switch` would become unsaveable the
 * moment its owner opened it to rename it or flip its enabled state, and a rule
 * that is inert is not broken enough to justify that. The stock-animation
 * conflict chip takes the same shape for the same reason. It uses an
 * information glyph where this one uses a warning glyph, because a doubled
 * animation still runs whereas this action never does.
 *
 * BOUNDARY. `_inert` requires the event to be a real taxonomy path, so a rule
 * carrying a typo or a path some other build removed stays unmarked and renders
 * as `(missing: ...)` in the picker, which is the honest reading for it. A
 * category path such as `window.appearance` is a real taxonomy path and would
 * be marked, but no shipped surface can store one: the picker skips category
 * rows, and the C++ seeds the param empty.
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
    readonly property list<string> _animationActionTypes: ["overrideAnimationShader", "overrideAnimationTiming", "overrideAnimationCurve"]

    /// The action's event path, or "" when this action carries none.
    readonly property string _event: {
        if (!chip.action || chip._animationActionTypes.indexOf(chip.action.type) === -1)
            return "";
        return chip.action.event || "";
    }

    readonly property bool _inert: {
        if (chip._event === "")
            return false;

        // No controller means the page is still wiring up. Stay hidden rather
        // than guessing: the alternative is flashing a warning on every action
        // for one frame during load.
        if (!chip.animationsController)
            return false;

        // An event this build does not know is not "not per window", it is not
        // an event. The picker already says so by rendering "(missing: ...)".
        if (!chip.animationsController.isValidEventPath(chip._event))
            return false;

        return !chip.animationsController.eventPathAcceptsWindowRules(chip._event);
    }

    /// True when the remedy the tooltip would otherwise name does not exist:
    /// a shader override on an event that carries no shader leg anywhere.
    readonly property bool _noShaderAnywhere: chip._inert && chip.action.type === "overrideAnimationShader" && !chip.animationsController.supportsShaderLeg(chip._event)

    visible: _inert
    Layout.alignment: Qt.AlignVCenter
    Layout.preferredWidth: Kirigami.Units.iconSizes.small
    Layout.preferredHeight: Kirigami.Units.iconSizes.small
    source: "dialog-warning"
    // Icon-only, so it needs a role of its own or a screen reader passes over
    // the one thing on screen that explains why the rule does nothing.
    Accessible.role: Accessible.AlertMessage
    Accessible.name: i18n("This event is not driven per window, so the rule cannot change it")
    Accessible.description: ToolTip.text
    // Reachable by keyboard as well as by pointer, for the same reason.
    activeFocusOnTab: true
    ToolTip.visible: inertHover.hovered || chip.activeFocus
    ToolTip.delay: Kirigami.Units.toolTipDelay
    ToolTip.text: chip._noShaderAnywhere ? i18n("Rules match windows, and this event does not belong to a window, so this action never runs. This event takes no shader anywhere, so the action can be removed.") : i18n("Rules match windows, and this event does not belong to a window, so this action never runs. Set the animation for this event on the Animations pages instead, where it applies everywhere.")

    HoverHandler {
        id: inertHover
    }
}
