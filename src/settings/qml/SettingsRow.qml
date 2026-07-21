// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief Two-line setting row with title, description, and a right-aligned control.
 *
 * The default children of this item become the right-side control widget.
 *
 * SIZING: that control slot is a plain `Row` POSITIONER, not a `RowLayout`, so
 * `Layout.*` attached properties on a default child are silently ignored — a
 * `Layout.fillWidth` or `Layout.preferredWidth` here does nothing and the
 * control renders at its implicit width. Give the child an explicit `width` in
 * `Kirigami.Units.gridUnit` multiples instead. (Wrapping the child in your own
 * `RowLayout` makes `Layout.*` work again for ITS children.)
 *
 * Usage:
 *   SettingsRow {
 *       title: i18n("Resolution")
 *       description: i18n("Resolution and refresh rate")
 *       ComboBox {
 *           width: Kirigami.Units.gridUnit * 12
 *           model: ["1080p", "1440p", "4K"]
 *       }
 *   }
 */
Item {
    id: root

    // ── Public API ──────────────────────────────────────────────────────
    property string title: ""
    property string description: ""
    /// Deep-link reveal anchor id for this row. Empty = not addressable.
    /// See SettingsFlickable.revealAnchor.
    property string searchAnchor: ""
    /// When true this row belongs to advanced mode only: it collapses out of
    /// the layout while the settings app is in simple mode. Declared here so
    /// the gate composes with the enabled-collapse default below instead of
    /// consumers replacing `visible:` by hand (which silently drops the
    /// enabled gate). Pair with `enabled:` for rows that ALSO have an
    /// applicability condition (e.g. only meaningful while borders are on).
    property bool advancedOnly: false
    default property alias content: controlContainer.data

    // A disabled row is hidden rather than shown super-dimmed: when a setting
    // can't apply in the current state there's nothing actionable in it, so it
    // collapses out of the layout instead of leaving dead, greyed space. This
    // tracks `enabled` directly, so a row disabled by an ancestor (e.g. a
    // card-level master toggle) hides too. The advancedOnly tier folds in
    // multiplicatively. Consumers that set their own `visible` binding
    // override BOTH gates (those rows manage their own show/hide).
    visible: enabled && (!advancedOnly || settingsController.advancedMode)

    Layout.fillWidth: true
    implicitWidth: rowLayout.implicitWidth
    implicitHeight: rowLayout.implicitHeight
    Accessible.name: root.title
    Accessible.description: root.description
    Accessible.role: Accessible.Row

    Component.onCompleted: {
        if (root.searchAnchor.length > 0)
            Qt.callLater(root._registerSearchAnchor);
    }
    Component.onDestruction: {
        if (root.searchAnchor.length > 0)
            root._unregisterSearchAnchor();
    }

    // Register this row's reveal anchor with the hosting page, resolving the
    // page + collapsible card via the shared walk-up helpers. Deferred via
    // callLater because SettingsCard reparents its contentItem — the parent
    // chain to the page is only complete after construction settles.
    function _registerSearchAnchor() {
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.registerSearchAnchor(root.searchAnchor, root);
    }
    function _unregisterSearchAnchor() {
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.unregisterSearchAnchor(root.searchAnchor, root);
    }

    RowLayout {
        id: rowLayout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Kirigami.Units.largeSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        // Left side: title + description
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
            spacing: Kirigami.Units.smallSpacing / 2

            Label {
                text: root.title
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Label {
                text: root.description
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                visible: root.description.length > 0
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }

        // Right side: control widget (default children)
        Row {
            id: controlContainer

            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            Layout.maximumWidth: rowLayout.width * 0.45
            spacing: Kirigami.Units.smallSpacing
        }
    }
}
