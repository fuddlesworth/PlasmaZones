// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.control

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
    /** Alias onto the chrome Sidebar item so consumers can configure
     *  the two delegate slots and drive navigation:
     *
     *      window.sidebar.drillInto(parentId)
     *      window.sidebar.drillOut()
     *      window.sidebar.toggleCategory(id)
     *      window.sidebar.currentParentId  // read
     *      window.sidebar.footerContent: Component { ... }
     *      window.sidebar.trailingDelegate: Component { ... }
     *
     *  Underscore-prefixed members on the underlying Sidebar
     *  (`_isExpanded`, `_refreshModel`, `_suppressAccordion`, …) are
     *  private by convention — do not read or assign them from
     *  outside. A typed QtObject façade was attempted in audit pass
     *  45 but breaks the grouped-property assignment syntax
     *  (`sidebar.trailingDelegate: Component { ... }`) that consumers
     *  rely on, so the alias was kept as the contract surface. */
    property alias sidebar: sidebarItem
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
     *  grid units, matching the legacy Phosphor chrome. Consumers
     *  can override this binding (e.g. for tests or to force a
     *  collapsed rail) by reassigning the property. */
    property bool sidebarCompact: width < Kirigami.Units.gridUnit * 50

    /** Emitted when the user picked Apply in the close-confirmation
     *  prompt, applyAll() ran, and the controller is STILL dirty —
     *  meaning at least one staging domain refused or failed its
     *  commit. Consumers wire this to a toast / error banner so the
     *  user understands the close-was-blocked path instead of being
     *  silently re-prompted with the same dialog on the next close
     *  attempt. Carries the unresolved page ids (best-effort: walks
     *  the registry and collects each PageController whose isDirty()
     *  is still true) AND the per-domain error strings collected by
     *  the async batch — consumers can use either: page-id list for
     *  "still unsaved on X, Y" framing, errors list for "%1 said: %2"
     *  framing with the actual D-Bus / file-IO message. */
    signal applyOnCloseFailed(var dirtyPageIds, var errors)
    /** Emitted when an async Discard initiated by the close-prompt
     *  failed (at least one staging domain reported discardResult ok
     *  = false). Consumers can surface the per-domain error strings;
     *  the window still closes because the user explicitly chose to
     *  throw away changes. */
    signal discardOnCloseFailed(var errors)

    /// Walk the registry and return the ids of every page whose
    /// controller is still dirty. Used by applyOnCloseFailed above so
    /// a consumer toast can name the page(s) that refused the commit
    /// instead of just saying "save failed".
    function collectDirtyPageIds() {
        const ids = [];
        if (!root.controller || !root.controller.registry)
            return ids;

        const entries = root.controller.registry.allPagesData();
        for (let i = 0; i < entries.length; ++i) {
            const id = entries[i].id;
            if (!id)
                continue;

            const ctrl = root.controller.registry.controller(id);
            if (ctrl && ctrl.isDirty())
                ids.push(id);
        }
        return ids;
    }

    // Default geometry sized in gridUnits so HiDPI displays (gridUnit ~24-36)
    // scale the window proportionally — staying above the compact-rail
    // threshold (50 gridUnits) at every DPI. At 1× DPR (gridUnit ~18)
    // this is ~1206×792, matching the legacy Phosphor default; at
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
    onClosing: function (close) {
        // Bypass the dirty prompt when the closeFlow is in its
        // "force close" phase — the user has already confirmed
        // Apply/Discard, the async batch landed, and the close was
        // re-issued via Qt.callLater(root.close). Without this gate,
        // a controller that briefly re-dirtied between applyAllComplete
        // and the deferred close (e.g. a daemon broadcast firing
        // mid-close) re-fires the prompt and the user gets stuck in
        // a close-loop.
        //
        // Reset the flag immediately so a *second* close that gets
        // accepted (e.g. close.accepted = false elsewhere in a
        // consumer's onClosing handler) doesn't leave forceClosing
        // permanently true — a stale flag silently bypasses the
        // dirty prompt on the NEXT close-with-dirty-data, losing
        // user edits.
        if (closeFlow.forceClosing) {
            closeFlow.forceClosing = false;
            return;
        }
        // If we're already waiting on an in-flight async batch from a
        // previous close attempt, leave the window open until the
        // batch's applyAllComplete / discardAllComplete handler runs
        // its deferred-close. Reading `dirty` here can race the
        // batch (a fast staging domain commits before the completion
        // signal arrives) and accidentally accept the close while
        // applyAllComplete still expects to drive it.
        if (closeFlow.waitingApply || closeFlow.waitingDiscard) {
            close.accepted = false;
            return;
        }
        if (root.controller.dirty) {
            close.accepted = false;
            // Don't double-open if the dialog is already visible (the
            // user double-clicked the X, or a previous close is still
            // unwinding) — Kirigami.PromptDialog tolerates this but
            // resetting waitingApply / waitingDiscard would silently
            // re-arm the close flow against a stale batch.
            if (!discardDialog.visible)
                discardDialog.open();
        }
    }

    // True while we're waiting for an async apply/discard that was
    // triggered by the close-prompt's Apply / Discard action — the
    // applyAllComplete / discardAllComplete handlers below close the
    // window only when this is set, so a normal footer-driven Save
    // doesn't accidentally close the window. forceClosing flips true
    // during the deferred close so onClosing bypasses its dirty
    // prompt for the second close-event the deferral generates.
    QtObject {
        id: closeFlow

        property bool waitingApply: false
        property bool waitingDiscard: false
        property bool forceClosing: false
    }

    DiscardChangesDialog {
        id: discardDialog

        applyAvailable: root.closePromptShowsApply
        onDiscardConfirmed: {
            // Async path so a heavy discard (motion-set restore that
            // touches dozens of profile files) doesn't freeze the
            // close-prompt window. Wait for discardAllComplete before
            // actually closing.
            closeFlow.waitingDiscard = true;
            root.controller.discardAllAsync();
        }
        onApplyConfirmed: {
            closeFlow.waitingApply = true;
            root.controller.applyAllAsync();
        }
    }

    // Async-path close drivers — fire only when an Apply / Discard
    // confirmation in the close-prompt is in flight (closeFlow flags).
    // The same applyAllComplete / discardAllComplete signals also fire
    // for footer-driven Saves / Discards — those leave the close
    // flags false and these handlers no-op.
    //
    // NOTE: DiscardChangesDialog's Apply / Discard / Keep actions
    // already call `root.close()` on themselves in onTriggered — so
    // by the time these completion handlers fire, the dialog is
    // already in its teardown animation. We do NOT re-close it here;
    // the second `discardDialog.close()` is redundant and (depending
    // on Kirigami.PromptDialog's internal state machine) can fire a
    // duplicate `aboutToHide` cycle that causes the underlying
    // QQuickWindow to lose focus mid-deferred-close.
    Connections {
        function onApplyAllComplete(ok, errors) {
            if (!closeFlow.waitingApply)
                return;

            closeFlow.waitingApply = false;
            if (!ok || root.controller.dirty) {
                // applyAllAsync left the controller dirty (one or
                // more staging domains refused or failed to commit).
                // Surface to the consumer instead of silently re-
                // prompting the discard dialog on the next close
                // attempt. Pass both the page ids AND the per-domain
                // error strings the async batch collected so the
                // consumer toast can render either.
                const dirtyIds = root.collectDirtyPageIds();
                root.applyOnCloseFailed(dirtyIds, errors);
                return;
            }
            closeFlow.forceClosing = true;
            Qt.callLater(root.close);
        }

        function onDiscardAllComplete(ok, errors) {
            if (!closeFlow.waitingDiscard)
                return;

            closeFlow.waitingDiscard = false;
            // A failed discard still progresses the close — the user
            // explicitly said "throw away changes" — but surface the
            // error list so the consumer can toast the user before
            // the window disappears.
            if (!ok && errors && errors.length > 0)
                root.discardOnCloseFailed(errors);
            closeFlow.forceClosing = true;
            Qt.callLater(root.close);
        }

        target: root.controller
    }

    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: root.title

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                // `sidebarItem` (not `sidebar`) — the root has a
                // `readonly property QtObject sidebar` façade above,
                // and JS scope chain would resolve a bare `sidebar`
                // identifier inside the façade's methods to the root
                // property (returning the façade itself) before
                // resolving it as a QML id. Naming the underlying
                // Sidebar item `sidebarItem` removes the collision so
                // the façade's `sidebarItem.drillInto(...)` etc. land
                // on the actual Sidebar.
                id: sidebarItem

                // Single intermediate property drives all three Layout
                // width hints — one Behavior+animation runs per
                // compact/non-compact transition instead of three
                // concurrent ones doing identical work. Pattern
                // mirrors UnsavedChangesFooter.qml's expansion driver.
                //
                // NOTE: not `readonly` — Qt 6.11 hardened the readonly
                // contract and blocks Behavior attachments on
                // readonly-marked properties. The value still flows
                // from the binding below; the Behavior intercepts the
                // transition between binding-driven values.
                property real targetWidth: root.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12

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

                // Header extras (e.g. global search) on their own centered row
                // ABOVE the breadcrumb trail. A Layout child with visible:false
                // is excluded from the column, so consumers that don't provide a
                // headerExtras Component get no empty row — no wrapper needed.
                Loader {
                    id: headerExtrasLoader

                    // Centered horizontally; the Loader adopts the loaded item's
                    // implicit size (slot contract: the consumer Component
                    // declares implicitWidth/Height). visible gates on
                    // Loader.Ready so a mid-instantiation skeleton can't take
                    // layout space.
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    visible: status === Loader.Ready && item !== null
                }

                RowLayout {
                    id: breadcrumbBar

                    Layout.fillWidth: true
                    // Horizontal gutter matches the PageHost content margin + the
                    // UnsavedChangesFooter inset (both largeSpacing) so breadcrumb,
                    // page body, and footer share one left/right edge.
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Breadcrumbs {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        controller: root.controller
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
