// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls

/**
 * @brief Context menu for a layout / autotile-algorithm card.
 *
 * Extracted from Main.qml, which instantiates it once at window scope and
 * exposes it to pages via the `window.layoutContextMenu` alias (pages call
 * `showForLayout(layout)` and connect to the delete / export signals). It
 * must be instantiated OUTSIDE any Loader — Qt 6 can SIGSEGV on Menu
 * destruction when a Loader tears the popup chain down.
 *
 * The aspect-ratio submenu is declared as a property value (not a child)
 * so QtQuick.Controls doesn't auto-append it as a static submenu item;
 * showForLayout() inserts / removes it imperatively per layout kind.
 */
Menu {
    id: layoutContextMenu

    // ── Wiring from Main.qml ────────────────────────────────────────
    /// The SettingsController context object — layout / algorithm
    /// actions (edit, duplicate, export, aspect ratio…) route through it.
    required property var settingsController
    /// The app-settings facade — default layout / algorithm and the
    /// auto-assign flags live here.
    required property var appSettings
    /// Canonical aspect-ratio key → localized label map
    /// (window.aspectRatioLabels). Read per-binding so locale changes
    /// propagate — see the WARNING on the map in Main.qml.
    required property var aspectRatioLabels

    property var layout: null
    /// Tracks the kind (`"snap"` / `"autotile"` / `"none"`) the
    /// aspect-ratio submenu was last reconciled to. showForLayout
    /// only mutates the menu when the current layout's kind
    /// differs from this state — Qt 6's auto-generated MenuItem
    /// placeholder is deleteLater'd on removeMenu and the inline
    /// submenu's reparenting back to its declared parent is
    /// unreliable enough that doing the dance on every show can
    /// lose the QML object after many cycles.
    property string _aspectRatioMenuKind: "none"
    readonly property bool isAutotile: layout && layout.isAutotile === true
    readonly property string layoutId: layout ? (layout.id || "") : ""
    // Cache the aspect-ratio options + screen list rather than
    // re-deriving them on every binding read. The Instantiator
    // delegate models below depend on these — re-evaluation on
    // each popup() round was tearing down and rebuilding every
    // delegate, which is precisely the Qt6 SIGSEGV class the
    // header comment warns about. The bindings still react to
    // `settingsController.screensChanged` so multi-monitor
    // hotplug is handled.
    // Named-key object form so the Instantiator delegate below can refer
    // to `modelData.key` / `modelData.label` / `modelData.index`
    // instead of positional `modelData[0]` / `[1]` / `[2]` — a future
    // edit that adds a field to one entry won't silently shift index
    // meanings on the others.
    // The `aspectRatioLabels` read is guarded: during creation this
    // binding can evaluate before Main.qml assigns the required
    // property, and a plain member read throws a TypeError on that
    // first pass (same failure mode documented on
    // KeyboardShortcutOverlay.qml's shortcutsModel and
    // EasingSettings.qml's appSettings resolution). The empty-object
    // fallback keeps the model sane until the property lands and the
    // binding re-runs.
    readonly property var _aspectRatioOptions: {
        const labels = layoutContextMenu.aspectRatioLabels || {};
        return [
            {
                "key": "any",
                "label": labels["any"],
                "index": 0
            },
            {
                "key": "standard",
                "label": labels["standard"],
                "index": 1
            },
            {
                "key": "ultrawide",
                "label": labels["ultrawide"],
                "index": 2
            },
            {
                "key": "super-ultrawide",
                "label": labels["super-ultrawide"],
                "index": 3
            },
            {
                "key": "portrait",
                "label": labels["portrait"],
                "index": 4
            }
        ];
    }
    // Memoise the screen list result. The getter still re-runs on
    // `settingsController.screensChanged`, but doesn't re-run on
    // each popup() / every `_screenItemsModel.length` read.
    // Cache the screens snapshot when there's more than one — the
    // multi-screen menu items only appear in that case. The binding
    // tracks settingsController.screens directly so a screensChanged
    // emit (e.g. daemon-driven hot-plug, late-arriving D-Bus reply)
    // refreshes the cache without needing a Connections + imperative
    // seed. Previously Component.onCompleted seeded once and missed
    // any value that arrived between settingsController construction
    // and Main.qml mount.
    // The `settingsController` deref is guarded for the same
    // creation-time reason as `_aspectRatioOptions` above: the binding
    // can run before Main.qml assigns the required property (precedent:
    // KeyboardShortcutOverlay.qml, EasingSettings.qml).
    readonly property var _cachedScreens: {
        const s = settingsController ? (settingsController.screens || []) : [];
        return s.length > 1 ? s : [];
    }
    readonly property var _screenItemsModel: _cachedScreens

    // Aspect-ratio submenu (added/removed imperatively by showForLayout).
    // Declared as a property VALUE rather than a child object — a Menu
    // declared as a direct child of a Menu is auto-appended as a static
    // submenu item, which would defeat the imperative reconciliation.
    // Referenced by id below; the holder property only anchors ownership.
    // Empty `enter` / `exit` transitions are the `finalizeExitTransition`
    // hardening pattern (mirrors the editor's metadata-preset menu):
    // synchronous close avoids the QQmlData::destroyed race Qt 6's
    // animated Menu teardown can otherwise hit.
    readonly property Menu _aspectRatioSubMenuHolder: Menu {
        id: aspectRatioSubMenu

        title: i18n("Aspect Ratio")
        icon.name: "view-fullscreen"

        Instantiator {
            id: aspectRatioItemInstantiator

            model: layoutContextMenu._aspectRatioOptions
            onObjectAdded: function (index, object) {
                aspectRatioSubMenu.insertItem(index, object);
            }
            onObjectRemoved: function (index, object) {
                aspectRatioSubMenu.removeItem(object);
            }

            delegate: ItemDelegate {
                required property var modelData
                readonly property string _arKey: (modelData && modelData.key) ? modelData.key : ""
                readonly property int _arIndex: (modelData && modelData.index !== undefined) ? modelData.index : 0
                readonly property bool isSelected: {
                    var current = (layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass) || "any";
                    return _arKey === current;
                }

                text: (modelData && modelData.label) ? modelData.label : ""
                icon.name: isSelected ? "checkmark" : ""
                Accessible.name: text
                onClicked: {
                    // SIGSEGV-avoidance — see the matching pattern in
                    // the per-screen edit MenuItem above. The submenu's
                    // visible-toggle + the layoutContextMenu close +
                    // the controller call all need to run AFTER the
                    // click event unwinds, otherwise the submenu
                    // dismissal can deref the click target's parent
                    // chain mid-event.
                    var layoutId = layoutContextMenu.layoutId;
                    var idx = _arIndex;
                    Qt.callLater(function () {
                        aspectRatioSubMenu.visible = false;
                        layoutContextMenu.visible = false;
                        settingsController.setLayoutAspectRatio(layoutId, idx);
                    });
                }
            }
        }

        enter: Transition {}

        exit: Transition {}
    }

    signal deleteRequested(var layout)
    signal exportRequested(string layoutId)

    function showForLayout(layout) {
        layoutContextMenu.layout = layout;
        var wantKind = layoutContextMenu.isAutotile ? "autotile" : "snap";
        if (wantKind !== layoutContextMenu._aspectRatioMenuKind) {
            if (wantKind === "snap") {
                var markerIdx = -1;
                for (var k = 0; k < layoutContextMenu.count; k++) {
                    if (layoutContextMenu.itemAt(k) === aspectRatioMarker) {
                        markerIdx = k;
                        break;
                    }
                }
                if (markerIdx >= 0)
                    layoutContextMenu.insertMenu(markerIdx + 1, aspectRatioSubMenu);
                else
                    layoutContextMenu.addMenu(aspectRatioSubMenu);
            } else {
                layoutContextMenu.removeMenu(aspectRatioSubMenu);
            }
            layoutContextMenu._aspectRatioMenuKind = wantKind;
        }
        layoutContextMenu.popup();
    }

    MenuItem {
        id: editMenuItem

        text: i18n("Edit")
        icon.name: "document-edit"
        onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
    }

    Instantiator {
        id: screenItemInstantiator

        model: layoutContextMenu._screenItemsModel
        onObjectAdded: function (index, object) {
            // Insert relative to the Edit marker — a future
            // MenuItem inserted before Edit would otherwise shift
            // the per-screen entries to the wrong slot.
            let editPos = 0;
            for (var k = 0; k < layoutContextMenu.count; k++) {
                if (layoutContextMenu.itemAt(k) === editMenuItem) {
                    editPos = k;
                    break;
                }
            }
            layoutContextMenu.insertItem(editPos + 1 + index, object);
        }
        onObjectRemoved: function (index, object) {
            layoutContextMenu.removeItem(object);
        }

        delegate: ItemDelegate {
            required property var modelData
            readonly property string _screenName: (modelData && modelData.name) ? modelData.name : ""

            text: i18n("Edit on %1", (modelData && modelData.displayLabel) || (modelData && modelData.name) || "")
            icon.name: (modelData && modelData.isPrimary) ? "starred-symbolic" : "monitor"
            Accessible.name: text
            onClicked: {
                // Capture by value because Qt.callLater fires after
                // the menu's onClicked stack unwinds — the model
                // delegate's row data is no longer guaranteed valid
                // (the Instantiator may rebuild on the same tick).
                var screenName = _screenName;
                var layoutId = layoutContextMenu.layoutId;
                // SIGSEGV-avoidance: setting `visible = false` on a
                // QtQuick.Controls Menu and then synchronously
                // invoking an action that tears down the parent
                // popup chain can deref the in-flight click target.
                // Defer the close + the controller call until after
                // the click event fully propagates.
                Qt.callLater(function () {
                    layoutContextMenu.visible = false;
                    if (screenName.length > 0)
                        settingsController.editLayoutOnScreen(layoutId, screenName);
                });
            }
        }
    }

    MenuSeparator {
        id: screenSeparator

        visible: layoutContextMenu._screenItemsModel.length > 0
    }

    MenuItem {
        text: i18n("Open in Text Editor")
        icon.name: "document-open"
        Accessible.name: text
        onTriggered: {
            if (layoutContextMenu.isAutotile)
                settingsController.openAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId));
            else
                settingsController.openLayoutFile(layoutContextMenu.layoutId);
        }
    }

    MenuSeparator {}

    MenuItem {
        text: i18n("Set as Default")
        icon.name: "favorite"
        enabled: {
            if (!layoutContextMenu.layout)
                return false;

            if (layoutContextMenu.isAutotile)
                return layoutContextMenu.layoutId !== ("autotile:" + layoutContextMenu.appSettings.defaultAutotileAlgorithm);

            return layoutContextMenu.layoutId !== layoutContextMenu.appSettings.defaultLayoutId;
        }
        onTriggered: {
            if (layoutContextMenu.isAutotile)
                layoutContextMenu.appSettings.defaultAutotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
            else
                layoutContextMenu.appSettings.defaultLayoutId = layoutContextMenu.layoutId;
        }
    }

    MenuItem {
        text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
        icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
        onTriggered: settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
    }

    MenuItem {
        readonly property bool perLayoutAuto: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true
        readonly property bool globalAuto: layoutContextMenu.appSettings.autoAssignAllLayouts === true

        text: globalAuto ? i18n("Auto-assign forced on (global setting)") : (perLayoutAuto ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign"))
        icon.name: (perLayoutAuto || globalAuto) ? "window-duplicate" : "window-new"
        visible: !layoutContextMenu.isAutotile
        enabled: !globalAuto
        onTriggered: settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !perLayoutAuto)
    }

    // Sentinel separators that flank the aspect-ratio submenu's
    // insert position. showForLayout() inserts the submenu between
    // them (after `aspectRatioMarker`) in snap mode and removes it
    // in autotile mode. Gate visibility on both `!isAutotile` AND
    // the actual reconciled menu kind tracked in
    // `_aspectRatioMenuKind` — relying solely on `!isAutotile`
    // would show two empty separators during the brief window
    // between layout assignment and showForLayout()'s
    // insertMenu/removeMenu reconciliation when the menu rebuilds
    // (e.g. a layout swap in-place).
    MenuSeparator {
        id: aspectRatioMarker

        visible: !layoutContextMenu.isAutotile && layoutContextMenu._aspectRatioMenuKind === "snap"
    }

    MenuSeparator {
        visible: !layoutContextMenu.isAutotile && layoutContextMenu._aspectRatioMenuKind === "snap"
    }

    MenuItem {
        text: i18n("Duplicate")
        icon.name: "edit-copy"
        visible: !layoutContextMenu.isAutotile
        onTriggered: settingsController.duplicateLayout(layoutContextMenu.layoutId)
    }

    MenuItem {
        text: i18n("Export")
        icon.name: "document-export"
        visible: !layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
    }

    MenuSeparator {
        visible: layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
    }

    MenuItem {
        text: i18n("Delete")
        icon.name: "edit-delete"
        visible: layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
    }

    MenuSeparator {
        visible: layoutContextMenu.isAutotile
    }

    MenuItem {
        text: i18n("Duplicate")
        icon.name: "edit-copy"
        visible: layoutContextMenu.isAutotile
        onTriggered: settingsController.duplicateAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId))
    }

    MenuItem {
        text: i18n("Export")
        icon.name: "document-export"
        visible: layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
    }

    MenuSeparator {
        visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
    }

    MenuItem {
        text: i18n("Delete")
        icon.name: "edit-delete"
        visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
        onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
    }
}
