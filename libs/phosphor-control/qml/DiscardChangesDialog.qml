// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/**
 * "You have unsaved changes" close-confirmation dialog.
 *
 * Two modes:
 *   - Default (`applyAvailable: false`): two actions, Discard / Keep
 *     Editing. Emits `discardConfirmed()` on Discard; no signal on
 *     Keep.
 *   - 3-action (`applyAvailable: true`): adds an Apply button which
 *     emits `applyConfirmed()` — for apps whose preferred answer to
 *     "you have unsaved changes" is to commit them, not throw them
 *     away (matches the legacy Phosphor Apply / Discard / Cancel
 *     prompt).
 *
 * Stateless — caller is responsible for tracking whether changes
 * exist and for wiring the signals into the appropriate
 * controller.applyAll() / discardAll() / close() actions.
 */
Kirigami.PromptDialog {
    id: root

    /** When true, the dialog adds an Apply action that emits
     *  applyConfirmed() — typically wired to controller.applyAll()
     *  followed by closing the window. */
    property bool applyAvailable: false

    signal discardConfirmed
    signal applyConfirmed

    title: qsTr("Discard unsaved changes?")
    subtitle: root.applyAvailable ? qsTr("You have unsaved settings. Apply them now, or close without saving?") : qsTr("You have unsaved settings. Closing now will discard them.")
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: {
        const actions = [];
        if (root.applyAvailable)
            actions.push(applyAction);

        actions.push(discardAction);
        actions.push(keepAction);
        return actions;
    }

    Kirigami.Action {
        id: applyAction

        text: qsTr("Apply")
        icon.name: "dialog-ok-apply"
        onTriggered: {
            // Emit BEFORE close so observers can still inspect the dialog
            // (read applyAvailable, etc.) if they need to. close() schedules
            // teardown via the dialog's own animation, which runs after this
            // returns regardless.
            root.applyConfirmed();
            root.close();
        }
    }

    Kirigami.Action {
        id: discardAction

        text: qsTr("Discard")
        // `edit-undo` matches UnsavedChangesFooter's inline discard
        // button and discard-confirm prompt — one icon for "throw
        // away pending edits" across all of the chrome.
        icon.name: "edit-undo"
        onTriggered: {
            root.discardConfirmed();
            root.close();
        }
    }

    Kirigami.Action {
        id: keepAction

        text: root.applyAvailable ? qsTr("Cancel") : qsTr("Keep Editing")
        icon.name: "dialog-ok"
        onTriggered: root.close()
    }
}
