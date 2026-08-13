// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The card delegate lives in a Component now (two containers instantiate it,
// the grid and the none row), and it reads the root id for its theme, metrics
// and selection state. Binding the component context is what makes reaching
// an outer id from a nested component well-defined rather than a historical
// accident, and it pairs with the delegate's `required property modelData`,
// which is the injection style Bound expects.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout picker content — Item-rooted body hosted in PassiveOverlayShell's
 * layoutPickerSlot. The slot's Loader re-instantiates this component on
 * every show (the `loaded` toggle), and the slot forwards all data
 * properties written by C++ (snapassist.cpp showLayoutPicker) onto the
 * instance via bindings.
 *
 * Phase 5: surface lifecycle + show/hide animations are driven entirely
 * by PhosphorAnimationLayer::SurfaceAnimator (registered for
 * PhosphorRoles::LayoutPicker with `osd.show` / `osd.pop` / `osd.hide`
 * profiles). PhosphorLayer::Surface handles `Qt.WindowTransparentForInput`
 * on the underlying QWindow during the hide cycle, and OverlayService::
 * showLayoutPicker / hideLayoutPicker drive the picker slot's animated
 * show/hide (hideLayoutPicker → ShellHost::hideSlot); the shell
 * wl_surface's map state is managed separately by
 * syncPassiveShellSurfaceState.
 *
 * This Item only owns:
 *   - Data properties written by C++ (layouts, activeLayoutId, locked, …)
 *   - Keyboard navigation state (selectedIndex; moveSelection /
 *     confirmSelection are invoked from C++, see below)
 *   - The visible content tree (backdrop + popup frame + grid of cards)
 *   - The `_dismissed` latch + `dismissRequested` signal, forwarded by
 *     the shell host as `layoutPickerDismissRequested` and routed by C++
 *     (wirePassiveShellSlots, shellhost_bridge.cpp) to OverlayService::
 *     onLayoutPickerDismissRequested → hideLayoutPicker →
 *     ShellHost::hideSlot
 */
