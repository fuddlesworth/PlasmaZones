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
        // Fade is on the LOADER's opacity, not the loaded item's. The
        // prior shape did `item.opacity = 0` from onLoaded — an
        // imperative assignment that broke any declarative `opacity`
        // binding the page declared on its root (e.g. an
        // enabled-state opacity dim). Animating the Loader keeps the
        // loaded item's own binding intact while still producing the
        // page-swap fade.
        opacity: 0
        onLoaded: {
            if (!item)
                return;

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

            // Fade in the freshly-loaded page. The Loader's own
            // opacity (pinned at 0 above) is what we animate — the
            // loaded item's opacity is untouched so its declarative
            // bindings survive.
            pageFadeIn.restart();
        }

        PhosphorMotionAnimation {
            id: pageFadeIn

            target: pageLoader
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

    // If a page gets re-registered (dynamic registration after
    // startup) with a different controller, the already-loaded
    // page keeps the stale pointer. Listen for pageRegistered and
    // re-inject the controller when it matches the active page.
    Connections {
        function onPageRegistered(id) {
            if (id !== root.controller.currentPageId)
                return;
            if (!pageLoader.item)
                return;
            const pageController = root.controller.registry.controller(id);
            // A registration that explicitly passes a null controller
            // ("detach the controller" path) leaves the previously
            // injected pointer in place rather than clobbering it
            // with null — re-using a stale controller is preferable
            // to a runtime "TypeError: cannot read property of null"
            // in the page body. Consumers wanting the detach
            // behaviour should re-register with a placeholder
            // controller instead.
            if (!pageController)
                return;
            if (pageLoader.item.hasOwnProperty("controller")) {
                try {
                    pageLoader.item.controller = pageController;
                } catch (e) {
                    // Page binds its own controller (readonly) — skip.
                }
            }
        }

        target: root.controller.registry
    }
}
