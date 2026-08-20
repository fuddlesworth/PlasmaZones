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
    /// aspect-ratio submenu was last reconciled to. Scrolling templates
    /// map onto `"autotile"` here: neither carries zones, so both want
    /// the submenu taken away, and no third state buys anything.
    /// showForLayout
    /// only mutates the menu when the current layout's kind
    /// differs from this state — Qt 6's removeMenu() DESTROYS the
    /// Menu itself (docs: "Removes and destroys the specified
    /// menu"), not just its auto-generated MenuItem placeholder,
    /// which is why the autotile branch uses takeMenu() (transfers
    /// ownership to the caller, keeping aspectRatioSubMenu alive
    /// for the next snap-layout show). Doing the dance on every
    /// show would also churn the popup chain needlessly.
    property string _aspectRatioMenuKind: "none"
    // Boolean-coerced: `layout` is untyped `var` and defaults to null, and the
    // `&&` chain yields the first falsy operand rather than `false`. Wrapped
    // rather than leaning on QML's null-to-false coercion, because the same
    // shape is documented elsewhere in this tree as a "Cannot assign
    // [undefined] to bool" hazard.
    readonly property bool isAutotile: Boolean(layout && layout.isAutotile === true)
    readonly property bool isTemplate: Boolean(layout && layout.isScrollingTemplate === true)
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
    // the animation pages' appSettings resolution). The empty-object
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
    // Memoised screens snapshot, held only when there is more than one screen
    // (the multi-screen menu items appear in that case alone). The binding
    // tracks settingsController.screens directly, so a screensChanged emit
    // (daemon-driven hot-plug, a late-arriving D-Bus reply) refreshes it with
    // no Connections + imperative seed, and it does not re-run on each popup()
    // or on every `.length` read. Previously Component.onCompleted seeded once
    // and missed any value that arrived between settingsController construction
    // and Main.qml mount.
    // The `settingsController` deref is guarded for the same
    // creation-time reason as `_aspectRatioOptions` above: the binding
    // can run before Main.qml assigns the required property (precedent:
    // KeyboardShortcutOverlay.qml).
    readonly property var _cachedScreens: {
        const s = layoutContextMenu.settingsController ? (layoutContextMenu.settingsController.screens || []) : [];
        return s.length > 1 ? s : [];
    }

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
                        layoutContextMenu.settingsController.setLayoutAspectRatio(layoutId, idx);
                    });
                }
            }
        }

        enter: Transition {}

        exit: Transition {}
    }

    signal deleteRequested(var layout)
    /// Carries the card kind so the page routes to the template export
    /// dialog without having to infer it from its own view mode.
    signal exportRequested(string layoutId, bool isTemplateExport)

    function showForLayout(nextLayout) {
        layoutContextMenu.layout = nextLayout;
        var wantKind = (layoutContextMenu.isAutotile || layoutContextMenu.isTemplate) ? "autotile" : "snap";
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
                // takeMenu(), NOT removeMenu(): removeMenu destroys the
                // Menu, so the next snap-layout show would insert null and
                // the submenu would be gone for good. takeMenu transfers
                // ownership back to us and keeps aspectRatioSubMenu alive.
                for (var m = 0; m < layoutContextMenu.count; m++) {
                    if (layoutContextMenu.menuAt(m) === aspectRatioSubMenu) {
                        layoutContextMenu.takeMenu(m);
                        break;
                    }
                }
            }
            layoutContextMenu._aspectRatioMenuKind = wantKind;
        }
        layoutContextMenu.popup();
    }

    MenuItem {
        id: editMenuItem

        text: i18n("Edit")
        icon.name: "document-edit"
        // Both arms route straight to the controller: a template card's Edit
        // launches the editor process in template mode, so the page-owned
        // signal hop the old in-process dialog needed is gone.
        onTriggered: {
            if (layoutContextMenu.isTemplate)
                layoutContextMenu.settingsController.editScrollingTemplate(layoutContextMenu.layoutId);
            else
                layoutContextMenu.settingsController.editLayout(layoutContextMenu.layoutId);
        }
    }

    Instantiator {
        id: screenItemInstantiator

        // The model stays the STABLE cached screen list: folding isTemplate
        // into it would destroy and recreate every delegate (via
        // removeItem, which also destroys) on each template-to-layout kind
        // switch — the churn class the header comment warns about. Template
        // cards hide the entries declaratively instead (with an explicit
        // height collapse, belt and braces against the Menu keeping a slot
        // for an invisible item).
        model: layoutContextMenu._cachedScreens
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

            // Per-screen entries make no sense for a template: the editor's
            // template mode is screen-agnostic (openEditorForScrollingTemplate
            // takes no screen argument; the strip opens under the cursor).
            visible: !layoutContextMenu.isTemplate
            height: visible ? implicitHeight : 0
            text: i18n("Edit on %1", (modelData && modelData.displayLabel) || (modelData && modelData.name) || "")
            icon.name: (modelData && modelData.isPrimary) ? "starred-symbolic" : "monitor"
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
                        layoutContextMenu.settingsController.editLayoutOnScreen(layoutId, screenName);
                });
            }
        }
    }

    MenuSeparator {
        // The per-screen block above is emptied for templates, so the
        // separator would otherwise sit against nothing.
        visible: layoutContextMenu._cachedScreens.length > 0 && !layoutContextMenu.isTemplate
    }

    MenuItem {
        text: i18n("Open in Text Editor")
        icon.name: "document-open"
        onTriggered: {
            if (layoutContextMenu.isTemplate)
                layoutContextMenu.settingsController.openScrollingTemplateFile(layoutContextMenu.layoutId);
            else if (layoutContextMenu.isAutotile)
                layoutContextMenu.settingsController.openAlgorithm(layoutContextMenu.settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId));
            else
                layoutContextMenu.settingsController.openLayoutFile(layoutContextMenu.layoutId);
        }
    }

    MenuSeparator {}

    MenuItem {
        id: defaultItem

        // Whether THIS entry is already its family's default. Drives the
        // enabled state for the two layout families, and for templates it
        // flips the item into its clearing form instead.
        readonly property bool isFamilyDefault: {
            if (!layoutContextMenu.layout)
                return false;

            if (layoutContextMenu.isTemplate)
                return layoutContextMenu.layoutId === layoutContextMenu.appSettings.defaultScrollingTemplate;

            if (layoutContextMenu.isAutotile)
                return layoutContextMenu.layoutId === ("autotile:" + layoutContextMenu.appSettings.defaultAutotileAlgorithm);

            return layoutContextMenu.layoutId === layoutContextMenu.appSettings.defaultLayoutId;
        }

        // All three families can CLEAR their default. Templates clear to an
        // empty slot (the engine falls back to its built-in width and height
        // steps); snapping and tiling clear to the reserved no-layout word,
        // because for them an empty slot means "inherit the bundled pick" —
        // the sentinel is what makes an unassigned screen genuinely get no
        // zones / no tiling, the explicit no-layout state the assignments
        // already carry per context.
        text: defaultItem.isFamilyDefault ? i18n("Clear Default") : i18n("Set as Default")
        icon.name: defaultItem.isFamilyDefault ? "edit-clear" : "favorite"
        enabled: Boolean(layoutContextMenu.layout)
        onTriggered: {
            // The active page here is a library page, which is never dirty
            // (its store writes immediately). Each family's default key is a
            // staged config key owned by a settings page, so wrap the write in
            // an external-edit envelope naming the owner: value-based
            // dirtiness, the badge, and per-page Discard all land on the page
            // that can actually revert it. The guard runs BEFORE the envelope
            // opens: QML has no RAII, so a throw between begin and end would
            // strand the external-edit stack for the rest of the session.
            if (!layoutContextMenu.appSettings)
                return;
            if (layoutContextMenu.isTemplate) {
                layoutContextMenu.settingsController.beginExternalEdit("scrolling-columns");
                layoutContextMenu.appSettings.defaultScrollingTemplate = defaultItem.isFamilyDefault ? "" : layoutContextMenu.layoutId;
                layoutContextMenu.settingsController.endExternalEdit();
            } else if (layoutContextMenu.isAutotile) {
                // Clearing writes the reserved word (PhosphorZones::
                // NoTilingAlgorithm), not an empty string: the setting
                // defaults to a concrete algorithm, so only the sentinel
                // expresses "no default algorithm" (unassigned screens stay
                // in tiling mode but nothing tiles).
                layoutContextMenu.settingsController.beginExternalEdit("tiling-algorithm");
                layoutContextMenu.appSettings.defaultAutotileAlgorithm = defaultItem.isFamilyDefault ? "none" : layoutContextMenu.layoutId.replace("autotile:", "");
                layoutContextMenu.settingsController.endExternalEdit();
            } else {
                // Same shape for snapping (PhosphorZones::NoSnappingLayout):
                // an EMPTY defaultLayoutId falls back to the first registered
                // layout, so only the sentinel gives unassigned screens no
                // zones at all.
                layoutContextMenu.settingsController.beginExternalEdit("snapping-window-behavior");
                layoutContextMenu.appSettings.defaultLayoutId = defaultItem.isFamilyDefault ? "none" : layoutContextMenu.layoutId;
                layoutContextMenu.settingsController.endExternalEdit();
            }
        }
    }

    MenuItem {
        text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
        icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
        visible: !layoutContextMenu.isTemplate
        onTriggered: layoutContextMenu.settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
    }

    MenuItem {
        readonly property bool perLayoutAuto: Boolean(layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true)
        readonly property bool globalAuto: Boolean(layoutContextMenu.appSettings && layoutContextMenu.appSettings.autoAssignAllLayouts === true)

        text: globalAuto ? i18n("Auto-assign forced on (global setting)") : (perLayoutAuto ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign"))
        icon.name: (perLayoutAuto || globalAuto) ? "window-duplicate" : "window-new"
        visible: !layoutContextMenu.isAutotile && !layoutContextMenu.isTemplate
        enabled: !globalAuto
        onTriggered: layoutContextMenu.settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !perLayoutAuto)
    }

    // Sentinel separators that flank the aspect-ratio submenu's
    // insert position. showForLayout() inserts the submenu between
    // them (after `aspectRatioMarker`) in snap mode and removes it
    // in autotile mode. Gate visibility on both `!isAutotile` AND
    // the actual reconciled menu kind tracked in
    // `_aspectRatioMenuKind` — relying solely on `!isAutotile`
    // would show two empty separators during the brief window
    // between layout assignment and showForLayout()'s
    // insertMenu/takeMenu reconciliation when the menu rebuilds
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
        onTriggered: {
            if (layoutContextMenu.isTemplate)
                layoutContextMenu.settingsController.duplicateScrollingTemplate(layoutContextMenu.layoutId);
            else
                layoutContextMenu.settingsController.duplicateLayout(layoutContextMenu.layoutId);
        }
    }

    MenuItem {
        text: i18n("Export")
        icon.name: "document-export"
        visible: !layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId, layoutContextMenu.isTemplate)
    }

    MenuSeparator {
        visible: Boolean(layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile)
    }

    MenuItem {
        text: i18n("Delete")
        icon.name: "edit-delete"
        visible: Boolean(layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile)
        onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
    }

    MenuSeparator {
        visible: layoutContextMenu.isAutotile
    }

    MenuItem {
        text: i18n("Duplicate")
        icon.name: "edit-copy"
        visible: layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.settingsController.duplicateAlgorithm(layoutContextMenu.settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId))
    }

    MenuItem {
        text: i18n("Export")
        icon.name: "document-export"
        visible: layoutContextMenu.isAutotile
        onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId, false)
    }

    MenuSeparator {
        visible: Boolean(layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem)
    }

    MenuItem {
        text: i18n("Delete")
        icon.name: "edit-delete"
        visible: Boolean(layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem)
        onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
    }
}
