// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A "bring your own files" card: description, drop zone, result banner,
 *        and the Import / Open Folder actions.
 *
 * The shape the shader browser and the sets pages both need. Both used to carry
 * their own copy, and the copies had already drifted (one trimmed a trailing
 * slash off a dropped directory URL, the other did not).
 *
 * The host supplies the copy and the two actions. `importFn` takes the dropped
 * or chosen URL and returns whether it worked, which drives the banner. The
 * optional `openFolderFn` adds the "Open Folder" button, and `importFn` alone
 * is enough for a card whose only entry point is the drop zone.
 */
SettingsCard {
    id: card

    /// Called with a URL (dropped, or chosen in the host's file dialog).
    /// @return true when the import succeeded.
    required property var importFn
    /// Opens the destination folder in the file manager. Null hides the button.
    property var openFolderFn: null
    /// Opens the host's file dialog. Null hides the Import… button, leaving the
    /// drop zone as the only way in.
    property var openImportDialogFn: null

    property string description: ""
    property string idleText: ""
    property string hoverText: ""
    property string idleIcon: "folder-download"
    property string hoverIcon: "folder-add"
    /// Given a file's display name, the banner text for each outcome.
    required property var successTextFn
    required property var failureTextFn

    property string importButtonText: i18n("Import…")
    property string importButtonAccessibleName: ""
    property string openFolderAccessibleName: ""

    /// Show the result banner for @p url. Public so a host whose import runs
    /// from its own file dialog reports through the same banner as the drop.
    function showResult(ok, url) {
        // A dropped URL is percent-encoded, so decode before showing the name
        // ("My%20Pack" is not what the user called it). Some drag sources hand
        // over a directory URL with a trailing slash, which would otherwise
        // split to an empty tail.
        const trimmed = String(url).replace(/\/+$/, "");
        let basename = "";
        try {
            basename = decodeURIComponent(trimmed.split("/").pop());
        } catch (e) {
            basename = trimmed.split("/").pop();
        }
        if (basename.length === 0)
            basename = i18nc("@item fallback name for a file with no usable name", "the file");

        resultBanner.type = ok ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error;
        resultBanner.text = ok ? card.successTextFn(basename) : card.failureTextFn(basename);
        resultBanner.visible = true;
        autoHideTimer.restart();
    }

    Layout.fillWidth: true
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            text: card.description
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            visible: text.length > 0
        }

        FileDropZone {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            idleText: card.idleText
            hoverText: card.hoverText
            idleIcon: card.idleIcon
            hoverIcon: card.hoverIcon
            onFileDropped: function (url) {
                card.showResult(card.importFn(url) === true, url);
            }
        }

        Kirigami.InlineMessage {
            id: resultBanner

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: false
            showCloseButton: true

            Timer {
                id: autoHideTimer

                // Long enough to read the result without leaving a stale banner
                // pinned to the card.
                interval: Kirigami.Units.humanMoment * 3
                onTriggered: resultBanner.visible = false
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing

            Item {
                Layout.fillWidth: true
            }

            Button {
                visible: card.openImportDialogFn !== null
                text: card.importButtonText
                icon.name: "document-import"
                flat: true
                Accessible.name: card.importButtonAccessibleName
                onClicked: card.openImportDialogFn()
            }

            Button {
                visible: card.openFolderFn !== null
                text: i18n("Open Folder")
                icon.name: "folder-open"
                flat: true
                Accessible.name: card.openFolderAccessibleName
                onClicked: card.openFolderFn()
            }
        }
    }
}
