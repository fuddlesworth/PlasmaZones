// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Confirmation prompt shown when the user attempts to close a rule
 *        editor (the edit `RuleEditorSheet` or the create `AddRuleWizard`)
 *        with unsaved edits. Two paths:
 *
 *          - "Discard changes" → emits `discardConfirmed` so the host closes
 *            for real.
 *          - "Keep editing"    → just closes the prompt; the host stays open.
 *
 * Shared between the edit sheet and the wizard so the wording / button set is
 * defined exactly once. The host owns the actual close — this dialog only
 * surfaces intent.
 *
 * Signal is named `discardConfirmed` (not `discarded`) so it does not collide
 * with `Kirigami.Dialog`'s built-in `discarded()` signal — declaring a same-
 * named signal on the derived type would be a duplicate-signal QML error and
 * the host could not distinguish the two intents.
 */
Kirigami.PromptDialog {
    id: root

    /// Emitted when the user picks "Discard changes". The host (the editor
    /// sheet or wizard) listens for this and closes itself.
    signal discardConfirmed

    title: i18nc("@title:window", "Discard unsaved changes?")
    subtitle: i18n("Your edits to this window rule will be lost. Discard them?")
    standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
    // `discarded` is the built-in signal Kirigami.Dialog emits when the user
    // picks the Discard standard button. Forward it to our explicit
    // `discardConfirmed` so the host wires against a single, clearly-named
    // intent rather than the framework's generic signal.
    onDiscarded: {
        root.close();
        root.discardConfirmed();
    }
}