Item {
    // Keyboard handling. The shell surface is kbd-None, so QML Shortcuts
    // can never fire here — arrow / Return navigation arrives from C++
    // instead: snapassist.cpp's pickerMoveSelection / pickerConfirmSelection
    // call moveSelection() / confirmSelection() on the host slot via
    // QMetaObject::invokeMethod, driven by KGlobalAccel registrations.
    // Escape is dismissed via the daemon's KGlobalAccel cancel-overlay
    // shortcut — KWin's wlr-layer-shell does not deliver keyboard events
    // to this layer surface in our Qt/KWin combination, so a QML
    // `Shortcut { sequence: "Escape" }` would never fire (verified via
    // Keys.onPressed diagnostic). See start.cpp's layoutPickerRequested
    // handler, which calls WindowDragAdaptor::ensureCancelOverlayShortcut
    // -Registered() so the picker piggybacks on cancelSnap()'s existing
    // Escape grab. cancelSnap() dismisses the picker first when visible.

    id: root

    // Layout data (array of layout objects with id, name, zones, category, autoAssign)
    property var layouts: []
    property string activeLayoutId: ""
    // The reserved scrolling-template id whose card stands for "no template".
    // Spelled here rather than passed from C++ because this surface already
    // spells the autotile prefix the same way; the authoritative declaration
    // is PhosphorZones::NoScrollingTemplate in AssignmentEntry.h.
    readonly property string noTemplateId: "none"
    // Mirrors the global "Auto-assign for all layouts" master toggle (#370).
    // Forwarded into LayoutCard so the category badge shows effective state.
    property bool globalAutoAssign: false
    // Screen info for aspect ratio
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4, screenAspectRatio))
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
    // Zone appearance — effective values arrive via the host slot's
    // bindings: snapassist.cpp's writeColorSettings pushes onto
    // layoutPickerSlot, which forwards them here. No picker-direct push.
    property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    /// Idempotency latch for `dismissRequested`. Multiple rapid backdrop
    /// clicks during the hide-animation fade-out window (the shell stays
    /// mapped and briefly input-accepting while shaders or animations
    /// are enabled — effects-gated keepMappedOnHide) can fire
    /// `dismissRequested` more than once before `Qt.WindowTransparentForInput`
    /// lands at the QWindow level. Without this, C++ re-runs the dismiss
    /// path (hideLayoutPicker) for an already-dismissed picker per
    /// spurious click. No writer resets it: the host slot's Loader
    /// re-instantiates this component on every show, so the latch starts
    /// false each cycle.
    ///
    /// Sibling latch — OsdDismissable.qml: same at-most-once-per-show
    /// idempotency, but driven by a Timer (auto-dismiss) and reset on
    /// the timer's `runningChanged` transition. The trigger surface and
    /// reset mechanism differ enough that the two are deliberately
    /// separate components; see OsdDismissable.qml for the rationale.
    property bool _dismissed: false
    // Current keyboard selection index — binding is intentionally broken on first
    // keyboard/mouse interaction; the picker is recreated each time so this is safe.
    property int selectedIndex: {
        for (var i = 0; i < layouts.length; i++) {
            if (layouts[i].id === activeLayoutId)
                return i;
        }
        return 0;
    }
    // Grid dimensions
    readonly property int layoutCount: layouts.length
    // The no-template row is drawn on its own centered row UNDER the grid
    // rather than flowing into it, so it reads as "or none of these" instead
    // of as one more choice. It is always last (the list builder sorts it
    // there), so the split is a trailing slice rather than a filter.
    readonly property bool hasNoneRow: layoutCount > 0 && layouts[layoutCount - 1].id === root.noTemplateId
    readonly property int gridCount: layoutCount - (hasNoneRow ? 1 : 0)
    readonly property var gridLayouts: hasNoneRow ? layouts.slice(0, layoutCount - 1) : layouts
    // Sized off the GRID's own count, not the full list: counting the
    // separate card here would widen the grid for a card that is not in it.
    readonly property int gridColumns: Math.min(gridCount, Math.max(3, Math.min(5, Math.ceil(Math.sqrt(gridCount * 1.5)))))
    readonly property int gridRows: gridColumns > 0 ? Math.ceil(gridCount / gridColumns) : 0
    // Card dimensions
    readonly property int previewWidth: metrics.previewWidth
    readonly property int previewHeight: Math.round(previewWidth / safeAspectRatio)
    readonly property int cardWidth: previewWidth + metrics.paddingSide * 2
    readonly property int cardHeight: previewHeight + metrics.containerPadding + metrics.paddingSide
    readonly property int cardSpacing: metrics.indicatorSpacing

    // Internal signals — host Window re-emits to its public signals.
    signal layoutSelected(string layoutId)
    /// User-initiated dismiss request (backdrop click, Escape, in-flight
    /// race). The shell host re-emits it as `layoutPickerDismissRequested`,
    /// which wirePassiveShellSlots (shellhost_bridge.cpp) connects to
    /// OverlayService::onLayoutPickerDismissRequested → hideLayoutPicker
    /// → ShellHost::hideSlot (animator-driven slot-hide). Same shape as
    /// LayoutOsd / NavigationOsd for consistency.
    signal dismissRequested

    /// Internal: emit dismissRequested at most once per show cycle.
    function _requestDismiss() {
        if (_dismissed)
            return;

        _dismissed = true;
        root.dismissRequested();
    }

    function moveSelection(dx, dy) {
        if (layoutCount === 0 || root.locked)
            return;

        // With nothing in the grid the None card is the only thing to select,
        // so every press is a no-op. Also the guard that keeps the modulos
        // below away from a zero column count.
        if (root.gridCount === 0)
            return;

        // The None card occupies one extra row of its own. Its index is the
        // last in the model (the list builder sorts it there), and it answers
        // to any column, which is what makes a Down press from anywhere in
        // the bottom row land on it.
        const noneIndex = root.hasNoneRow ? root.layoutCount - 1 : -1;
        const totalRows = root.gridRows + (root.hasNoneRow ? 1 : 0);
        const onNoneRow = selectedIndex === noneIndex;
        // A row of one has no horizontal neighbours, so sideways presses stay
        // put rather than wrapping onto a grid row the user did not aim at.
        if (onNoneRow && dy === 0)
            return;

        // Leaving the None card vertically re-enters the grid at its middle
        // column: the card is centered, so the middle is what sits above and
        // below it. Nothing remembers which column the user came from, and
        // guessing the edge would move focus sideways on a vertical press.
        let col = onNoneRow ? Math.floor(root.gridColumns / 2) : selectedIndex % root.gridColumns;
        let row = onNoneRow ? root.gridRows : Math.floor(selectedIndex / root.gridColumns);
        col = (col + dx + root.gridColumns) % root.gridColumns;
        row = (row + dy + totalRows) % totalRows;
        if (row === root.gridRows) {
            // The extra row: only the None card lives there.
            selectedIndex = noneIndex;
            return;
        }
        let newIndex = row * root.gridColumns + col;
        if (newIndex >= root.gridCount) {
            // Clamp to last valid item in the target row
            const lastColInRow = Math.min(root.gridColumns, root.gridCount - row * root.gridColumns) - 1;
            newIndex = row * root.gridColumns + Math.min(col, lastColInRow);
        }
        selectedIndex = Math.max(0, Math.min(root.gridCount - 1, newIndex));
    }

    /// A layout's position in the FULL model, by id. The cards are drawn from
    /// two containers now (the grid, and the None card's own row), so a
    /// delegate's position within its Repeater is no longer its index in the
    /// model the selection is keyed on. Deriving it from the id keeps one
    /// delegate serving both, and the list is a handful of entries, so the
    /// scan costs nothing. Returns -1 for an id the model does not carry.
    function indexOfLayoutId(id) {
        for (var i = 0; i < layouts.length; i++) {
            if (layouts[i].id === id)
                return i;
        }
        return -1;
    }

    function confirmSelection() {
        if (root.locked)
            return;

        if (selectedIndex >= 0 && selectedIndex < layoutCount) {
            var layout = layouts[selectedIndex];
            root.layoutSelected(layout.id);
        }
    }

    // Layout constants — match ZoneSelectorLayout (zoneselectorlayout.h)
    QtObject {
        id: metrics

        // Container chrome
        readonly property int containerPadding: Kirigami.Units.gridUnit * 2
        readonly property int paddingSide: Kirigami.Units.gridUnit
        readonly property int indicatorSpacing: Kirigami.Units.gridUnit
        // Card preview
        readonly property int previewWidth: Kirigami.Units.gridUnit * 10
    }

    // Backdrop — click outside to dismiss. _requestDismiss collapses
    // multiple rapid clicks during the fade-out window into a single
    // dismissRequested per show cycle.
    MouseArea {
        anchors.fill: parent
        onClicked: root._requestDismiss()
        Accessible.name: i18n("Dismiss layout picker")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: root._requestDismiss()
    }

    // Main container card
    QFZCommon.PopupFrame {
        id: container

        // The SurfaceAnimator shader anchor lives inside PopupFrame (on
        // its captureItem), scoped to the card plus a capture margin, so
        // any decoration halo and the show / hide transition are captured
        // instead of being clipped. See PopupFrame.qml.
        anchors.centerIn: parent
        // The frame follows the GRID's width even when the none row is
        // present: that row holds one card, which is narrower than any grid
        // row, so letting it participate would only ever shrink the frame
        // under the grid it has to contain.
        width: gridView.width + metrics.containerPadding
        // top padding + title + gap below title + grid + the none row when
        // it exists (its own top margin is folded into noneRow.height by the
        // anchor, so a screen without one adds nothing) + bottom padding
        height: titleLabel.height + gridView.height + noneRow.height + (root.hasNoneRow ? root.cardSpacing : 0) + metrics.paddingSide * 3
        backgroundColor: root.backgroundColor

        // Absorb clicks inside container so they do not reach the
        // backdrop MouseArea (which would dismiss the picker). QML
        // MouseArea has no propagation chain across siblings — winning
        // a press is purely z-order, and the backdrop and container
        // overlap geometrically: the inner one wins because the picker
        // root declares the backdrop FIRST and the container LAST, so
        // the container's children paint on top. This MouseArea fills
        // the container's gaps (between the layout cards' own
        // MouseAreas) and grabs presses there so they never reach the
        // backdrop. `Accessible.ignored: true` keeps this transparent
        // absorber out of the a11y tree — only the backdrop's
        // "Dismiss layout picker" MouseArea above should be announced
        // as the dismiss control.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
            onClicked: function (mouse) {
                mouse.accepted = true;
            }
        }

        // Title — shared popup-card typography (PopupCardTitle).
        PopupCardTitle {
            id: titleLabel

            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            anchors.top: parent.top
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            text: i18n("Choose Layout")
            color: root.textColor
        }

        // Layout grid
        Grid {
            id: gridView

            anchors.top: titleLabel.bottom
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            columns: root.gridColumns
            spacing: root.cardSpacing

            Repeater {
                model: root.gridLayouts
                delegate: layoutCardDelegate
            }
        }

        // The no-template card, on its own row under the grid and centered
        // against it. Outside the Grid on purpose: a Grid flows its children
        // in order, so this card would otherwise take the next free cell in
        // the last row and read as one more choice rather than as the way out
        // of the list. The Row is over a one-or-zero element slice, so on a
        // screen with no template family nothing is instantiated and the row
        // collapses to zero height.
        Row {
            id: noneRow

            anchors.top: gridView.bottom
            anchors.topMargin: root.hasNoneRow ? root.cardSpacing : 0
            anchors.horizontalCenter: parent.horizontalCenter

            Repeater {
                model: root.hasNoneRow ? [root.layouts[root.layoutCount - 1]] : []
                delegate: layoutCardDelegate
            }
        }
    }

    // Shared by both containers above. Its position in a Repeater is no
    // longer its index in the model, so everything keyed on selection goes
    // through root.indexOfLayoutId rather than a delegate index.
    Component {
        id: layoutCardDelegate

        Item {
            id: layoutCard

            required property var modelData

            property var layoutData: modelData
            readonly property int modelIndex: root.indexOfLayoutId(layoutData.id)
            property bool isSelected: modelIndex === root.selectedIndex
            property bool isActive: layoutData.id === root.activeLayoutId
            property bool isHovered: cardMouse.containsMouse

            width: root.cardWidth
            height: root.cardHeight
            Accessible.role: Accessible.Button
            Accessible.name: layoutData.displayName || ""
            Accessible.focusable: true

            QFZCommon.LayoutCard {
                anchors.fill: parent
                layoutData: layoutCard.layoutData
                isActive: layoutCard.isActive
                isSelected: layoutCard.isSelected
                isHovered: layoutCard.isHovered
                globalAutoAssign: root.globalAutoAssign
                // The no-template row stands for an absence, so it has
                // no zones and would otherwise draw an empty well that
                // reads as a card which failed to load. The same
                // symbol vocabulary as the lock overlay below.
                placeholderIcon: layoutCard.layoutData.id === root.noTemplateId ? "edit-none" : ""
                showMasterDot: layoutCard.layoutData.isAutotile === true && layoutCard.layoutData.supportsMasterCount === true
                producesOverlappingZones: layoutCard.layoutData.producesOverlappingZones === true
                zoneNumberDisplay: layoutCard.layoutData.zoneNumberDisplay || "all"
                previewWidth: root.previewWidth
                previewHeight: root.previewHeight
                // Layout picker features
                showCardBackground: true
                // Zone appearance (consistent with zone selector)
                zonePadding: 1
                edgeGap: 1
                minZoneSize: 8
                zoneHighlightColor: root.highlightColor
                zoneInactiveColor: root.inactiveColor
                zoneBorderColor: root.borderColor
                activeOpacity: root.activeOpacity
                inactiveOpacity: root.inactiveOpacity
                // Theme
                highlightColor: root.highlightColor
                textColor: root.textColor
                backgroundColor: root.backgroundColor
                // Font
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                fontStrikeout: root.fontStrikeout
                animationDuration: Kirigami.Units.shortDuration
            }

            // Lock overlay for non-active layouts — absorbs all mouse events
            Rectangle {
                anchors.fill: parent
                visible: root.locked && !layoutCard.isActive
                z: 100
                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                radius: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "object-locked"
                    width: Math.min(parent.width, parent.height) * 0.3
                    height: width
                    color: Kirigami.Theme.textColor
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.ForbiddenCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: i18nc("@info:whatsthis layout picker lock overlay", "Layout is locked. Unlock the current layout before switching to another one.")
                    onClicked: function (mouse) {
                        mouse.accepted = true;
                    }
                    onPressed: function (mouse) {
                        mouse.accepted = true;
                    }
                }
            }

            MouseArea {
                id: cardMouse

                anchors.fill: parent
                hoverEnabled: true
                enabled: !(root.locked && !layoutCard.isActive)
                cursorShape: root.locked && !layoutCard.isActive ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                onClicked: {
                    if (root.locked)
                        return;

                    root.selectedIndex = layoutCard.modelIndex;
                    root.confirmSelection();
                }
                onEntered: {
                    if (root.locked && !layoutCard.isActive)
                        return;

                    root.selectedIndex = layoutCard.modelIndex;
                }
            }
        }
    }
}
