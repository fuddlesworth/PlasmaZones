// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Profile picker — a ComboBox whose entries carry each profile's
 *        identicon, shared by every place a profile is chosen.
 *
 * The model is a list of rows shaped like ProfileStore::availableProfiles()
 * (`{ id, name, signature }`); a row with an empty `signature` (the "Defaults"
 * option in a parent picker) draws the blank mark, which reads as "nothing
 * inherited" beside the patterned ones.
 *
 * Popup styling follows LayoutComboBox, the app's dropdown convention: the
 * stock Menu-based popup is replaced by a plain ListView with the View colour
 * set pinned on the contentItem and background, and entries highlight with the
 * standard 0.15-alpha wash with a leading checkmark on the current one.
 */
ComboBox {
    id: root

    /// Smallest popup width, so the list stays readable when the field itself
    /// is narrow (the sidebar switcher in the compact rail).
    property real minimumPopupWidth: Kirigami.Units.gridUnit * 12

    textRole: "name"
    valueRole: "id"

    // ── Custom popup ────────────────────────────────────────────────────
    // Reparented to Overlay.overlay with a high z so it escapes any modal
    // layer (this control is used inside Kirigami dialogs); `popupType: Item`
    // keeps it in-scene, so outside-click dismissal is handled by the catcher
    // below — Qt's CloseOnPressOutside is unreliable once reparented.
    popup: T.Popup {
        id: pop

        popupType: T.Popup.Item
        parent: Overlay.overlay
        z: 999999
        modal: false
        dim: false
        closePolicy: T.Popup.CloseOnEscape
        // Positioned imperatively: a declarative mapToItem binding does not
        // re-evaluate when an ancestor moves (a dialog animating into place),
        // which would pin the popup at overlay origin.
        x: 0
        y: 0
        onAboutToShow: {
            if (parent) {
                const pos = root.mapToItem(parent, 0, root.height);
                x = pos.x;
                y = pos.y;
            }
        }
        width: Math.max(root.width, root.minimumPopupWidth)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (root.Window.window ? root.Window.window.height : 600) - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        // Horizontal clamping is off (-1) unless the side margins are set, so a
        // combo near the window edge would overflow and clip (same fix as
        // WideComboBox's popup).
        leftMargin: Kirigami.Units.smallSpacing
        rightMargin: Kirigami.Units.smallSpacing
        padding: 1
        // The Desktop style's field background reads `popup.exit.running` (its
        // ComboBox.qml:128). A bare Popup leaves enter/exit null, so that
        // binding throws "Cannot read property 'running' of null" the moment
        // the control is pressed. Empty transitions give it something non-null.
        enter: Transition {}
        exit: Transition {}

        // The View colorSet is pinned on the contentItem and background
        // individually, NOT on the Popup node: Kirigami's theme attachment
        // resolves through parentItem(), and those parent to the internal popup
        // item, so a pin on the Popup never reaches them.
        contentItem: ListView {
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
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
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    // Outside-click closer: while the popup is open a transparent catcher fills
    // the overlay, closes on any outside press, and consumes the press when it
    // lands on the field itself so it cannot immediately re-open.
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
                    const fieldPos = root.mapToItem(catcher, 0, 0);
                    const onField = mouse.x >= fieldPos.x && mouse.y >= fieldPos.y && mouse.x < fieldPos.x + root.width && mouse.y < fieldPos.y + root.height;
                    pop.close();
                    mouse.accepted = onField;
                }
            }
        }
    }

    delegate: ItemDelegate {
        id: entry

        required property var modelData
        required property int index

        readonly property bool isCurrentSelection: root.currentIndex === entry.index

        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        // Follow the popup's list, not the field: the field can be far narrower
        // than the popup (the compact rail). Reserve the scrollbar gutter so the
        // row ends at its edge rather than running under it.
        width: {
            const view = entry.ListView.view;
            if (!view)
                return root.width;
            const bar = view.ScrollBar ? view.ScrollBar.vertical : null;
            return view.width - (bar && bar.visible ? bar.width : 0);
        }
        // Only the hovered / keyboard-navigated row highlights; the current one
        // is marked by the checkmark instead.
        highlighted: root.highlightedIndex === entry.index

        background: Rectangle {
            color: entry.highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                visible: entry.isCurrentSelection
                source: "checkmark"
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.textColor
            }

            // Keeps the marks and names aligned on unchecked rows.
            Item {
                visible: !entry.isCurrentSelection
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            ProfileSignature {
                signature: entry.modelData.signature !== undefined ? entry.modelData.signature : ""
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: entry.modelData.name
                elide: Text.ElideRight
                color: Kirigami.Theme.textColor
            }
        }
    }
}
