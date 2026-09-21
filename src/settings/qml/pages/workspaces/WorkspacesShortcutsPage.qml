// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Present on this page and not on its three siblings, deliberately. It binds
// the Repeater delegate below to its own component scope, which is what lets
// the nested combo read `slotDelegate.*` unambiguously. WorkspacesBehaviorPage
// instantiates no delegate at all, and the two Named Workspaces files hand a
// `rowDelegate` Component across a file boundary into ReorderableColumn, where
// the row content resolves its data through `parent.rowModelData` — a lookup
// the pragma's stricter scoping would break.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Quick Shortcuts leaf.
 *
 * The quick-layout model applied to workspaces: a fixed set of slots, each
 * with a factory chord, and the page assigns WHICH named workspace the slot
 * acts on. The chords themselves are not editable here, exactly like the
 * snapping/tiling quick shortcuts. Names come from the Named Workspaces leaf;
 * a slot with no workspace assigned does nothing.
 *
 * Each slot carries TWO daemon-bound chords: the move chord sends the active
 * window to the assigned workspace, and the focus chord jumps to the slot's
 * position in the acting monitor's list. Both are shown, because a chord the
 * daemon binds but the page never mentions is a chord the user cannot
 * discover.
 */
SettingsFlickable {
    id: root

    // Declared names for the combos (the same shim RulesPage uses).
    readonly property var workspaceNames: {
        var names = [];
        var entries = appSettings.workspacesNamedEntries;
        for (var i = 0; i < entries.length; ++i) {
            var name = ("" + (entries[i].name || "")).trim();
            if (name.length > 0)
                names.push(name);
        }
        return names;
    }
    /// The slot combos' shared option list: "None" plus every declared name.
    /// Built once on the page rather than once per slot — all nine combos show
    /// the same set, and a per-delegate builder re-ran all nine on every
    /// workspace add or rename.
    readonly property var slotOptions: {
        var items = [
            {
                "label": i18n("None"),
                "value": ""
            }
        ];
        for (var i = 0; i < root.workspaceNames.length; ++i)
            items.push({
                "label": root.workspaceNames[i],
                "value": root.workspaceNames[i]
            });
        return items;
    }
    // Bumped on target/shortcut changes so the invokable reads re-evaluate.
    property int _slotTick: 0

    Connections {
        function onWorkspaceSlotTargetsChanged() {
            root._slotTick++;
        }

        function onWorkspaceSlotShortcutsChanged() {
            root._slotTick++;
        }

        target: appSettings
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Workspace quick shortcuts")
            searchAnchor: "workspaceQuickShortcuts"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    // Names BOTH verbs and where the keys live. The page only
                    // assigns which workspace a slot acts on, so a user who
                    // wants a different chord has nowhere to go from here
                    // unless the text says so. Worded without naming the KDE
                    // System Settings module, which the portable
                    // (USE_KDE_FRAMEWORKS=OFF) build does not have.
                    text: i18n("Each slot carries two keys. One jumps to the workspace assigned to that slot and the other sends the active window there. Both are registered under PlasmaZones and can be changed in your desktop's keyboard shortcut settings.")
                }

                Repeater {
                    // The quick-slot count the indexed key builders and the
                    // daemon's slot entries share
                    // (ConfigDefaults::WorkspaceSlotCount).
                    model: settingsController.workspaceSlotCount

                    delegate: ColumnLayout {
                        id: slotDelegate

                        required property int index
                        property int slotNumber: index + 1
                        property string shortcutText: {
                            void root._slotTick;
                            return appSettings.workspaceMoveSlotShortcut(slotDelegate.index);
                        }
                        property string focusShortcutText: {
                            void root._slotTick;
                            return appSettings.workspaceFocusSlotShortcut(slotDelegate.index);
                        }
                        property string targetName: {
                            void root._slotTick;
                            return appSettings.workspaceSlotTarget(slotDelegate.index);
                        }
                        /// The move-chord caption. Named so the slot title can
                        /// announce it (see its Accessible.description).
                        readonly property string moveCaption: shortcutText !== "" ? i18nc("%1 is a keyboard shortcut such as Meta+Shift+1", "%1 moves the active window here", shortcutText) : i18n("No move shortcut assigned")
                        /// The focus-chord caption, empty when unbound. The
                        /// chord is positional rather than name-based, so it
                        /// does not read the assignment beside it; hidden when
                        /// unbound rather than showing a second "none" line.
                        readonly property string focusCaption: focusShortcutText !== "" ? i18nc("%1 is a keyboard shortcut such as Meta+1", "%1 switches to the workspace in this position", focusShortcutText) : ""
                        readonly property real _captionOpacity: 0.6
                        readonly property real _emptyCaptionOpacity: 0.35

                        Layout.fillWidth: true
                        spacing: 0

                        SettingsSeparator {
                            visible: slotDelegate.index > 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            Layout.rightMargin: Kirigami.Units.largeSpacing
                            spacing: Kirigami.Units.largeSpacing

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                                spacing: Kirigami.Units.smallSpacing / 2

                                Label {
                                    text: i18n("Quick Workspace %1", slotDelegate.slotNumber)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    // The two captions below are hidden from
                                    // assistive technology and folded in here
                                    // instead. Announced on their own they are
                                    // free-floating sentences between slots,
                                    // with nothing tying them to the slot they
                                    // describe.
                                    Accessible.description: slotDelegate.focusCaption !== "" ? i18nc("two shortcut descriptions read one after the other", "%1 %2", slotDelegate.moveCaption, slotDelegate.focusCaption) : slotDelegate.moveCaption
                                }

                                Label {
                                    text: slotDelegate.moveCaption
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
                                    opacity: slotDelegate.shortcutText !== "" ? slotDelegate._captionOpacity : slotDelegate._emptyCaptionOpacity
                                    Accessible.ignored: true
                                }

                                // The slot's OTHER daemon-bound chord
                                // (WorkspaceController::focusWorkspaceAt); see
                                // `focusCaption` for why it is positional.
                                Label {
                                    visible: slotDelegate.focusCaption !== ""
                                    text: slotDelegate.focusCaption
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
                                    opacity: slotDelegate._captionOpacity
                                    Accessible.ignored: true
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                                spacing: Kirigami.Units.smallSpacing

                                WideComboBox {
                                    id: slotCombo

                                    Layout.fillWidth: true
                                    Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                                    model: root.slotOptions
                                    textRole: "label"
                                    valueRole: "value"
                                    // storedValue, never a hand-rolled
                                    // currentIndex binding: QQC2 severs
                                    // currentIndex the moment the user picks an
                                    // item, after which the combo stops
                                    // following the slot. The Clear button
                                    // beside it and the Named page's rename
                                    // cascade both write the target from
                                    // elsewhere, so the display has to keep
                                    // following it.
                                    storedValue: slotDelegate.targetName
                                    // An assigned name whose declaration was
                                    // removed stays legible: the slot is
                                    // dormant, not broken. WideComboBox clamps
                                    // an unresolved storedValue to index 0,
                                    // which here is "None", so without this
                                    // fallback a dormant slot would read as
                                    // unassigned.
                                    displayText: (slotDelegate.targetName !== "" && indexOfValue(slotDelegate.targetName) < 0) ? slotDelegate.targetName : currentText
                                    Accessible.name: i18n("Workspace for quick shortcut %1", slotDelegate.slotNumber)
                                    onActivated: function (index) {
                                        var entry = root.slotOptions[index];
                                        var value = entry ? (entry.value || "") : "";
                                        if (value !== slotDelegate.targetName)
                                            appSettings.setWorkspaceSlotTarget(slotDelegate.index, value);
                                    }
                                }

                                ToolButton {
                                    icon.name: "edit-clear"
                                    enabled: slotDelegate.targetName !== ""
                                    onClicked: appSettings.setWorkspaceSlotTarget(slotDelegate.index, "")
                                    ToolTip.visible: hovered
                                    ToolTip.text: i18n("Clear workspace")
                                    Accessible.name: i18n("Clear workspace for quick shortcut %1", slotDelegate.slotNumber)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
