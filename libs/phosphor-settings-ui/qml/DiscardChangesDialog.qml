// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/**
 * "You have unsaved changes" close-confirmation dialog.
 *
 * Emits discardConfirmed() when the user confirms; emits no signal on
 * Cancel (caller should keep the window open). Stateless — caller is
 * responsible for tracking whether changes exist.
 */
Kirigami.PromptDialog {
    id: root

    signal discardConfirmed()

    title: qsTr("Discard unsaved changes?")
    subtitle: qsTr("You have unsaved settings. Closing now will discard them.")
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Discard")
            icon.name: "dialog-cancel"
            onTriggered: {
                root.close();
                root.discardConfirmed();
            }
        },
        Kirigami.Action {
            text: qsTr("Keep Editing")
            icon.name: "dialog-ok"
            onTriggered: root.close()
        }
    ]
}
