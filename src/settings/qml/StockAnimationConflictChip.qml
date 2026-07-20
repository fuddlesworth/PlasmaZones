// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Stock-animation conflict chip for rule actions.
 *
 * Shown next to an `overrideAnimationShader` action targeting the minimize
 * or maximize event when KDE's own effect for that event will run alongside
 * the rule's shader: a per-window rule cannot suppress the stock effect
 * (unloading it is global, so the runtime only does that for packs assigned
 * in the animation tree, never for rules). The chip therefore HIDES when a
 * tree-assigned pack already owns the event — the stock effect is unloaded
 * session-wide then, and there is no double-animation left to warn about
 * (`AnimationsPageController.stockSuppressedEvents` mirrors the compositor's
 * suppression gate and notifies on tree / registry / master-toggle changes).
 * With no controller available the chip falls back to warning on the basic
 * predicate alone. Gated on a non-empty effectId: an empty override is the
 * "no shader" sentinel and removes the conflict instead.
 *
 * Shared by the rule editor's action row and the read-only rule summary so
 * the two surfaces cannot drift. Same chip-plus-tooltip shape as the
 * editor's type-incompatibility warning.
 */
Kirigami.Icon {
    id: chip

    /// The action JSON — `{ type, event, effectId, ... }`.
    required property var action
    /// AnimationsPageController, or null while the page is still wiring up.
    property var animationsController: null

    readonly property bool _conflict: {
        if (!chip.action || chip.action.type !== "overrideAnimationShader")
            return false;

        if ((chip.action.effectId || "") === "")
            return false;

        if (chip.action.event !== "window.appearance.minimize" && chip.action.event !== "window.movement.maximize")
            return false;

        var suppressed = chip.animationsController ? chip.animationsController.stockSuppressedEvents : [];
        return suppressed.indexOf(chip.action.event) === -1;
    }

    visible: _conflict
    Layout.alignment: Qt.AlignVCenter
    Layout.preferredWidth: Kirigami.Units.iconSizes.small
    Layout.preferredHeight: Kirigami.Units.iconSizes.small
    source: "dialog-information"
    Accessible.name: i18n("May run alongside the KDE animation for this event")
    ToolTip.visible: conflictHover.hovered
    ToolTip.delay: Kirigami.Units.toolTipDelay
    ToolTip.text: chip.action && chip.action.event === "window.movement.maximize" ? i18n("A rule cannot turn off the KDE maximize animation for matched windows. If it is enabled in System Settings → Desktop Effects, both animations will play together.") : i18n("A rule cannot turn off the KDE minimize animation (Magic Lamp or Squash) for matched windows. If one is enabled in System Settings → Desktop Effects, both animations will play together.")

    HoverHandler {
        id: conflictHover
    }
}
