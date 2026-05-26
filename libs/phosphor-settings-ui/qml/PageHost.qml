// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Loads the QML page for ApplicationController.currentPageId.
 *
 * Sets two context properties on the loaded item if they exist:
 *   - controller: the registered PageController for the current id
 *   - settingsApp: the ApplicationController, for cross-page access
 */
Item {
    id: root

    required property ApplicationController controller

    readonly property var currentEntry:
        root.controller.currentPageId === ""
        ? null
        : root.controller.registry.pageData(root.controller.currentPageId)

    Loader {
        id: pageLoader
        anchors.fill: parent
        active: root.currentEntry !== null
        source: root.currentEntry ? root.currentEntry.qmlSource : ""

        onLoaded: {
            if (!item) {
                return;
            }
            const pageController = root.controller.registry.controller(
                root.controller.currentPageId);
            if (item.hasOwnProperty("controller")) {
                item.controller = pageController;
            }
            if (item.hasOwnProperty("settingsApp")) {
                item.settingsApp = root.controller;
            }
        }
    }

    // Empty state when no page is selected.
    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        visible: root.currentEntry === null
        text: qsTr("Select a page from the sidebar")
        icon.name: "settings-configure"
    }
}
