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
 *   headerBand (full-width, optional)
 *   sidebar | (breadcrumbs / pageHost / footer)
 *
 * Consumers create one of these per app, set `controller`, and optionally
 * override `title` and `headerExtras`.
 */
Kirigami.ApplicationWindow {
    id: root

    required property ApplicationController controller
    //* Optional extra content shown centered in the header toolbar (e.g. global search).
    property alias headerExtras: headerExtrasLoader.sourceComponent
    //* Optional content pinned to the RIGHT of the header-extras row (e.g. a
    //  status toggle), sharing the row with the centered headerExtras.
    property alias headerTrailing: headerTrailingLoader.sourceComponent
    //* Optional label for the unsaved-changes footer bar, replacing its plain
    //  "Unsaved changes" when the app can say what Save will actually do
    //  (e.g. name the profile a pending switch applies). Empty keeps the
    //  default.
    property string unsavedChangesMessage
    //* Optional content pinned to the RIGHT of the per-page breadcrumb row
    //  (e.g. a page-scoped overflow/kebab menu). Sits on the same line as the
    //  breadcrumbs, right-aligned; the row already reserves the leading space
    //  via Breadcrumbs' Layout.fillWidth. Empty by default.
    property alias breadcrumbTrailing: breadcrumbTrailingLoader.sourceComponent
    /** Alias onto the chrome Sidebar item so consumers can configure
     *  the two delegate slots and drive navigation:
     *
     *      window.sidebar.drillInto(parentId)
     *      window.sidebar.drillOut()
     *      window.sidebar.toggleCategory(id)
     *      window.sidebar.currentParentId  // read
     *      window.sidebar.footerContent: Component { ... }
     *      window.sidebar.trailingDelegate: Component { ... }
     *      window.sidebar.flattenTree            // simple-mode flat rail
     *      window.sidebar.flatTitleOverrides     // per-leaf flat titles
     *
     *  The last two are also read back by this window to keep Breadcrumbs
     *  in step with the rail, so a consumer must set them through this alias
     *  rather than on the Sidebar directly.
     *
     *  Underscore-prefixed members on the underlying Sidebar
     *  (`_isExpanded`, `_refreshModel`, `_suppressAccordion`, …) are
     *  private by convention — do not read or assign them from
     *  outside. A typed QtObject façade was attempted for audit
     *  finding 45 but breaks the grouped-property assignment syntax
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
    /** Gate for the built-in back/forward navigation inputs (the
     *  Alt+Left / Alt+Right key handling on the chrome page and the
     *  mouse back/forward buttons — the breadcrumb-row buttons are
     *  ordinary click targets and stay active). Modal popups block
     *  both inputs on their own (focus moves into the popup and mouse
     *  input outside it is grabbed), but non-modal surfaces — a search
     *  dropdown, a help overlay, a native child window — do not, so
     *  apps bind this to their shortcut-guard expression (the same one
     *  gating any page-step shortcuts) to keep history navigation from
     *  firing under those. Defaults true. */
    property bool navigationShortcutsEnabled: true

    /** Emitted when the user picked Apply in the close-confirmation
     *  prompt, applyAll() ran, and the controller is STILL dirty —
     *  meaning at least one staging domain refused or failed its
     *  commit. Consumers wire this to a toast / error banner so the
     *  user understands the close-was-blocked path instead of being
     *  silently re-prompted with the same dialog on the next close
     *  attempt. Carries the unresolved page ids (best-effort: walks
     *  the registry and collects each PageController whose `dirty`
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
        // Delegates to C++: isDirty() is not Q_INVOKABLE, so walking the
        // registry here and calling it threw a TypeError on the first
        // non-null controller, which killed the applyOnCloseFailed emit and
        // left the window silently refusing to close.
        return root.controller ? root.controller.dirtyPageIds() : [];
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
        // Rest keyboard focus inside the page so key events (and the
        // ShortcutOverride pass below) route through it even before any
        // control has taken focus.
        focus: true

        // ── Back / Forward history keys (Alt+Left / Alt+Right + the
        // dedicated XF86Back/Forward keys) ──────────────────────────────
        // NOT implemented as Shortcut items: Kirigami's PageRow installs
        // its own always-enabled StandardKey.Back/Forward Shortcuts in
        // this window (column/layer navigation, a no-op for this
        // single-column chrome). A second Shortcut on the same sequence
        // makes the pair AMBIGUOUS — Qt then activates neither, it only
        // rotates activatedAmbiguously between them — so a competing
        // Shortcut here would simply never fire. Instead, accept the
        // keys in the ShortcutOverride pass (which preempts the whole
        // shortcut map) and handle them as ordinary key presses bubbling
        // up from the focused item.
        // Claim a key only when the matching history direction has
        // somewhere to go — an unusable Back/Forward key falls through
        // to the shortcut map (Kirigami's no-op) instead of being
        // silently consumed here, mirroring the enabled-gating the
        // replaced Shortcut items had.
        Keys.onShortcutOverride: event => {
            if (!root.navigationShortcutsEnabled)
                return;

            if ((event.matches(StandardKey.Back) && root.controller.canGoBack) || (event.matches(StandardKey.Forward) && root.controller.canGoForward))
                event.accepted = true;
        }
        Keys.onPressed: event => {
            if (!root.navigationShortcutsEnabled)
                return;

            if (event.matches(StandardKey.Back) && root.controller.canGoBack) {
                root.controller.goBack();
                event.accepted = true;
            } else if (event.matches(StandardKey.Forward) && root.controller.canGoForward) {
                root.controller.goForward();
                event.accepted = true;
            }
        }

        // Mouse back/forward buttons drive the page history, matching
        // the Alt+Left / Alt+Right keys. Declared before (so stacked
        // underneath) the content column: the area accepts ONLY the two
        // navigation buttons, and receives them only when no content
        // item claimed the press first — left-button interaction
        // everywhere is untouched. Modal popups block outside mouse
        // input on their own, but the shortcut gate is honoured anyway
        // for consistency with the keyboard path.
        //
        // Navigation happens on PRESS, not click: Kirigami's ColumnView
        // (the PageRow's C++ container, which filters every descendant's
        // mouse events) consumes the ForwardButton RELEASE outright —
        // and the BackButton release too once its column index moves —
        // so a clicked handler never fires for forward. The press is
        // the only edge guaranteed to arrive; it also matches browser
        // behaviour, where side-button navigation triggers on press.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.BackButton | Qt.ForwardButton
            onPressed: function (mouse) {
                if (!root.navigationShortcutsEnabled)
                    return;

                if (mouse.button === Qt.BackButton)
                    root.controller.goBack();
                else if (mouse.button === Qt.ForwardButton)
                    root.controller.goForward();
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Full-width header bar spanning the ENTIRE window, ABOVE both the
            // sidebar and the content panel: the headerExtras slot (e.g. global
            // search) centered across the whole window, with the optional
            // headerTrailing slot (e.g. a status toggle) pinned right. The band
            // collapses when neither slot is filled. A left phantom the width of
            // the trailing keeps the centered slot window-centered (not biased
            // left by the trailing's width).
            //
            // The band declares the Header color set and paints its own
            // backgroundColor so the chrome is truthful: slot content resolves
            // Header roles, and schemes with a distinct Header hue (e.g.
            // wallpaper-generated schemes carry the wallpaper's second hue
            // there) actually show it instead of silently inheriting Window.
            Item {
                id: headerBand

                Kirigami.Theme.colorSet: Kirigami.Theme.Header
                Kirigami.Theme.inherit: false

                Layout.fillWidth: true
                // Collapse to zero height when both slots are filled but
                // their items are currently hidden (e.g. a consumer binds
                // the slot content's `visible` to some mode). Reading the
                // loaded items' `visible` here returns their EFFECTIVE
                // visibility, which is safe in this direction: the band
                // itself stays `visible` (the gate below checks item
                // presence, not visibility), so the children's effective
                // visibility tracks their own bindings and cannot latch —
                // unlike routing the *visible* gate through a child, which
                // would latch the band hidden. See the comment on `visible`
                // below.
                implicitHeight: (headerExtrasLoader.item !== null && headerExtrasLoader.item.visible) || (headerTrailingLoader.item !== null && headerTrailingLoader.item.visible) ? headerExtrasRow.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
                // Gate on the Loaders directly, NOT on headerExtrasRow.visible:
                // reading a child's `visible` returns its EFFECTIVE visibility,
                // so routing the condition through the row would latch the band
                // hidden forever once it first collapsed (same trap as the
                // breadcrumb trailing slot below).
                visible: headerExtrasLoader.item !== null || headerTrailingLoader.item !== null

                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.backgroundColor
                }

                RowLayout {
                    id: headerExtrasRow

                    // Gated width of the trailing slot — the phantom on the left
                    // pads the same amount so the centered slot stays
                    // window-centered. Read via the gated expression rather
                    // than the Loader's width, which lags one layout-polish
                    // cycle behind the binding.
                    readonly property real _trailingWidth: (headerTrailingLoader.item !== null && headerTrailingLoader.item.visible) ? headerTrailingLoader.item.implicitWidth : 0

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: 0

                    Item {
                        Layout.preferredWidth: headerExtrasRow._trailingWidth
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // The Loaders adopt their loaded item's implicit size (slot
                    // contract: the consumer Component declares implicitWidth/Height)
                    // and collapse to zero size when the loaded item hides itself.
                    // Gated via Layout.preferredWidth/Height, like the breadcrumb
                    // trailing slot below (which gates width only) — see the
                    // comment there for why the gate must not route through the
                    // Loader's own `visible`.
                    Loader {
                        id: headerExtrasLoader

                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: (item !== null && item.visible) ? item.implicitWidth : 0
                        Layout.preferredHeight: (item !== null && item.visible) ? item.implicitHeight : 0
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Loader {
                        id: headerTrailingLoader

                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: (item !== null && item.visible) ? item.implicitWidth : 0
                        Layout.preferredHeight: (item !== null && item.visible) ? item.implicitHeight : 0
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                // Also gate on implicitHeight: the band stays visible:true while
                // collapsing to 0 height when both slot items self-hide, and the
                // 1-px separator would otherwise render as a stray line.
                visible: headerBand.visible && headerBand.implicitHeight > 0
            }

            // Sidebar + content panel sit BELOW the full-width header bar.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Sidebar {
                    // `sidebarItem` (not `sidebar`) — the root exposes
                    // this item through the `property alias sidebar`
                    // declaration on root (the typed QtObject
                    // façade attempted for audit finding 45 was reverted
                    // because it broke grouped-property assignment).
                    // The alias name and the id share the root's name
                    // scope, so the id cannot also be `sidebar`: the
                    // alias would then read `sidebar: sidebar`, a
                    // self-referential declaration QML rejects. Naming
                    // the underlying item `sidebarItem` keeps the alias
                    // target unambiguous.
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

                        // Back / Forward history buttons. Always present
                        // (disabled when there's nowhere to go) so the
                        // breadcrumb row doesn't reflow as history
                        // accumulates.
                        QQC2.ToolButton {
                            Layout.alignment: Qt.AlignVCenter
                            icon.name: "go-previous"
                            display: QQC2.AbstractButton.IconOnly
                            text: qsTr("Back")
                            Accessible.name: text
                            enabled: root.controller.canGoBack
                            onClicked: root.controller.goBack()

                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            QQC2.ToolTip.text: text
                        }

                        QQC2.ToolButton {
                            Layout.alignment: Qt.AlignVCenter
                            icon.name: "go-next"
                            display: QQC2.AbstractButton.IconOnly
                            text: qsTr("Forward")
                            Accessible.name: text
                            enabled: root.controller.canGoForward
                            onClicked: root.controller.goForward()

                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            QQC2.ToolTip.text: text
                        }

                        Breadcrumbs {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            controller: root.controller
                            // Follow the rail: when it flattens the tree the
                            // crumb trail collapses to the page title too.
                            flattenTree: sidebarItem.flattenTree
                            flatTitleOverrides: sidebarItem.flatTitleOverrides
                        }

                        // Page-scoped trailing slot (e.g. a per-page overflow
                        // menu). Adopts its loaded item's implicit size when the
                        // item is shown, and collapses to zero width both when
                        // unfilled and when the loaded item hides itself (a
                        // self-gating slot shown only on some pages). The slot is
                        // collapsed via Layout.preferredWidth rather than the
                        // Loader's own `visible`: reading `item.visible` returns
                        // the child's EFFECTIVE visibility, so gating the Loader's
                        // visible on it would force the child invisible (parent
                        // suppression) and latch `item.visible` false forever, so
                        // the slot could never reappear once first hidden.
                        Loader {
                            id: breadcrumbTrailingLoader

                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: (item !== null && item.visible) ? item.implicitWidth : 0
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
                        message: root.unsavedChangesMessage
                    }
                }
            }
        }
    }
}
