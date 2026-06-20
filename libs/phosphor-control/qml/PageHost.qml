// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.control
import "LoaderHelpers.js" as PhosphorLoaderHelpers

/**
 * Hosts the QML page for ApplicationController.currentPageId, KDE
 * System-Settings style: each visited page is built once and KEPT ALIVE
 * (hidden), so navigating back to it is an instant visibility flip rather
 * than a rebuild. This mirrors how `systemsettings` caches loaded KCMs.
 *
 * First-visit construction cost is reduced elsewhere — the settings app's
 * composition root (main.cpp) background-compiles every settings QML unit
 * (pages AND their child component types) after startup, so the first
 * navigation here pays instance construction only, not first-time
 * compilation of the page and its children. This Item only handles the
 * instance cache.
 *
 * Each loaded item gets two injections if it declares them:
 *   - controller: the registered PageController for that page id
 *   - settingsApp: the ApplicationController, for cross-page access
 */
Item {
    id: root

    required property ApplicationController controller
    /** Edge gutter applied around every loaded page so page content
     *  (cards, rows, breadcrumb-aligned headers) has breathing room
     *  instead of running flush to the chrome separators / window
     *  edge. Defaults to largeSpacing to match the UnsavedChangesFooter
     *  content inset and the breadcrumb row. */
    property real contentMargins: Kirigami.Units.largeSpacing

    // `pageData()` returns an empty QVariantMap when the id is unknown
    // (mid-startup before registration, or a stale id restored from disk).
    // An empty map marshals to a non-null JS object, so check for a valid
    // `id` field to fall through to the placeholder rather than rendering
    // an empty viewport.
    readonly property var currentEntry: {
        if (root.controller.currentPageId === "")
            return null;

        const data = root.controller.registry.pageData(root.controller.currentPageId);
        return (data && data.id) ? data : null;
    }

    // ── Instance cache ───────────────────────────────────────────────
    // Ordered list of page ids that have a live (built, cached) Loader.
    // Appended to the first time each page becomes current; the Repeater
    // below maps it to one persistent Loader per id. Never reordered, so a
    // page's Loader index is stable for the host's lifetime.
    property var cachedPageIds: []

    // Append `id` to the cache list if it names a real registered page and
    // is not already cached. slice()+push()+reassign so the change is a new
    // array reference — a plain in-place push would not notify the Repeater
    // model binding.
    function cachePage(id) {
        if (!id)
            return;
        const data = root.controller.registry.pageData(id);
        if (!data || !data.id)
            return;
        if (root.cachedPageIds.indexOf(id) !== -1)
            return;
        const next = root.cachedPageIds.slice();
        next.push(id);
        root.cachedPageIds = next;
    }

    Component.onCompleted: root.cachePage(root.controller.currentPageId)

    // Cache the page the instant it becomes current (covers normal
    // navigation, restored-on-startup ids, programmatic switches).
    Connections {
        function onCurrentPageIdChanged() {
            root.cachePage(root.controller.currentPageId);
        }

        target: root.controller
    }

    Repeater {
        id: pageRepeater

        model: root.cachedPageIds

        delegate: Loader {
            id: pageLoader

            required property string modelData
            readonly property string pageId: pageLoader.modelData
            readonly property bool isCurrent: root.controller.currentPageId === pageLoader.pageId

            // Deep-link reveal latch: when this page is built + current,
            // consume any pending anchor targeting it and invoke the page's
            // duck-typed revealAnchor() contract. Covers first build
            // (onLoaded), cached re-show (onIsCurrentChanged), and same-page
            // deep links (the pendingAnchorChanged Connections below).
            function _maybeReveal() {
                if (!pageLoader.item || !pageLoader.isCurrent || pageLoader.status !== Loader.Ready)
                    return;
                const anchor = root.controller.takePendingAnchor(pageLoader.pageId);
                if (anchor.length > 0 && typeof pageLoader.item.revealAnchor === "function")
                    pageLoader.item.revealAnchor(anchor);
            }

            anchors.fill: parent
            anchors.margins: root.contentMargins
            // Built once when this page first becomes current; the compiled
            // unit is already warm, so construction is the only cost. Async
            // so even a heavy first build never hitches the UI thread.
            asynchronous: true
            active: true
            source: {
                const data = root.controller.registry.pageData(pageLoader.pageId);
                return (data && data.id) ? data.qmlSource : "";
            }
            // Only the current page is shown + interactive. Hidden cached
            // pages stay built but are not painted and take no input.
            visible: pageLoader.isCurrent
            enabled: pageLoader.isCurrent
            z: pageLoader.isCurrent ? 1 : 0

            // Fade the page up when it becomes the current one. Pinned to 0
            // and driven imperatively (not `opacity: isCurrent ? 1 : 0` with
            // a `Behavior`): a Behavior never animates the INITIAL value, so
            // the first build — whose Loader is created already-current —
            // would snap to 1 with no fade; and on a cached re-show the
            // unspecified eval order of the opacity binding vs the Behavior's
            // `enabled` would drop the animation. Restarting the animation
            // from the state transition covers first load (onLoaded), cached
            // re-show (onIsCurrentChanged while already Ready), and resets to
            // 0 on hide so the next show fades again.
            opacity: 0
            onIsCurrentChanged: {
                if (pageLoader.isCurrent) {
                    if (pageLoader.status === Loader.Ready) {
                        pageFadeIn.restart();
                        pageLoader._maybeReveal();
                    }
                } else {
                    pageFadeIn.stop();
                    pageLoader.opacity = 0;
                }
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

            onLoaded: {
                if (!pageLoader.item)
                    return;

                const pageController = root.controller.registry.controller(pageLoader.pageId);
                // Pages that bind their own readonly `controller` /
                // `settingsApp` Q_PROPERTY reject the assignment under
                // Qt 6.11; the helper's try/catch swallows that so the page
                // still mounts. PageHost's injection is authoritative for
                // the rest.
                PhosphorLoaderHelpers.injectIfAssignable(pageLoader.item, "controller", pageController);
                PhosphorLoaderHelpers.injectIfAssignable(pageLoader.item, "settingsApp", root.controller);

                // Fade in if this page is the one being shown (covers the
                // common first-visit case: built async while current).
                if (pageLoader.isCurrent) {
                    pageFadeIn.restart();
                    pageLoader._maybeReveal();
                }
            }

            Connections {
                // Same-page deep link: the page is already built + current, so
                // neither onLoaded nor onIsCurrentChanged fires — react to the
                // pending-anchor signal directly.
                function onPendingAnchorChanged() {
                    pageLoader._maybeReveal();
                }

                target: root.controller
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

    // If a page is re-registered (dynamic registration after startup) with
    // a different controller, its already-built cached Loader keeps the
    // stale pointer. Re-inject into whichever cached Loader matches.
    Connections {
        function onPageRegistered(id) {
            const pageController = root.controller.registry.controller(id);
            // A registration that explicitly passes a null controller
            // ("detach" path) leaves the previously injected pointer in
            // place rather than clobbering it with null — a stale
            // controller beats a runtime null-deref in the page body.
            if (!pageController)
                return;
            for (let i = 0; i < pageRepeater.count; ++i) {
                const ld = pageRepeater.itemAt(i);
                if (ld && ld.pageId === id && ld.item) {
                    PhosphorLoaderHelpers.injectIfAssignable(ld.item, "controller", pageController);
                    return;
                }
            }
        }

        target: root.controller.registry
    }
}
