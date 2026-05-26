// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Top-level settings application window.
 *
 * Wires the ApplicationController to the standard chrome layout:
 *   sidebar | (breadcrumbs / pageHost / footer)
 *
 * Consumers create one of these per app, set `controller`, and optionally
 * override `title` and `headerExtras`.
 */
Kirigami.ApplicationWindow {
    id: root

    required property ApplicationController controller
    //* Optional extra content shown in the header toolbar (e.g. global search).
    property alias headerExtras: headerExtrasLoader.sourceComponent
    /** Public alias on the chrome Sidebar — consumers use this to restore
     *  drill state on startup (`sidebar.drillInto(parentId)`), toggle
     *  collapsible categories programmatically, or react to navigation. */
    property alias sidebar: sidebar
    /** When true, the close-confirmation prompt offers an Apply action
     *  (Apply / Discard / Cancel) in addition to Discard / Keep Editing.
     *  Defaults false; flip on for apps whose preferred answer to "you
     *  have unsaved changes" is to commit them. */
    property bool closePromptShowsApply: false
    /** Alias to the close-confirmation dialog itself, for consumers
     *  that need to retitle / restyle it. Read-only — the property
     *  the alias points to is `id: discardDialog` declared below. */
    property alias closeDialog: discardDialog

    width: 1100
    height: 720
    title: qsTr("Settings")
    onClosing: function(close) {
        if (root.controller.dirty) {
            close.accepted = false;
            discardDialog.open();
        }
    }

    DiscardChangesDialog {
        id: discardDialog

        applyAvailable: root.closePromptShowsApply
        onDiscardConfirmed: {
            root.controller.discardAll();
            Qt.callLater(root.close);
        }
        onApplyConfirmed: {
            root.controller.applyAll();
            Qt.callLater(root.close);
        }
    }

    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: root.title

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                id: sidebar

                Layout.fillHeight: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                // Layout.maximumWidth caps the Sidebar so an inflated
                // implicitWidth from any inner child (the daemon Pane
                // in the footer slot, a long row title, the ListView
                // delegates) can't push the sidebar past its preferred
                // size. Without this, ColumnLayout's max-child-implicit
                // sizing fights the parent RowLayout's preferredWidth
                // and the sidebar grows to fill the window.
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                controller: root.controller
            }

            Kirigami.Separator {
                Layout.fillHeight: true
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Breadcrumbs {
                        Layout.fillWidth: true
                        controller: root.controller
                    }

                    Loader {
                        id: headerExtrasLoader
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                PageHost {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: root.controller
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                ApplyResetCancelFooter {
                    Layout.fillWidth: true
                    controller: root.controller
                }

            }

        }

    }

}
