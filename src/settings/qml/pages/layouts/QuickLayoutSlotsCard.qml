// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Quick layout slots card - Assign layouts to keyboard shortcuts
 */
SettingsCard {
    id: root

    // The settings controller. Slot reads/writes and the shortcut caption are
    // controller invokables; `settings` is the shared Settings object the
    // combo bridge below reads the global auto-assign toggle from.
    required property var appSettings
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms),
    // 2 = scrolling (native templates)
    property int viewMode: 0

    // Bridge for LayoutComboBox — exposes only what it accesses, in the shape
    // it expects. Handing it the controller directly looked right (`layouts`
    // lives there) but silently lost the preview's auto-assign badge:
    // autoAssignAllLayouts lives on Settings, so the combo read undefined and
    // the badge never lit. Same pattern as MonitorStatePage's _layoutBridge.
    // The `layouts` binding auto-generates the layoutsChanged signal the
    // combo's Connections target listens for. defaultLayoutId /
    // defaultAutotileAlgorithm are deliberately absent: every combo below
    // pins resolvedDefaultId to "" (a slot has no default layout), so the
    // combo never reads them.
    readonly property QtObject _comboBridge: QtObject {
        readonly property var layouts: root.appSettings.layouts
        readonly property bool autoAssignAllLayouts: root.appSettings.settings ? root.appSettings.settings.autoAssignAllLayouts : false
    }

    // Bumped on EXTERNAL (daemon) quick-layout-slot changes. Both snapping and
    // tiling slots are daemon-backed (mode-keyed registry) and the daemon emits
    // quickLayoutSlotsChanged for any slot change, so a single bump here
    // re-evaluates every slot delegate's currentLayoutId binding. One Connections
    // at root, not one per delegate, so an external change fires a single handler.
    property int slotRevision: 0

    function getSlot(slotNumber) {
        if (viewMode === 2)
            return appSettings.getScrollingQuickLayoutSlot(slotNumber);
        return viewMode === 1 ? appSettings.getTilingQuickLayoutSlot(slotNumber) : appSettings.getQuickLayoutSlot(slotNumber);
    }

    function setSlot(slotNumber, value) {
        // A pick that matches what the slot already holds is not an edit.
        // Staging it anyway dirtied the whole app (footer badge, Discard
        // offer) for re-selecting the row that was already selected, or for
        // clearing a slot that was already empty.
        if (getSlot(slotNumber) === value)
            return;

        if (viewMode === 2)
            appSettings.setScrollingQuickLayoutSlot(slotNumber, value);
        else if (viewMode === 1)
            appSettings.setTilingQuickLayoutSlot(slotNumber, value);
        else
            appSettings.setQuickLayoutSlot(slotNumber, value);
    }

    // Catch-up half of the visibility gate below: one bump when the card comes
    // back picks up whatever changed while it was hidden, and also re-reads the
    // shortcut captions, which have no change signal of their own.
    onVisibleChanged: {
        if (visible)
            slotRevision++;
    }

    Connections {
        target: root.appSettings

        function onQuickLayoutSlotsChanged() {
            // A bump costs nine BLOCKING slot reads plus nine shortcut reads.
            // The page host keeps visited pages alive and merely hides them, so
            // an event every cached page reacts to (a global Discard) paid that
            // once per page ever visited. Hidden cards defer to the catch-up
            // bump above.
            if (!root.visible)
                return;

            root.slotRevision++;
        }
    }

    headerText: root.viewMode === 2 ? i18n("Scrolling Quick Shortcuts") : (root.viewMode === 1 ? i18n("Tiling Quick Shortcuts") : i18n("Snapping Quick Shortcuts"))
    searchAnchor: "quickShortcuts"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: 0

        Repeater {
            // One row per quick-layout slot. Nine is the slot count the whole
            // stack agrees on (ConfigDefaults' QuickLayoutSlotCount, the nine
            // Meta+Alt+digit shortcuts, and the controller's own 1..9 bounds
            // check); a tenth row would have no key to reach it.
            model: 9

            delegate: ColumnLayout {
                id: slotDelegate

                required property int index
                property int slotNumber: index + 1
                // Revision-driven: getQuickLayoutShortcut is an invokable, so
                // this binding has no reactive dependency of its own.
                // slotRevision bumps on the daemon's quickLayoutSlotsChanged,
                // the only slot-related change signal the controller exposes,
                // so the caption re-reads alongside the slot combos. The
                // shortcut itself is config-backed and rebound on another page,
                // and no signal ties the two, so a fresh rebind shows here on
                // the next slot change or the next time the card is shown.
                property string shortcutText: {
                    void root.slotRevision;
                    return root.appSettings.getQuickLayoutShortcut(slotNumber);
                }
                // Secondary-text dimming for the shortcut caption, and the
                // deeper dimming the "no shortcut" state carries.
                readonly property real _captionOpacity: 0.6
                readonly property real _emptyCaptionOpacity: 0.35

                Layout.fillWidth: true
                spacing: 0

                // Separator between items (not before the first)
                SettingsSeparator {
                    visible: slotDelegate.index > 0
                }

                // Slot row — matches SettingsRow layout: title+description left, control right
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    // Left: slot title + shortcut info
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                        spacing: Kirigami.Units.smallSpacing / 2

                        Label {
                            text: root.viewMode === 2 ? i18n("Quick Scrolling %1", slotDelegate.slotNumber) : (root.viewMode === 1 ? i18n("Quick Tiling %1", slotDelegate.slotNumber) : i18n("Quick Snapping %1", slotDelegate.slotNumber))
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            // One key per slot for BOTH modes: pressing it
                            // applies the snapping slot on a snapping monitor
                            // and the tiling slot on a tiling one, so the
                            // caption says which monitors this page's slot
                            // answers for rather than implying a second key.
                            text: slotDelegate.shortcutText !== "" ? i18nc("%1 is a keyboard shortcut such as Meta+Alt+1", "Shortcut %1, used on monitors in this mode", slotDelegate.shortcutText) : i18n("No shortcut assigned")
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            // Dimmed as secondary text, dimmed further when it
                            // has no shortcut to state.
                            opacity: slotDelegate.shortcutText !== "" ? slotDelegate._captionOpacity : slotDelegate._emptyCaptionOpacity
                        }
                    }

                    // Right: layout combo + clear button. Use a CONSTANT preferred
                    // width — binding the width to any enclosing layout's width (the
                    // row above or the delegate) feeds this child's size back into
                    // that layout mid-pass, which is the "recursive rearrange" Qt
                    // Quick Layouts aborts on. The left column is fillWidth and takes
                    // the remaining space.
                    RowLayout {
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                        spacing: Kirigami.Units.smallSpacing

                        LayoutComboBox {
                            id: slotLayoutCombo

                            Layout.fillWidth: true
                            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                            appSettings: root._comboBridge
                            noneText: i18n("None")
                            showPreview: true
                            layoutFilter: root.viewMode === 2 ? 2 : (root.viewMode === 1 ? 1 : 0)
                            resolvedDefaultId: ""
                            // Nine combos would otherwise all announce the
                            // component's generic "Layout selection", leaving
                            // a screen-reader user no way to tell which slot
                            // they are on.
                            Accessible.name: root.viewMode === 2 ? i18n("Scrolling template for quick shortcut %1", slotDelegate.slotNumber) : (root.viewMode === 1 ? i18n("Tiling algorithm for quick shortcut %1", slotDelegate.slotNumber) : i18n("Zone layout for quick shortcut %1", slotDelegate.slotNumber))
                            currentLayoutId: {
                                void (root.slotRevision);
                                return root.getSlot(slotDelegate.slotNumber);
                            }
                            onActivated: {
                                // Guard against a transient index/model mismatch.
                                // MonitorStatePage's selectors read the activated
                                // index instead; here the ComboBox has already
                                // moved currentIndex by the time this fires, so
                                // the two read the same row by different means.
                                var entry = model[currentIndex];
                                root.setSlot(slotDelegate.slotNumber, entry ? (entry.value || "") : "");
                            }
                        }

                        ToolButton {
                            icon.name: "edit-clear"
                            // Nothing to clear on an empty slot, and pressing
                            // it there staged a no-op that dirtied the app.
                            enabled: slotLayoutCombo.currentLayoutId !== ""
                            onClicked: {
                                root.setSlot(slotDelegate.slotNumber, "");
                                slotLayoutCombo.clearSelection();
                            }
                            ToolTip.visible: hovered
                            // The button clears the slot's LAYOUT. The shortcut
                            // itself is not editable here.
                            ToolTip.text: root.viewMode === 2 ? i18n("Clear template") : i18n("Clear layout")
                            Accessible.name: root.viewMode === 2 ? i18n("Clear template for quick shortcut %1", slotDelegate.slotNumber) : i18n("Clear layout for quick shortcut %1", slotDelegate.slotNumber)
                        }
                    }
                }
            }
        }
    }
}
