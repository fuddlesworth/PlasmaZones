// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
 * showLayoutPicker / hideLayoutPicker drive `Surface::show()` /
 * `Surface::hide()` directly.
 *
 * This Item only owns:
 *   - Data properties written by C++ (layouts, activeLayoutId, locked, …)
 *   - Keyboard navigation state (selectedIndex; moveSelection /
 *     confirmSelection are invoked from C++, see below)
 *   - The visible content tree (backdrop + popup frame + grid of cards)
 *   - The `_dismissed` latch + `dismissRequested` signal that C++ wires
 *     to Surface::hide() (via the host Window's signal forwarding)
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
    /// clicks during the keepMappedOnHide=true fade-out window can fire
    /// `dismissRequested` more than once before `Qt.WindowTransparentForInput`
    /// lands at the QWindow level. Without this, C++ runs Surface::hide()
    /// on an already-Hidden surface and the library logs a qCWarning per
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
    readonly property int gridColumns: Math.min(layoutCount, Math.max(3, Math.min(5, Math.ceil(Math.sqrt(layoutCount * 1.5)))))
    readonly property int gridRows: gridColumns > 0 ? Math.ceil(layoutCount / gridColumns) : 0
    // Card dimensions
    readonly property int previewWidth: metrics.previewWidth
    readonly property int previewHeight: Math.round(previewWidth / safeAspectRatio)
    readonly property int cardWidth: previewWidth + metrics.paddingSide * 2
    readonly property int cardHeight: previewHeight + metrics.containerPadding + metrics.paddingSide
    readonly property int cardSpacing: metrics.indicatorSpacing

    // Internal signals — host Window re-emits to its public signals.
    signal layoutSelected(string layoutId)
    /// User-initiated dismiss request (backdrop click, Escape, in-flight
    /// race). C++ event filter and OverlayService::hideLayoutPicker
    /// translate this into Surface::hide() — which then drives the library
    /// animator. Same shape as LayoutOsd / NavigationOsd for consistency.
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

        var col = selectedIndex % gridColumns;
        var row = Math.floor(selectedIndex / gridColumns);
        col = (col + dx + gridColumns) % gridColumns;
        row = (row + dy + gridRows) % gridRows;
        var newIndex = row * gridColumns + col;
        if (newIndex >= layoutCount) {
            // Clamp to last valid item in the target row
            var lastColInRow = Math.min(gridColumns, layoutCount - row * gridColumns) - 1;
            newIndex = row * gridColumns + Math.min(col, lastColInRow);
        }
        selectedIndex = Math.max(0, Math.min(layoutCount - 1, newIndex));
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
        readonly property int containerRadius: Kirigami.Units.largeSpacing * 2
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
    }

    // Main container card
    QFZCommon.PopupFrame {
        id: container

        // The SurfaceAnimator shader anchor lives inside PopupFrame (on
        // its captureItem), scoped to the card plus a glow margin, so
        // the card's glow is captured into show / hide transitions
        // instead of being clipped — see PopupFrame.qml.
        anchors.centerIn: parent
        width: gridView.width + metrics.containerPadding
        // top padding + title + gap below title + grid + bottom padding
        height: titleLabel.height + gridView.height + metrics.paddingSide * 3
        backgroundColor: root.backgroundColor
        containerRadius: metrics.containerRadius

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

        // Title
        Label {
            id: titleLabel

            anchors.top: parent.top
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            text: i18n("Choose Layout")
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.4
            font.weight: Font.DemiBold
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
                model: root.layouts

                Item {
                    id: layoutCard

                    property var layoutData: modelData
                    property bool isSelected: index === root.selectedIndex
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
                        showMasterDot: layoutCard.layoutData.isAutotile === true && layoutCard.layoutData.supportsMasterCount === true
                        producesOverlappingZones: layoutCard.layoutData.producesOverlappingZones === true
                        zoneNumberDisplay: layoutCard.layoutData.zoneNumberDisplay || "all"
                        previewWidth: root.previewWidth
                        previewHeight: root.previewHeight
                        // Layout picker features
                        showCardBackground: true
                        interactive: false
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

                            root.selectedIndex = index;
                            root.confirmSelection();
                        }
                        onEntered: {
                            if (root.locked && !layoutCard.isActive)
                                return;

                            root.selectedIndex = index;
                        }
                    }
                }
            }
        }
    }
}
