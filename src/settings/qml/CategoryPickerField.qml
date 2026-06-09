// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief A combo-like field that opens a categorized master-detail picker.
 *
 * The field/action lists in the rule editor are long and flat; this groups
 * them into categories. The closed control reads like a ComboBox; the open
 * popup shows the category list on the left and the hovered category's items
 * on the right (a single in-scene popup, NOT nested Menu fly-outs — the rule
 * editor lives in a Kirigami.OverlaySheet where nested popups fight the
 * sheet's modal/z-order, so the proven single-popup overlay machinery from
 * WideComboBox is reused here verbatim).
 *
 * Model: `options` is the flat list each picker already produces — every
 * entry carries `{ value, label, category, categoryOrder, ... }`. Grouping is
 * derived from `category`/`categoryOrder`, so the (enum-interleaved) source
 * order does not matter.
 */
Item {
    id: root

    /// Flat option list: `[{ <valueRole>, <textRole>, category, categoryOrder }]`.
    property var options: []
    /// Which option key identifies an entry (the field picker keys on "wire",
    /// the action picker on "value").
    property string valueRole: "value"
    /// Which option key holds the user-visible label.
    property string textRole: "label"
    /// Currently-selected value (compared against `option[valueRole]`); the
    /// closed field shows its label.
    property string currentValue: ""
    /// Shown on the closed field when `currentValue` matches no option.
    property string placeholderText: ""
    /// Accessible name for the closed field.
    property string accessibleName: ""
    /// Optional per-option dim predicate — `function(option) -> bool`. A dimmed
    /// option stays selectable (matches the prior combo's "incompatible but
    /// pickable" UX); it renders at reduced opacity with a warning marker.
    property var dimPredicate: null
    /// Tooltip shown on a dimmed option's warning marker.
    property string dimTooltip: ""

    /// Emitted with the chosen option's `value`.
    signal activated(string value)

    implicitWidth: field.implicitWidth
    implicitHeight: field.implicitHeight

    // ── Grouping (category label/order → items) ──────────────────────────
    readonly property var _groups: _buildGroups(options)

    function _buildGroups(opts) {
        var byOrder = ({});
        for (var i = 0; i < opts.length; ++i) {
            var o = opts[i];
            var k = o.categoryOrder;
            if (byOrder[k] === undefined)
                byOrder[k] = {
                    "order": k,
                    "label": o.category,
                    "items": []
                };
            byOrder[k].items.push(o);
        }
        var arr = [];
        for (var key in byOrder)
            arr.push(byOrder[key]);
        arr.sort(function (a, b) {
            return a.order - b.order;
        });
        return arr;
    }

    function _labelFor(value) {
        for (var i = 0; i < root.options.length; ++i)
            if (root.options[i][root.valueRole] === value)
                return root.options[i][root.textRole];
        return "";
    }

    function _isDimmed(option) {
        return root.dimPredicate ? root.dimPredicate(option) : false;
    }

    function _select(value) {
        pop.close();
        if (value !== root.currentValue)
            root.activated(value);
    }

    // ── Closed field (reads like a ComboBox) ─────────────────────────────
    AbstractButton {
        id: field

        anchors.fill: parent
        implicitWidth: Math.max(Kirigami.Units.gridUnit * 8, fieldRow.implicitWidth + Kirigami.Units.largeSpacing)
        implicitHeight: Math.max(Kirigami.Units.gridUnit * 1.6, fieldRow.implicitHeight + Kirigami.Units.smallSpacing)
        hoverEnabled: true
        Accessible.name: root.accessibleName
        Accessible.role: Accessible.ComboBox

        onClicked: pop.opened ? pop.close() : pop.open()

        background: Rectangle {
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: (field.hovered || pop.opened) ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
        }

        contentItem: RowLayout {
            id: fieldRow

            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                readonly property string _label: root._labelFor(root.currentValue)
                text: _label !== "" ? _label : root.placeholderText
                opacity: _label !== "" ? 1 : 0.6
                color: Kirigami.Theme.textColor
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Kirigami.Icon {
                Layout.rightMargin: Kirigami.Units.smallSpacing
                source: "arrow-down"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
        }
    }

    // ── Outside-click catcher (verbatim approach from WideComboBox) ───────
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
                    const rootPos = field.mapToItem(catcher, 0, 0);
                    const onField = mouse.x >= rootPos.x && mouse.y >= rootPos.y && mouse.x < rootPos.x + field.width && mouse.y < rootPos.y + field.height;
                    pop.close();
                    mouse.accepted = onField;
                }
            }
        }
    }

    // ── Master-detail popup ──────────────────────────────────────────────
    T.Popup {
        id: pop

        // In-scene popup reparented to the application overlay so it escapes
        // the hosting OverlaySheet's modal layer — same rationale (and z /
        // popupType / manual positioning) as WideComboBox's popup.
        popupType: T.Popup.Item
        parent: Overlay.overlay
        z: 999999
        modal: false
        dim: false
        closePolicy: T.Popup.CloseOnEscape
        x: 0
        y: 0
        onAboutToShow: {
            // Reset the detail pane to the category containing the current
            // selection (or the first category) each time it opens.
            categoryList.currentIndex = Math.max(0, root._indexOfCategoryFor(root.currentValue));
            if (parent) {
                const pos = field.mapToItem(parent, 0, field.height);
                x = pos.x;
                y = pos.y;
            }
        }
        width: categoryList.implicitWidth + itemList.implicitWidth + 2
        height: Math.min(Math.max(categoryList.contentHeight, itemList.contentHeight) + 2, (root.Window.window ? root.Window.window.height : 600) - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        padding: 1

        contentItem: RowLayout {
            spacing: 0

            // Left: categories.
            ListView {
                id: categoryList

                Layout.fillHeight: true
                implicitWidth: Math.max(Kirigami.Units.gridUnit * 7, _widest)
                implicitHeight: contentHeight
                clip: true
                model: root._groups
                currentIndex: 0
                boundsBehavior: Flickable.StopAtBounds

                readonly property real _widest: {
                    var w = 0;
                    for (var i = 0; i < root._groups.length; ++i) {
                        catMetrics.text = root._groups[i].label;
                        w = Math.max(w, catMetrics.advanceWidth);
                    }
                    return w + Kirigami.Units.iconSizes.small + Kirigami.Units.largeSpacing * 2;
                }

                TextMetrics {
                    id: catMetrics
                }

                delegate: ItemDelegate {
                    id: catDelegate

                    required property var modelData
                    required property int index
                    width: categoryList.width
                    highlighted: categoryList.currentIndex === index
                    hoverEnabled: true
                    onHoveredChanged: if (hovered)
                        categoryList.currentIndex = index
                    // Clicking a category just keeps it open (selection happens
                    // on an item in the detail pane).
                    onClicked: categoryList.currentIndex = index

                    background: Rectangle {
                        color: catDelegate.highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.smallSpacing
                            text: catDelegate.modelData.label
                            color: Kirigami.Theme.textColor
                            font.weight: catDelegate.highlighted ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Kirigami.Icon {
                            Layout.rightMargin: Kirigami.Units.smallSpacing
                            source: "arrow-right"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.6
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            }

            // Right: items of the highlighted category.
            ListView {
                id: itemList

                Layout.fillHeight: true
                implicitWidth: Math.max(Kirigami.Units.gridUnit * 9, _widest)
                implicitHeight: contentHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: (categoryList.currentIndex >= 0 && categoryList.currentIndex < root._groups.length) ? root._groups[categoryList.currentIndex].items : []

                ScrollBar.vertical: ScrollBar {}

                readonly property real _widest: {
                    var w = 0;
                    for (var i = 0; i < root.options.length; ++i) {
                        itemMetrics.text = root.options[i][root.textRole];
                        w = Math.max(w, itemMetrics.advanceWidth);
                    }
                    return w + Kirigami.Units.iconSizes.small + Kirigami.Units.largeSpacing * 2;
                }

                TextMetrics {
                    id: itemMetrics
                }

                delegate: ItemDelegate {
                    id: itemDelegate

                    required property var modelData
                    required property int index
                    readonly property bool isCurrentSelection: itemDelegate.modelData[root.valueRole] === root.currentValue
                    readonly property bool _dimmed: root._isDimmed(itemDelegate.modelData)
                    width: itemList.width
                    highlighted: hovered
                    hoverEnabled: true
                    opacity: _dimmed ? 0.45 : 1
                    onClicked: root._select(itemDelegate.modelData[root.valueRole])

                    ToolTip.delay: 300
                    ToolTip.visible: hovered && _dimmed && root.dimTooltip !== ""
                    ToolTip.text: root.dimTooltip

                    background: Rectangle {
                        color: itemDelegate.highlighted ? Kirigami.Theme.highlightColor : itemDelegate.isCurrentSelection ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.smallSpacing
                            text: itemDelegate.modelData[root.textRole]
                            color: itemDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            font.weight: (itemDelegate.highlighted || itemDelegate.isCurrentSelection) ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Kirigami.Icon {
                            visible: itemDelegate._dimmed
                            Layout.rightMargin: Kirigami.Units.smallSpacing
                            source: "dialog-warning"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                    }
                }
            }
        }

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    /// Index into `_groups` of the category that contains @p value (or -1).
    function _indexOfCategoryFor(value) {
        for (var g = 0; g < root._groups.length; ++g) {
            var items = root._groups[g].items;
            for (var i = 0; i < items.length; ++i)
                if (items[i][root.valueRole] === value)
                    return g;
        }
        return -1;
    }
}
