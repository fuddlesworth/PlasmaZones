// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui

/**
 * Loads the QML page for ApplicationController.currentPageId.
 *
 * Sets two context properties on the loaded item if they exist:
 *   - controller: the registered PageController for the current id
 *   - settingsApp: the ApplicationController, for cross-page access
 *
 * Each loaded page is faded in via a 180 ms widget.fadeIn motion
 * profile so navigating between pages reads as a soft transition
 * rather than an instantaneous swap.
 */
Item {
    id: root

    required property ApplicationController controller
    // `pageData()` returns an empty QVariantMap when the id is unknown
    // (e.g. mid-startup before registration completes, or a stale id
    // restored from disk). An empty map marshals to a non-null JS
    // object — so we must check for a valid `id` field to fall
    // through to the placeholder rather than rendering an empty
    // viewport with no page and no fallback message.
    readonly property var currentEntry: {
        if (root.controller.currentPageId === "")
            return null;

        const data = root.controller.registry.pageData(root.controller.currentPageId);
        return (data && data.id) ? data : null;
    }

    Loader {
        id: pageLoader

        anchors.fill: parent
        active: root.currentEntry !== null
        source: root.currentEntry ? root.currentEntry.qmlSource : ""
        onLoaded: {
            if (!item)
                return;

            // Pin opacity to 0 BEFORE the animation starts. Without this,
            // the new item's default opacity is 1 for the first paint
            // frame between item construction and the first PhosphorMotion
            // tick — produces a brief at-full-opacity flash before the
            // fade-in actually begins.
            item.opacity = 0;
            const pageController = root.controller.registry.controller(root.controller.currentPageId);
            // Pages that already have their own readonly `controller`
            // binding (e.g. `readonly property var controller:
            // settingsController.windowRulesPage`) reject the
            // assignment under Qt 6.11. That's fine — the page
            // already has a controller; PageHost's injection is
            // redundant. Try/catch swallows the TypeError so the
            // page still loads instead of failing to mount entirely.
            if (item.hasOwnProperty("controller")) {
                try {
                    item.controller = pageController;
                } catch (e) {
                    // Page has its own readonly controller binding —
                    // skip the injection, the page is wired through
                    // its own channel.
                }
            }

            if (item.hasOwnProperty("settingsApp")) {
                try {
                    item.settingsApp = root.controller;
                } catch (e) {
                    // Same readonly tolerance as `controller`.
                }
            }

            // Fade in the freshly-loaded page from opacity 0. The
            // animation starts immediately because the Loader has just
            // finished synchronously instantiating its item; the
            // restart() resets `from: 0` even when the previous
            // animation was mid-flight (rapid page switches).
            pageFadeIn.restart();
        }

        PhosphorMotionAnimation {
            id: pageFadeIn

            target: pageLoader.item
            properties: "opacity"
            from: 0
            to: 1
            profile: "widget.fadeIn"
            durationOverride: 180
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
