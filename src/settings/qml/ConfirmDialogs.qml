// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

// The chrome-owned confirmation prompts, split out of Main.qml when that file
// passed the 1150-line ceiling. These are one coherent concern: every one is a
// two-choice "are you sure" over a destructive settings action, declared at the
// window root so the page-nav shortcut guard can see whether any is open and so
// per-page QML can reach them without a Loader breaking the file-id lookup.
//
// Hosted as a non-visual holder. Kirigami.PromptDialog is a Popup, which
// resolves the window overlay for itself, so parking the set inside this Item
// does not change where they render. `settingsController` and `appSettings` are
// app-wide context properties, so they resolve here exactly as in Main.qml.
Item {
    id: root

    // Reached by per-page QML and by Main's kebab/toggle/header handlers.
    property alias defaults: defaultsConfirmDialog
    property alias resetPage: resetPageConfirmDialog
    property alias discardPage: discardPageConfirmDialog
    property alias sectionToggle: sectionToggleDiscardConfirm
    property alias daemonStop: daemonStopConfirm

    // One property for the nav guard to depend on, instead of five .visible
    // reads spelled out at the call site.
    readonly property bool anyOpen: defaultsConfirmDialog.visible || resetPageConfirmDialog.visible || discardPageConfirmDialog.visible || sectionToggleDiscardConfirm.visible || daemonStopConfirm.visible

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

    // Per-page Reset — restores just the active page's settings to their
    // defaults, staged for Save/Discard (opened from the breadcrumb kebab).
    Kirigami.PromptDialog {
        id: resetPageConfirmDialog

        title: i18n("Reset Page to Defaults")
        subtitle: i18n("Reset the settings on this page to their default values? You can still review the result and Save or Discard afterwards.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Reset Page")
                icon.name: "document-revert"
                onTriggered: {
                    resetPageConfirmDialog.close();
                    settingsController.resetPage(settingsController.activePage);
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetPageConfirmDialog.close()
            }
        ]
    }

    // Per-page Discard — reverts just the active page's unsaved edits to the
    // last-saved values, leaving other pages' changes intact.
    Kirigami.PromptDialog {
        id: discardPageConfirmDialog

        // The scope this Discard will actually clear, which in simple mode is
        // wider than the page on screen: a condensed page is the sole visible
        // row for its whole feature area, so its Discard has to reach the
        // area's hidden advanced leaves or the badge it carries can never be
        // cleared from simple mode.
        readonly property string discardScope: settingsController.activeDirtyScope

        title: i18n("Discard Page Changes")
        // Say which it is. Telling the user "changes on other pages are kept"
        // while clearing a whole feature area would be a false promise.
        subtitle: discardScope === settingsController.activePage ? i18n("Discard the unsaved changes on this page? Changes on other pages are kept.") : i18n("Discarding here also drops the unsaved changes on the other pages in this feature area, including any that are hidden right now. Changes elsewhere are kept.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    discardPageConfirmDialog.close();
                    settingsController.discardPage(discardPageConfirmDialog.discardScope);
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: discardPageConfirmDialog.close()
            }
        ]
    }

    // Confirm dialog for the sidebar's inline snapping/tiling toggle when
    // the relevant page has unsaved edits. Disabling the section through
    // beginExternalEdit/endExternalEdit commits the *_Enabled flag plus
    // whatever the page has staged dirty — without this gate the user
    // could silently apply a partial edit by flipping the sidebar toggle.
    Kirigami.PromptDialog {
        id: sectionToggleDiscardConfirm

        // Set by the trailing-delegate SettingsSwitch before open(); the
        // confirm action reads them to know which section to commit and
        // what value to set.
        property string pendingSection: ""
        property bool pendingValue: false

        title: i18n("Discard unsaved changes?")
        subtitle: pendingSection === "snapping" ? i18n("Disabling Snapping will discard your unsaved Snapping changes. Continue?") : i18n("Disabling Tiling will discard your unsaved Tiling changes. Continue?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard and Disable")
                icon.name: "edit-undo"
                onTriggered: {
                    const section = sectionToggleDiscardConfirm.pendingSection;
                    const value = sectionToggleDiscardConfirm.pendingValue;
                    sectionToggleDiscardConfirm.close();
                    // Discard the section's staged edits first, THEN flip the
                    // enable flag — otherwise the inline beginExternalEdit /
                    // endExternalEdit pair would surface the still-staged edits
                    // alongside the disable. discardPage("snapping"/"tiling")
                    // reverts every manifest-backed leaf under that mode back to
                    // the committed baseline (the framework PageAdapter.discard()
                    // for these virtual parents is a no-op, so the old
                    // registry.controller(section).discard() call did nothing).
                    settingsController.discardPage(section);
                    settingsController.beginExternalEdit(section);
                    if (section === "snapping")
                        appSettings.snappingEnabled = value;
                    else
                        appSettings.autotileEnabled = value;
                    settingsController.endExternalEdit();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: sectionToggleDiscardConfirm.close()
            }
        ]
    }

    // Confirm before stopping the daemon from the header toggle. Declared at the
    // window root (not in the headerTrailing Component) so the page-nav shortcut
    // guard `_navShortcutsEnabled` can read its `visible` state; the header
    // SettingsSwitch opens it via outer-scope reference.
    Kirigami.PromptDialog {
        id: daemonStopConfirm

        title: i18n("Stop daemon?")
        subtitle: i18n("Stopping the PlasmaZones daemon disables window tiling and snapping until you start it again.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Stop daemon")
                icon.name: "system-shutdown"
                onTriggered: {
                    settingsController.daemonController.setEnabled(false);
                    daemonStopConfirm.close();
                }
            }
        ]
    }
}
