// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
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
    /** Auto-collapses the sidebar to an icon-only rail when the window
     *  is too narrow to comfortably show labels. Threshold is 50
     *  grid units, matching the legacy PlasmaZones chrome. Consumers
     *  can override this binding (e.g. for tests or to force a
     *  collapsed rail) by reassigning the property. */
    property bool sidebarCompact: width < Kirigami.Units.gridUnit * 50

    // Default geometry sized in gridUnits so HiDPI displays (gridUnit ~24-36)
    // scale the window proportionally — staying above the compact-rail
    // threshold (50 gridUnits) at every DPI. At 1× DPR (gridUnit ~18)
    // this is ~1206×792, matching the legacy PlasmaZones default; at
    // HiDPI it grows to keep the same visual weight. Consumers can
    // override.
    width: Kirigami.Units.gridUnit * 67
    height: Kirigami.Units.gridUnit * 44
    // Floor sizes so the chrome stays usable: the compact rail keeps
    // 3 gridUnits, breadcrumb row needs room for the page label, and
    // a too-short window crushes the unsaved-changes footer.
    minimumWidth: Kirigami.Units.gridUnit * 25
    minimumHeight: Kirigami.Units.gridUnit * 20
    title: qsTr("Settings")
    // The chrome supplies its own breadcrumbs row inside the page
    // body — Kirigami's default ApplicationWindow global toolbar
    // would stack a redundant "<page title>" header bar above that,
    // which is what the legacy chrome avoided by mounting its
    // content directly under the window. None tells Kirigami's
    // pageStack to render nothing there.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
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

                // Single intermediate property drives all three Layout
                // width hints — one Behavior+animation runs per
                // compact/non-compact transition instead of three
                // concurrent ones doing identical work. Pattern
                // mirrors UnsavedChangesFooter.qml's expansion driver.
                readonly property real targetWidth: root.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12

                Layout.fillHeight: true
                // Sidebar width tracks sidebarCompact: 12 gridUnits at
                // normal width (matches legacy), collapses to 3
                // gridUnits in compact mode (icon-only rail). All
                // three Layout width hints stay in lockstep via
                // targetWidth so the animation lands at a clean width
                // and inner-child implicitWidth can't push the rail
                // wider during the transition.
                Layout.preferredWidth: targetWidth
                Layout.minimumWidth: targetWidth
                Layout.maximumWidth: targetWidth
                controller: root.controller
                compact: root.sidebarCompact

                // Slide animation when the rail collapses / expands.
                // Direction is read from `root.sidebarCompact` (the
                // same flag that drove the width assignment above) so
                // the easing leg is decided synchronously — reading
                // `targetWidth` inside the Behavior would re-evaluate
                // during the animation and converge to the wrong leg
                // as the value approached its target.
                Behavior on targetWidth {
                    PhosphorMotionAnimation {
                        profile: !root.sidebarCompact ? "panel.slideIn" : "panel.slideOut"
                    }

                }

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

                        // Sizes from the loaded item's implicitWidth/Height so
                        // the slot contract is "consumer Component must declare
                        // implicitWidth/Height" rather than the silent "renders
                        // as 0×0 if missing" pre-fix behaviour. Layout.alignment
                        // matches the Breadcrumbs centering above. visible gates
                        // on Loader.Ready so a mid-instantiation skeleton can't
                        // take layout space.
                        Layout.preferredWidth: item ? item.implicitWidth : 0
                        Layout.preferredHeight: item ? item.implicitHeight : 0
                        Layout.alignment: Qt.AlignVCenter
                        visible: status === Loader.Ready && item
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

                UnsavedChangesFooter {
                    Layout.fillWidth: true
                    controller: root.controller
                }

            }

        }

    }

}
