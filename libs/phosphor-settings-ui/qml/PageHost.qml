// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui
import "LoaderHelpers.js" as PhosphorLoaderHelpers

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
        // Async loading lets the chrome stay responsive during page
        // instantiation — important for heavy pages (motion-set list,
        // shader catalog) whose synchronous instantiation would
        // hitch the UI thread mid-navigation. The Loader.status gate
        // on opacity (below) keeps the previous page hidden while
        // the new one is still constructing instead of flashing an
        // empty viewport.
        asynchronous: true
        source: root.currentEntry ? root.currentEntry.qmlSource : ""
        // Fade is on the LOADER's opacity, not the loaded item's. The
        // prior shape did `item.opacity = 0` from onLoaded — an
        // imperative assignment that broke any declarative `opacity`
        // binding the page declared on its root (e.g. an
        // enabled-state opacity dim). Animating the Loader keeps the
        // loaded item's own binding intact while still producing the
        // page-swap fade.
        //
        // Opacity is pinned to 0 at construction. Source changes snap
        // it back to 0 imperatively (onSourceChanged below) so the
        // outgoing page disappears synchronously rather than waiting
        // for the new page to fade in over the top of it. The
        // pageFadeIn animation below restores opacity to 1 once
        // onLoaded fires. Combined with `asynchronous: true`, that
        // gives a symmetric fade: outgoing page goes invisible the
        // instant the source changes, viewport stays empty while the
        // new page constructs in the background, then the new page
        // fades up.
        opacity: 0
        onSourceChanged: {
            // Imperative reset, overriding the live `pageFadeIn`
            // animation if one is in-flight. Without this, swapping
            // from a fully visible page to a new source would leave
            // opacity at 1 until onLoaded fires — long enough on an
            // async page to read as a frozen-old-page flash.
            opacity = 0;
        }
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
            // Both injections go through the shared helper so the
            // hasOwnProperty + try-catch dance lives in one place.
            // Pages that bind their own readonly `controller` /
            // `settingsApp` Q_PROPERTY survive the assignment via the
            // helper's catch block.
            PhosphorLoaderHelpers.injectIfAssignable(item, "controller", pageController);
            PhosphorLoaderHelpers.injectIfAssignable(item, "settingsApp", root.controller);

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
            PhosphorLoaderHelpers.injectIfAssignable(pageLoader.item, "controller", pageController);
        }

        target: root.controller.registry
    }
}
