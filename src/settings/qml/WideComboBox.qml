// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief ComboBox with a popup that always fits the widest item text.
 *
 * The KDE desktop style's Menu-based popup binds its width to the ComboBox
 * width and ignores external popup.width overrides.  This component replaces
 * the popup with a plain T.Popup + ListView (the same approach used by
 * LayoutComboBox) so the dropdown is never truncated.
 */
ComboBox {
    id: root

    // Cached widest-item width — recalculated only when model or count changes.
    // Using a separate TextMetrics avoids the binding loop caused by the
    // _longestItemWidth → metrics.text → advanceWidth → _longestItemWidth cycle.
    property real _longestItemWidth: 0

    function _recalcLongestWidth() {
        let maxW = 0;
        for (let i = 0; i < root.count; ++i) {
            metrics.text = root.textAt(i) || "";
            maxW = Math.max(maxW, metrics.advanceWidth);
        }
        _longestItemWidth = maxW;
    }

    implicitContentWidthPolicy: ComboBox.WidestTextWhenCompleted
    onCountChanged: Qt.callLater(_recalcLongestWidth)
    onModelChanged: Qt.callLater(_recalcLongestWidth)
    Component.onCompleted: _recalcLongestWidth()

    TextMetrics {
        id: metrics

        font: root.font
    }

    // Outside-click closer. While the popup is open, a transparent MouseArea
    // fills the application overlay and forwards presses outside the popup's
    // bounds to `pop.close()`. This replaces `T.Popup.CloseOnPressOutside`,
    // which is unreliable when the popup is reparented into `Overlay.overlay`
    // — the modal scrim doesn't render at the right z to receive outside
    // presses, so the press never reaches the popup's close heuristic and
    // only Escape works. Loader.active gates the catcher on popup state so we
    // never intercept events at idle.
    Loader {
        active: pop.opened
        sourceComponent: catcherComponent
    }

    Component {
        id: catcherComponent

        Item {
            id: catcher

            z: 999998
            Component.onCompleted: {
                const ovr = root.Overlay.overlay;
                if (ovr) {
                    parent = ovr;
                    anchors.fill = ovr;
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                propagateComposedEvents: true
                onPressed: function (mouse) {
                    // Map the ComboBox button rect into catcher-local coords
                    // (catcher fills Overlay.overlay, so these match the mouse
                    // event's coordinate system).
                    const rootPos = root.mapToItem(catcher, 0, 0);
                    const onCombo = mouse.x >= rootPos.x && mouse.y >= rootPos.y && mouse.x < rootPos.x + root.width && mouse.y < rootPos.y + root.height;
                    pop.close();
                    // If the press landed on the ComboBox button itself, eat
                    // the event so the button's own click handler doesn't
                    // immediately re-open the popup (close → re-open loop).
                    // Otherwise let it propagate so the click still reaches
                    // sheet content / other UI.
                    mouse.accepted = onCombo;
                }
            }
        }
    }

    popup: T.Popup {
        id: pop

        // View color set on the popup itself (not just the background) so the
        // delegates' idle fill and text colors resolve against the same View
        // background the popup draws.
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        // Re-parent to the application Overlay so the popup escapes any
        // hosting popup (e.g. Kirigami.OverlaySheet) that would otherwise
        // out-z-order it. Position is mapped from the ComboBox's local
        // coordinates into the overlay so the dropdown still anchors under
        // the combo. The bumped `z` puts us above the sheet's own popup
        // stack — without it, the sheet's modal layer wins and the dropdown
        // renders behind the sheet content.
        // `popupType: Popup.Item` pins the popup to in-scene rendering so the
        // KDE / Plasma style can't auto-promote it to a top-level OS window
        // (the promotion behaviour differs between Qt 6.x style versions and
        // would re-trigger the focus-loss regression that closes the host
        // OverlaySheet the moment a real window grabs focus).
        popupType: T.Popup.Item
        parent: Overlay.overlay
        z: 999999
        // `modal: false`: when the popup is reparented into `Overlay.overlay`,
        // Qt's modal scrim is drawn UNDER the popup in the overlay's child
        // stack — which is below the host OverlaySheet's own modal layer. The
        // scrim never receives outside presses, so `CloseOnPressOutside` never
        // fires and the dropdown can only be dismissed with Escape. The
        // outside-click catcher item below handles dismissal manually, so
        // modal: false is correct here.
        modal: false
        dim: false
        closePolicy: T.Popup.CloseOnEscape
        // Position is refreshed imperatively in `onAboutToShow` (below).
        // A declarative `x: root.mapToItem(...).x` binding only re-evaluates
        // when one of its named dependencies (`parent`, `root.height`)
        // changes — it does NOT re-evaluate when the ComboBox's ancestors
        // move, which is exactly what happens while a hosting OverlaySheet
        // animates into its final position. The result was a popup stuck at
        // overlay-origin (0, 0) instead of under the combo button.
        x: 0
        y: 0
        onAboutToShow: {
            if (parent) {
                const pos = root.mapToItem(parent, 0, root.height);
                x = pos.x;
                y = pos.y;
            }
        }
        width: Math.max(root.width, root._longestItemWidth + Kirigami.Units.gridUnit * 3)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (root.Window.window ? root.Window.window.height : 600) - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        padding: 1

        contentItem: ListView {
            id: popupList

            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            highlightMoveDuration: 0

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    delegate: ItemDelegate {
        required property var modelData
        required property int index
        readonly property bool isCurrentSelection: root.currentIndex === index

        // Reserve the scrollbar's gutter so the row content ends at the
        // scrollbar's left edge instead of running underneath it (avoids the
        // stray vertical-line seam beside the handle when the list scrolls).
        width: popupList.width - (popupList.ScrollBar.vertical.visible ? popupList.ScrollBar.vertical.width : 0)
        highlighted: root.highlightedIndex === index

        background: Rectangle {
            color: parent.highlighted ? Kirigami.Theme.highlightColor : parent.isCurrentSelection ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
        }

        contentItem: Label {
            text: root.textRole ? modelData[root.textRole] : modelData
            color: parent.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            font.weight: (parent.highlighted || parent.isCurrentSelection) ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}
