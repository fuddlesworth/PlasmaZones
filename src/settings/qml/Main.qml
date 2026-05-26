// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui as PhosphorUi

// Top-level settings window. Chrome (sidebar, breadcrumbs, apply/discard
// footer, "select a page" placeholder) comes from
// PhosphorUi.SettingsAppWindow — driven by the PageRegistry that
// SettingsController.app exposes. This file owns the PlasmaZones-specific
// glue that surrounds the chrome: the layout-context popup menu QML pages
// reach via `window.layoutContextMenu`, the three-way unsaved-changes
// dialog (Apply / Discard / Cancel — richer than the lib's plain
// Discard/Keep prompt), the toast, the shortcut overlay, and the
// What's-New banner that pops on first launch after an update.
PhosphorUi.SettingsAppWindow {
    id: window

    // ── Public API used by per-page QML files ───────────────────────
    // Pages reach the layout-context popup via window.layoutContextMenu.
    readonly property alias layoutContextMenu: layoutContextMenu
    // Shared aspect-ratio labels (also referenced by other pages).
    readonly property var aspectRatioLabels: ({
        "any": i18n("All Monitors"),
        "standard": i18n("Standard (16:9)"),
        "ultrawide": i18n("Ultrawide (21:9)"),
        "super-ultrawide": i18n("Super-Ultrawide (32:9)"),
        "portrait": i18n("Portrait (9:16)")
    })
    // Keyboard-shortcut overlay state.
    property bool _showShortcuts: false
    // Set true when one of the unsaved-changes dialog actions decides
    // the close should go through unconditionally on the next attempt.
    property bool _closeConfirmed: false

    function showWhatsNew() {
        whatsNewDialog.open();
    }

    function showToast(msg) {
        toast.show(msg);
    }

    function showLayoutContextMenu(layout) {
        layoutContextMenu.showForLayout(layout);
    }

    controller: settingsController.app
    title: i18n("PlasmaZones Settings")
    width: 1200
    height: 800
    // Override the lib's onClosing — PlasmaZones wants the richer
    // three-way unsaved-changes prompt (Apply / Discard / Cancel)
    // instead of the lib's plain Discard / Keep dialog.
    onClosing: function(close) {
        settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
        if (settingsController.needsSave && !window._closeConfirmed) {
            close.accepted = false;
            unsavedChangesDialog.open();
        }
    }
    Component.onCompleted: {
        var geo = settingsController.loadWindowGeometry();
        if (geo.width > 0 && geo.height > 0) {
            window.width = geo.width;
            window.height = geo.height;
        }
        if (geo.hasPosition) {
            window.x = geo.x;
            window.y = geo.y;
        }
    }

    // ── Help-overlay shortcut ───────────────────────────────────────
    Shortcut {
        sequence: "?"
        enabled: {
            // Don't toggle the overlay while the user is typing in a
            // text field — `?` is a legitimate character there.
            var item = window.activeFocusItem;
            if (!item)
                return true;

            if (item instanceof TextInput || item instanceof TextEdit)
                return false;

            var role = item.Accessible.role;
            if (role === Accessible.EditableText || role === Accessible.PasswordText)
                return false;

            return true;
        }
        onActivated: window._showShortcuts = !window._showShortcuts
    }

    // ── Toast ───────────────────────────────────────────────────────
    Rectangle {
        id: toast

        property string message: ""

        function show(msg) {
            message = msg;
            toastShow.restart();
            toastHide.restart();
        }

        parent: window.contentItem
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Kirigami.Units.largeSpacing * 4
        width: toastLabel.implicitWidth + Kirigami.Units.largeSpacing * 3
        height: toastLabel.implicitHeight + Kirigami.Units.largeSpacing * 1.5
        radius: height / 2
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)
        opacity: 0
        visible: opacity > 0
        z: 100

        Label {
            id: toastLabel

            anchors.centerIn: parent
            text: toast.message
            color: Kirigami.Theme.backgroundColor
            font.weight: Font.Medium
        }

        PhosphorMotionAnimation {
            id: toastShow

            target: toast
            properties: "opacity"
            from: 0
            to: 1
            profile: "popup"
            durationOverride: 200
        }

        SequentialAnimation {
            id: toastHide

            PauseAnimation {
                duration: 2000
            }

            PhosphorMotionAnimation {
                target: toast
                properties: "opacity"
                from: 1
                to: 0
                profile: "widget.fadeOut"
            }

        }

    }

    // ── Layout context menu (lives outside any Loader to avoid Qt6 SIGSEGV on Menu destruction) ──
    Menu {
        id: layoutContextMenu

        property var layout: null
        property int viewMode: 0
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
        readonly property var _aspectRatioOptions: [["any", window.aspectRatioLabels["any"], 0], ["standard", window.aspectRatioLabels["standard"], 1], ["ultrawide", window.aspectRatioLabels["ultrawide"], 2], ["super-ultrawide", window.aspectRatioLabels["super-ultrawide"], 3], ["portrait", window.aspectRatioLabels["portrait"], 4]]
        readonly property var _screenItemsModel: {
            var screens = settingsController.screens || [];
            return screens.length > 1 ? screens : [];
        }

        signal deleteRequested(var layout)
        signal exportRequested(string layoutId)

        function showForLayout(layout) {
            layoutContextMenu.layout = layout;
            layoutContextMenu.viewMode = (layout && layout.isAutotile === true) ? 1 : 0;
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
            text: i18n("Edit")
            icon.name: "document-edit"
            onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
        }

        Instantiator {
            id: screenItemInstantiator

            model: layoutContextMenu._screenItemsModel
            onObjectAdded: function(index, object) {
                layoutContextMenu.insertItem(1 + index, object);
            }
            onObjectRemoved: function(index, object) {
                layoutContextMenu.removeItem(object);
            }

            delegate: ItemDelegate {
                required property var modelData
                readonly property string _screenName: (modelData && modelData.name) ? modelData.name : ""

                text: i18n("Edit on %1", (modelData && modelData.displayLabel) || (modelData && modelData.name) || "")
                icon.name: (modelData && modelData.isPrimary) ? "starred-symbolic" : "monitor"
                Accessible.name: text
                onClicked: {
                    var screenName = _screenName;
                    var layoutId = layoutContextMenu.layoutId;
                    Qt.callLater(function() {
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

        MenuSeparator {
        }

        MenuItem {
            text: i18n("Set as Default")
            icon.name: "favorite"
            enabled: {
                if (!layoutContextMenu.layout)
                    return false;

                if (layoutContextMenu.viewMode === 1)
                    return layoutContextMenu.layoutId !== ("autotile:" + appSettings.defaultAutotileAlgorithm);

                return layoutContextMenu.layoutId !== appSettings.defaultLayoutId;
            }
            onTriggered: {
                if (layoutContextMenu.viewMode === 1)
                    appSettings.defaultAutotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                else
                    appSettings.defaultLayoutId = layoutContextMenu.layoutId;
            }
        }

        MenuItem {
            text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
            icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
            onTriggered: settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
        }

        MenuItem {
            readonly property bool perLayoutAuto: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true
            readonly property bool globalAuto: appSettings.autoAssignAllLayouts === true

            text: globalAuto ? i18n("Auto-assign forced on (global setting)") : (perLayoutAuto ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign"))
            icon.name: (perLayoutAuto || globalAuto) ? "window-duplicate" : "window-new"
            visible: !layoutContextMenu.isAutotile
            enabled: !globalAuto
            onTriggered: settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !perLayoutAuto)
        }

        MenuSeparator {
            id: aspectRatioMarker

            visible: !layoutContextMenu.isAutotile
        }

        MenuSeparator {
            visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
        }

        MenuItem {
            text: i18n("Duplicate")
            icon.name: "edit-copy"
            visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
            onTriggered: settingsController.duplicateLayout(layoutContextMenu.layoutId)
        }

        MenuItem {
            text: i18n("Export")
            icon.name: "document-export"
            visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
            onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
        }

        MenuSeparator {
            visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
        }

        MenuItem {
            text: i18n("Delete")
            icon.name: "edit-delete"
            visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
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

    // Aspect-ratio submenu (added/removed imperatively by showForLayout).
    // Empty `enter` / `exit` transitions are the `finalizeExitTransition`
    // hardening pattern (mirrors the editor's metadata-preset menu):
    // synchronous close avoids the QQmlData::destroyed race Qt 6's
    // animated Menu teardown can otherwise hit.
    Menu {
        id: aspectRatioSubMenu

        title: i18n("Aspect Ratio")
        icon.name: "view-fullscreen"

        Instantiator {
            id: aspectRatioItemInstantiator

            model: layoutContextMenu._aspectRatioOptions
            onObjectAdded: function(index, object) {
                aspectRatioSubMenu.insertItem(index, object);
            }
            onObjectRemoved: function(index, object) {
                aspectRatioSubMenu.removeItem(object);
            }

            delegate: ItemDelegate {
                required property var modelData
                readonly property string _arKey: (modelData && modelData[0]) ? modelData[0] : ""
                readonly property int _arIndex: (modelData && modelData[2] !== undefined) ? modelData[2] : 0
                readonly property bool isSelected: {
                    var current = (layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass) || "any";
                    return _arKey === current;
                }

                text: (modelData && modelData[1]) ? modelData[1] : ""
                icon.name: isSelected ? "checkmark" : ""
                Accessible.name: text
                onClicked: {
                    var layoutId = layoutContextMenu.layoutId;
                    var idx = _arIndex;
                    Qt.callLater(function() {
                        aspectRatioSubMenu.visible = false;
                        layoutContextMenu.visible = false;
                        settingsController.setLayoutAspectRatio(layoutId, idx);
                    });
                }
            }

        }

        enter: Transition {
        }

        exit: Transition {
        }

    }

    // ── Unsaved-changes confirmation (3-way) ─────────────────────────
    Kirigami.PromptDialog {
        id: unsavedChangesDialog

        title: i18n("Unsaved Changes")
        subtitle: i18n("You have unsaved changes. Do you want to apply them before closing?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Apply")
                icon.name: "dialog-ok-apply"
                onTriggered: {
                    unsavedChangesDialog.close();
                    settingsController.save();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-delete"
                onTriggered: {
                    unsavedChangesDialog.close();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: unsavedChangesDialog.close()
            }
        ]
    }

    // ── Reset / Restore-defaults dialogs (used by Tools menu in pages) ──
    Kirigami.PromptDialog {
        id: resetConfirmDialog

        title: i18n("Discard Changes")
        subtitle: i18n("Are you sure you want to discard all unsaved changes?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    resetConfirmDialog.close();
                    settingsController.load();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetConfirmDialog.close()
            }
        ]
    }

    Kirigami.PromptDialog {
        id: defaultsConfirmDialog

        title: i18n("Restore Defaults")
        subtitle: i18n("Are you sure you want to reset all settings to their default values?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Restore Defaults")
                icon.name: "document-revert"
                onTriggered: {
                    defaultsConfirmDialog.close();
                    settingsController.defaults();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: defaultsConfirmDialog.close()
            }
        ]
    }

    // ── Keyboard-shortcut overlay ───────────────────────────────────
    Rectangle {
        id: shortcutOverlay

        parent: window.contentItem
        anchors.fill: parent
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
        visible: opacity > 0
        opacity: window._showShortcuts ? 1 : 0
        z: 200
        Keys.onEscapePressed: window._showShortcuts = false
        focus: window._showShortcuts

        MouseArea {
            anchors.fill: parent
            onClicked: window._showShortcuts = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.6, Kirigami.Units.gridUnit * 30)
            height: shortcutContent.implicitHeight + Kirigami.Units.largeSpacing * 3
            radius: Kirigami.Units.smallSpacing * 2
            color: Kirigami.Theme.backgroundColor
            border.width: Math.round(Kirigami.Units.devicePixelRatio)
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

            ColumnLayout {
                id: shortcutContent

                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing * 1.5
                spacing: Kirigami.Units.largeSpacing

                Label {
                    text: i18n("Keyboard Shortcuts")
                    font.weight: Font.DemiBold
                    font.pixelSize: Kirigami.Units.gridUnit * 1.2
                    Layout.alignment: Qt.AlignHCenter
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Repeater {
                    model: [{
                        "key": "Meta+Shift+P",
                        "action": i18n("Open PlasmaZones Settings")
                    }, {
                        "key": "Meta+Shift+E",
                        "action": i18n("Open Zone Editor")
                    }, {
                        "key": "?",
                        "action": i18n("Toggle this overlay")
                    }]

                    delegate: RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: modelData.action
                            Layout.fillWidth: true
                            opacity: 0.7
                        }

                        Rectangle {
                            implicitWidth: keyLabel.implicitWidth + Kirigami.Units.largeSpacing
                            implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                            border.width: Math.round(Kirigami.Units.devicePixelRatio)
                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                            Label {
                                id: keyLabel

                                anchors.centerIn: parent
                                text: modelData.key
                                font: Kirigami.Theme.smallFont
                            }

                        }

                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Press ? or Escape to close")
                    opacity: 0.4
                    Layout.alignment: Qt.AlignHCenter
                    font: Kirigami.Theme.smallFont
                }

            }

        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                profile: window._showShortcuts ? "widget.fadeIn" : "widget.fadeOut"
                durationOverride: 200
            }

        }

    }

    // ── What's New dialog ──────────────────────────────────────────
    WhatsNewPage {
        id: whatsNewDialog
    }

    Timer {
        interval: 500
        running: settingsController.hasUnseenWhatsNew
        onTriggered: whatsNewDialog.open()
    }

}
