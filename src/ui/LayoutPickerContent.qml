// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout picker content — Item-rooted body for use inside the standalone
 * LayoutPickerOverlay Window. The picker uses CenteredModal layer-shell
 * role (exclusive keyboard, distinct anchors), so it can't share a
 * wl_surface with the OSDs in NotificationOverlay; the extraction is
 * still worthwhile for code-shape consistency with the OSD content
 * components and to keep future "lazy-create on shortcut" refactors
 * clean.
 *
 * Phase 5: surface lifecycle + show/hide animations are driven entirely
 * by PhosphorAnimationLayer::SurfaceAnimator (registered for
 * PzRoles::LayoutPicker with `osd.show` / `osd.pop` / `osd.hide`
 * profiles). PhosphorLayer::Surface handles `Qt.WindowTransparentForInput`
 * on the underlying QWindow during the hide cycle, and OverlayService::
 * showLayoutPicker / hideLayoutPicker drive `Surface::show()` /
 * `Surface::hide()` directly.
 *
 * This Item only owns:
 *   - Data properties written by C++ (layouts, activeLayoutId, locked, …)
 *   - Keyboard navigation state (selectedIndex + Shortcuts)
 *   - The visible content tree (backdrop + popup frame + grid of cards)
 *   - The `_dismissed` latch + `dismissRequested` signal that C++ wires
 *     to Surface::hide() (via the host Window's signal forwarding)
 */
Item {
    // Keyboard handling. All Shortcuts gate on root._shortcutsActive — see
    // the property doc for why this matters under keepMappedOnHide=true.
    // Escape originally relied on a C++ QObject::eventFilter on the picker
    // QQuickWindow. That works reliably for SnapAssist (which destroys on
    // hide) but on the keep-mapped picker the wl_surface lifecycle + the
    // Qt.WindowTransparentForInput flip during hide leaves the eventFilter
    // path inconsistent — KeyPress events don't always reach the QWindow
    // on the warm path. SnapAssistOverlay uses a QML Shortcut and that
    // path is robust against the same lifecycle, so use it here too.
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
    property color highlightColor: Kirigami.Theme.highlightColor
    // Zone appearance (set from C++ settings for consistency with zone selector)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
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
    /// Logically-shown gate for keyboard Shortcuts. Written by C++ alongside
    /// `Surface::show()` / `Surface::hide()` so a logically-hidden picker
    /// (still Qt-visible under keepMappedOnHide=true) doesn't silently
    /// respond to stray accelerator deliveries. See LayoutPickerOverlay.qml's
    /// alias forwarding and snapassist.cpp's matching writes.
    property bool _shortcutsActive: false
    /// Idempotency latch for `dismissRequested`. Multiple rapid backdrop
    /// clicks during the keepMappedOnHide=true fade-out window can fire
    /// `dismissRequested` more than once before `Qt.WindowTransparentForInput`
    /// lands at the QWindow level. Without this, C++ runs Surface::hide()
    /// on an already-Hidden surface and the library logs a qCWarning per
    /// spurious click. Reset by C++ explicitly on every show (QML's
    /// `on<Name>Changed` handler form does not work for underscore-
    /// prefixed properties).
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
    signal dismissRequested()

    /// Internal: emit dismissRequested at most once per show cycle.
    function _requestDismiss() {
        if (_dismissed)
            return ;

        _dismissed = true;
        root.dismissRequested();
    }

    function moveSelection(dx, dy) {
        if (layoutCount === 0 || root.locked)
            return ;

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
            return ;

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
        readonly property int previewWidth: 160
    }

    Shortcut {
        sequence: "Return"
        enabled: root._shortcutsActive
        onActivated: confirmSelection()
    }

    Shortcut {
        sequence: "Enter"
        enabled: root._shortcutsActive
        onActivated: confirmSelection()
    }

    Shortcut {
        sequence: "Left"
        enabled: root._shortcutsActive
        onActivated: moveSelection(-1, 0)
    }

    Shortcut {
        sequence: "Right"
        enabled: root._shortcutsActive
        onActivated: moveSelection(1, 0)
    }

    Shortcut {
        sequence: "Up"
        enabled: root._shortcutsActive
        onActivated: moveSelection(0, -1)
    }

    Shortcut {
        sequence: "Down"
        enabled: root._shortcutsActive
        onActivated: moveSelection(0, 1)
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

        anchors.centerIn: parent
        width: gridView.width + metrics.containerPadding
        // top padding + title + gap below title + grid + bottom padding
        height: titleLabel.height + gridView.height + metrics.paddingSide * 3
        backgroundColor: root.backgroundColor
        textColor: root.textColor
        containerRadius: metrics.containerRadius

        // Absorb clicks inside container to prevent backdrop dismiss
        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
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
                    Accessible.name: layoutData.name || ""
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
                        color: Qt.rgba(0, 0, 0, 0.5)
                        radius: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            source: "object-locked"
                            width: Math.min(parent.width, parent.height) * 0.3
                            height: width
                            color: "white"
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.ForbiddenCursor
                            onClicked: function(mouse) {
                                mouse.accepted = true;
                            }
                            onPressed: function(mouse) {
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
                                return ;

                            root.selectedIndex = index;
                            root.confirmSelection();
                        }
                        onEntered: {
                            if (root.locked && !layoutCard.isActive)
                                return ;

                            root.selectedIndex = index;
                        }
                    }

                }

            }

        }

    }

}
