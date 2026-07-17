// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Zone Selector content body — Item version of the legacy
 * ZoneSelectorWindow.qml, hosted inside the unified
 * PassiveOverlayShell's zoneSelector slot.
 *
 * Shell migration: the per-VS PhosphorRoles::ZoneSelector wl_surface is
 * replaced by an Item slot inside the per-screen passive shell.
 * Per-VS positioning is handled by the C++ side sizing the slot's
 * `width` / `height` to match the VS rect; anchors live on the slot
 * Item, not on a layer-shell window.
 */
Item {
    id: root

    // The SurfaceAnimator shader anchor lives inside the `container`
    // PopupFrame (on its captureItem) — NOT on this fullscreen root,
    // which would key the per-leg shader to the entire shell rect and
    // smear the effect across the whole screen on every show/hide.
    // Layout data
    property var layouts: []
    property string activeLayoutId: ""
    property bool globalAutoAssign: false
    property string selectedLayoutId: ""
    property int selectedZoneIndex: -1
    property int minZoneSize: 8
    property int cursorX: -1
    property int cursorY: -1
    property int selectorPosition: 0
    property int indicatorWidth: 180
    property int indicatorHeight: 101
    property int indicatorSpacing: 18
    property int layoutColumns: 1
    property int contentWidth: 180
    property int contentHeight: 129
    property int containerPadding: 36
    property int containerPaddingSide: 18
    property int containerTopMargin: 10
    property int containerSideMargin: 10
    /// Effective edge margins for corner / edge selector positions. The
    /// captureItem inside QFZCommon.PopupFrame extends `container.captureMargin`
    /// past the visible card on every side; if `containerTopMargin` or
    /// `containerSideMargin` is smaller than that, the capture ring (and the
    /// decoration halo drawn into it) is pushed off the slot edge and clipped,
    /// leaving an empty margin on the SurfaceAnimator FBO grab. Clamp to at
    /// least the captureMargin so the ring stays on-screen at every position
    /// state.
    ///
    /// Use the Kirigami literal directly (not `container.captureMargin`)
    /// because `container` (the QFZCommon.PopupFrame instance) is declared later
    /// in this file and would resolve to `undefined` during initial
    /// construction — `Math.max(int, undefined) === NaN`, which silently coerces
    /// to 0 when assigned to anchors.topMargin / leftMargin etc. and produces a
    /// one-frame flash with the selector flush against the screen edge before
    /// the binding re-evaluates. PopupFrame's `captureMargin` is exactly this
    /// same expression (`Kirigami.Units.gridUnit * 1.25`), so the literal is
    /// always in sync with the source of truth.
    readonly property real _captureMargin: Math.ceil(Kirigami.Units.gridUnit * 1.25)
    readonly property real effectiveTopMargin: Math.max(containerTopMargin, _captureMargin)
    readonly property real effectiveSideMargin: Math.max(containerSideMargin, _captureMargin)
    property int labelTopMargin: 8
    property int labelHeight: 20
    property int labelSpace: 28
    property int cardPadding: 26
    property int cardSidePadding: 18
    property int containerWidth: 216
    property int containerHeight: 165
    property int scrollContentHeight: 129
    property int scrollContentWidth: 180
    property bool needsScrolling: false
    property bool needsHorizontalScrolling: false
    property int scaledPadding: 1
    property int scaledBorderWidth: 1
    property int scaledBorderRadius: 2
    property bool locked: false
    property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    readonly property color fadeColor: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, autoScrollConstants.fadeOpacity)

    function applyScrollDelta(angleDeltaY) {
        var step = angleDeltaY / 120 * 0.1;
        if (root.needsScrolling) {
            var vBar = scrollView.ScrollBar.vertical;
            vBar.position = Math.max(0, Math.min(1 - vBar.size, vBar.position - step));
        } else if (root.needsHorizontalScrolling) {
            var hBar = scrollView.ScrollBar.horizontal;
            hBar.position = Math.max(0, Math.min(1 - hBar.size, hBar.position - step));
        }
    }

    anchors.fill: parent

    QtObject {
        id: animationConstants

        readonly property int shortDuration: Kirigami.Units.shortDuration
        readonly property int normalDuration: Kirigami.Units.longDuration
    }

    QtObject {
        id: autoScrollConstants

        readonly property int edgeMargin: Kirigami.Units.gridUnit * 6
        readonly property real maxSpeed: 0.07
        readonly property int interval: 16
        readonly property real scrollThreshold: 0.01
        readonly property real fadeOpacity: 0.95
    }

    Timer {
        id: autoScrollTimer

        interval: autoScrollConstants.interval
        repeat: true
        running: root.visible && (root.needsScrolling || root.needsHorizontalScrolling) && root.cursorX >= 0
        onTriggered: {
            var local = scrollView.mapFromItem(null, root.cursorX, root.cursorY);
            var lx = local.x;
            var ly = local.y;
            if (root.needsScrolling) {
                var vBar = scrollView.ScrollBar.vertical;
                if (ly >= 0 && ly < autoScrollConstants.edgeMargin) {
                    var topFactor = 1 - ly / autoScrollConstants.edgeMargin;
                    vBar.position = Math.max(0, vBar.position - autoScrollConstants.maxSpeed * topFactor);
                } else if (ly > scrollView.height - autoScrollConstants.edgeMargin && ly <= scrollView.height) {
                    var bottomFactor = 1 - (scrollView.height - ly) / autoScrollConstants.edgeMargin;
                    var maxPos = 1 - vBar.size;
                    vBar.position = Math.min(maxPos, vBar.position + autoScrollConstants.maxSpeed * bottomFactor);
                }
            }
            if (root.needsHorizontalScrolling) {
                var hBar = scrollView.ScrollBar.horizontal;
                if (lx >= 0 && lx < autoScrollConstants.edgeMargin) {
                    var leftFactor = 1 - lx / autoScrollConstants.edgeMargin;
                    hBar.position = Math.max(0, hBar.position - autoScrollConstants.maxSpeed * leftFactor);
                } else if (lx > scrollView.width - autoScrollConstants.edgeMargin && lx <= scrollView.width) {
                    var rightFactor = 1 - (scrollView.width - lx) / autoScrollConstants.edgeMargin;
                    var maxHPos = 1 - hBar.size;
                    hBar.position = Math.min(maxHPos, hBar.position + autoScrollConstants.maxSpeed * rightFactor);
                }
            }
        }
    }

    QFZCommon.PopupFrame {
        id: container

        // The SurfaceAnimator shader anchor, and the `shaderAnchor`
        // objectName selector_update.cpp polishes, both live inside
        // PopupFrame (on its captureItem) so the card's glow is captured
        // into show / hide transitions — see PopupFrame.qml.
        width: root.containerWidth
        height: root.containerHeight
        backgroundColor: root.backgroundColor
        state: {
            switch (root.selectorPosition) {
            case 0:
                return "topLeft";
            case 1:
                return "top";
            case 2:
                return "topRight";
            case 3:
                return "left";
            case 4:
                return "center";
            case 5:
                return "right";
            case 6:
                return "bottomLeft";
            case 7:
                return "bottom";
            case 8:
                return "bottomRight";
            default:
                return "top";
            }
        }
        states: [
            State {
                name: "topLeft"

                AnchorChanges {
                    target: container
                    anchors.top: parent.top
                    anchors.bottom: undefined
                    anchors.left: parent.left
                    anchors.right: undefined
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.topMargin: root.effectiveTopMargin
                    anchors.leftMargin: root.effectiveSideMargin
                }
            },
            State {
                name: "top"

                AnchorChanges {
                    target: container
                    anchors.top: parent.top
                    anchors.bottom: undefined
                    anchors.left: undefined
                    anchors.right: undefined
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                PropertyChanges {
                    target: container
                    anchors.topMargin: root.effectiveTopMargin
                }
            },
            State {
                name: "topRight"

                AnchorChanges {
                    target: container
                    anchors.top: parent.top
                    anchors.bottom: undefined
                    anchors.left: undefined
                    anchors.right: parent.right
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.topMargin: root.effectiveTopMargin
                    anchors.rightMargin: root.effectiveSideMargin
                }
            },
            State {
                name: "left"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: undefined
                    anchors.left: parent.left
                    anchors.right: undefined
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.leftMargin: root.effectiveSideMargin
                }
            },
            State {
                name: "center"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: undefined
                    anchors.left: undefined
                    anchors.right: undefined
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            },
            State {
                name: "right"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: undefined
                    anchors.left: undefined
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.rightMargin: root.effectiveSideMargin
                }
            },
            State {
                name: "bottomLeft"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: undefined
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.bottomMargin: root.effectiveTopMargin
                    anchors.leftMargin: root.effectiveSideMargin
                }
            },
            State {
                name: "bottom"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: parent.bottom
                    anchors.left: undefined
                    anchors.right: undefined
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                PropertyChanges {
                    target: container
                    anchors.bottomMargin: root.effectiveTopMargin
                }
            },
            State {
                name: "bottomRight"

                AnchorChanges {
                    target: container
                    anchors.top: undefined
                    anchors.bottom: parent.bottom
                    anchors.left: undefined
                    anchors.right: parent.right
                    anchors.verticalCenter: undefined
                    anchors.horizontalCenter: undefined
                }

                PropertyChanges {
                    target: container
                    anchors.bottomMargin: root.effectiveTopMargin
                    anchors.rightMargin: root.effectiveSideMargin
                }
            }
        ]

        ScrollView {
            id: scrollView

            anchors.centerIn: parent
            width: root.contentWidth
            height: root.contentHeight
            clip: root.needsScrolling || root.needsHorizontalScrolling
            contentWidth: root.needsHorizontalScrolling ? root.scrollContentWidth : root.contentWidth
            contentHeight: root.needsScrolling ? root.scrollContentHeight : root.contentHeight
            ScrollBar.vertical.policy: root.needsScrolling ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: root.needsHorizontalScrolling ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

            GridLayout {
                id: contentGrid

                objectName: "zoneSelectorContentGrid"
                width: root.needsHorizontalScrolling ? root.scrollContentWidth : root.contentWidth
                height: root.needsScrolling ? root.scrollContentHeight : root.contentHeight
                columns: root.layoutColumns
                rowSpacing: root.indicatorSpacing
                columnSpacing: root.indicatorSpacing

                Repeater {
                    model: root.layouts

                    delegate: Item {
                        id: indicator

                        required property var modelData
                        required property int index
                        property string layoutId: modelData.id || ""
                        property bool isActive: layoutId === root.activeLayoutId
                        property bool hasSelectedZone: root.selectedLayoutId === layoutId

                        width: root.indicatorWidth + root.cardSidePadding * 2
                        height: root.indicatorHeight + root.labelSpace + root.cardPadding
                        Layout.preferredWidth: width
                        Layout.preferredHeight: height

                        QFZCommon.LayoutCard {
                            anchors.fill: parent
                            layoutData: indicator.modelData
                            isActive: indicator.isActive
                            isSelected: indicator.hasSelectedZone
                            globalAutoAssign: root.globalAutoAssign
                            previewWidth: root.indicatorWidth
                            previewHeight: root.indicatorHeight
                            showCardBackground: true
                            // The zone-selector slot is input-transparent by design —
                            // OverlayService::updateSelectorPosition pushes cursor coords from
                            // the D-Bus drag stream and writes `selectedLayoutId` /
                            // `selectedZoneIndex` back; the commit happens at drag-end in
                            // WindowDragAdaptor's drop path (drop.cpp). ZonePreview carries no
                            // pointer handlers at all (its hover machinery was removed), so
                            // nothing here can switch the active layout on stray hover events.
                            selectedZoneIndex: indicator.hasSelectedZone ? root.selectedZoneIndex : -1
                            zonePadding: root.scaledPadding
                            edgeGap: root.scaledPadding
                            minZoneSize: root.minZoneSize
                            zoneHighlightColor: root.highlightColor
                            zoneInactiveColor: root.inactiveColor
                            zoneBorderColor: root.borderColor
                            inactiveOpacity: root.inactiveOpacity
                            activeOpacity: root.activeOpacity
                            highlightColor: root.highlightColor
                            textColor: root.textColor
                            backgroundColor: root.backgroundColor
                            fontFamily: root.fontFamily
                            fontSizeScale: root.fontSizeScale
                            fontWeight: root.fontWeight
                            fontItalic: root.fontItalic
                            fontUnderline: root.fontUnderline
                            fontStrikeout: root.fontStrikeout
                            animationDuration: animationConstants.normalDuration
                            shortAnimationDuration: animationConstants.shortDuration
                            labelTopMargin: root.labelTopMargin
                            // No hover handling: ZonePreview has no MouseAreas.
                            // `selectedLayoutId` / `selectedZoneIndex` are written from C++
                            // (selector.cpp::updateSelectorPosition) so the highlight still
                            // tracks the cursor.
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: root.locked && !indicator.isActive
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
                                Accessible.name: i18nc("@info:whatsthis zone selector lock overlay", "Layout is locked. Switch to this layout before selecting a zone.")
                                onClicked: function (mouse) {
                                    mouse.accepted = true;
                                }
                                onPressed: function (mouse) {
                                    mouse.accepted = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.top: scrollView.top
            anchors.left: scrollView.left
            anchors.right: scrollView.right
            height: root.indicatorSpacing
            visible: root.needsScrolling && scrollView.ScrollBar.vertical.position > autoScrollConstants.scrollThreshold
            z: 10

            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.fadeColor
                }

                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }

        Rectangle {
            anchors.bottom: scrollView.bottom
            anchors.left: scrollView.left
            anchors.right: scrollView.right
            height: root.indicatorSpacing
            visible: root.needsScrolling && (scrollView.ScrollBar.vertical.position + scrollView.ScrollBar.vertical.size) < (1 - autoScrollConstants.scrollThreshold)
            z: 10

            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: "transparent"
                }

                GradientStop {
                    position: 1
                    color: root.fadeColor
                }
            }
        }

        Rectangle {
            anchors.top: scrollView.top
            anchors.bottom: scrollView.bottom
            anchors.left: scrollView.left
            width: root.indicatorSpacing
            visible: root.needsHorizontalScrolling && scrollView.ScrollBar.horizontal.position > autoScrollConstants.scrollThreshold
            z: 10

            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: root.fadeColor
                }

                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }

        Rectangle {
            anchors.top: scrollView.top
            anchors.bottom: scrollView.bottom
            anchors.right: scrollView.right
            width: root.indicatorSpacing
            visible: root.needsHorizontalScrolling && (scrollView.ScrollBar.horizontal.position + scrollView.ScrollBar.horizontal.size) < (1 - autoScrollConstants.scrollThreshold)
            z: 10

            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: "transparent"
                }

                GradientStop {
                    position: 1
                    color: root.fadeColor
                }
            }
        }

        Label {
            anchors.centerIn: parent
            text: i18nc("@info zone selector empty state", "No layouts available")
            color: Kirigami.Theme.disabledTextColor
            visible: root.layouts.length === 0
        }
    }
}
