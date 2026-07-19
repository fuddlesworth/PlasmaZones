// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Shortcut cheatsheet content — Item-rooted body hosted in
 * PassiveOverlayShell's cheatsheetSlot. Display-only: a centered card
 * listing every global shortcut grouped by category, filtered by the
 * tiling mode of the screen the sheet opened on.
 *
 * Data arrives via the host slot's bindings (C++ pushes `shortcuts`,
 * `currentMode`, `autotileAvailable` onto cheatsheetSlot; live mode
 * switches re-push and the group filter re-evaluates reactively).
 *
 * Keyboard: the shell surface is kbd-None, so Escape routes via the
 * daemon's dedicated ad-hoc grab (start.cpp); QML Shortcuts can never
 * fire here. The backdrop MouseArea is the pointer dismiss path.
 */
Item {
    id: root

    /// Catalog rows from ShortcutManager::cheatsheetModel(): one object per
    /// shortcut with id, label, category, categoryOrder, triggers (list of
    /// display strings), assigned (bool), mode ("all"|"snapping"|"autotile").
    property var shortcuts: []
    /// Tiling mode of the screen the sheet opened on:
    /// "snapping" | "autotile" | "scrolling".
    property string currentMode: "snapping"
    /// Global autotile feature gate. When off, the Autotile group hides in
    /// every mode (the mode is unreachable, so its shortcuts are noise).
    property bool autotileAvailable: true
    /// Corner radius the surface decoration rounds to (forwarded from slot).
    property real cardCornerRadius: Kirigami.Units.largeSpacing * 2
    property string fontFamily: ""
    property real fontSizeScale: 1

    /// Idempotency latch for `dismissRequested` — same contract as
    /// LayoutPickerContent's: rapid backdrop clicks during the fade-out
    /// window collapse into one dismiss per show cycle. No writer resets
    /// it; the Loader re-instantiates this component on every show.
    property bool _dismissed: false

    signal dismissRequested

    function _requestDismiss() {
        if (_dismissed)
            return;

        _dismissed = true;
        root.dismissRequested();
    }

    /// True when the given catalog row applies in the current mode.
    function rowVisible(row) {
        if (row.mode === "autotile")
            return root.autotileAvailable && root.currentMode === "autotile";
        if (row.mode === "snapping")
            return root.currentMode === "snapping";
        return true;
    }

    /// Rows regrouped into [{name, rows}] preserving the model's category
    /// order, with mode-inapplicable rows dropped. Recomputes reactively on
    /// shortcuts / currentMode / autotileAvailable changes.
    readonly property var groups: {
        var byCat = [];
        var index = {};
        for (var i = 0; i < shortcuts.length; i++) {
            var row = shortcuts[i];
            if (!rowVisible(row))
                continue;

            if (!(row.category in index)) {
                index[row.category] = byCat.length;
                byCat.push({
                    name: row.category,
                    rows: []
                });
            }
            byCat[index[row.category]].rows.push(row);
        }
        return byCat;
    }

    QtObject {
        id: metrics

        readonly property int containerPadding: Kirigami.Units.gridUnit * 2
        readonly property int columnWidth: Kirigami.Units.gridUnit * 19
        readonly property int columnSpacing: Kirigami.Units.gridUnit * 2
        readonly property int maxColumns: 3
        readonly property int columns: {
            var avail = root.width * 0.9 - containerPadding * 2;
            var fit = Math.floor((avail + columnSpacing) / (columnWidth + columnSpacing));
            return Math.max(1, Math.min(maxColumns, fit));
        }
        readonly property int contentWidth: columns * columnWidth + (columns - 1) * columnSpacing
        readonly property int maxContentHeight: Math.round(root.height * 0.85) - containerPadding * 2
    }

    // Backdrop — click outside to dismiss, same bare click-only backdrop
    // as LayoutPickerContent (no scrim; popup surfaces don't dim the
    // desktop).
    MouseArea {
        anchors.fill: parent
        onClicked: root._requestDismiss()
        Accessible.name: i18n("Dismiss shortcut cheatsheet")
        Accessible.role: Accessible.Button
    }

    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        width: metrics.contentWidth + metrics.containerPadding * 2
        height: contentColumn.implicitHeight + metrics.containerPadding * 2

        Accessible.name: i18n("Keyboard shortcuts")

        // Absorb clicks inside the card so they never reach the backdrop —
        // same sibling z-order contract as LayoutPickerContent.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
            onClicked: function (mouse) {
                mouse.accepted = true;
            }
        }

        ColumnLayout {
            id: contentColumn

            anchors.fill: parent
            anchors.margins: metrics.containerPadding
            spacing: Kirigami.Units.largeSpacing

            // Title — same centered DemiBold style as the picker's
            // "Choose Layout".
            Label {
                Layout.fillWidth: true
                text: i18n("Keyboard Shortcuts")
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.4
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }

            Flickable {
                id: scroller

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(groupFlow.implicitHeight, metrics.maxContentHeight)
                contentWidth: width
                contentHeight: groupFlow.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Flow {
                    id: groupFlow

                    width: scroller.width
                    spacing: metrics.columnSpacing

                    Repeater {
                        model: root.groups

                        delegate: Column {
                            id: groupColumn

                            required property var modelData

                            width: metrics.columnWidth
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 4
                                text: groupColumn.modelData.name
                                color: Kirigami.Theme.disabledTextColor
                            }

                            Repeater {
                                model: groupColumn.modelData.rows

                                delegate: RowLayout {
                                    id: shortcutRow

                                    required property var modelData

                                    width: metrics.columnWidth
                                    spacing: Kirigami.Units.smallSpacing

                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: shortcutRow.modelData.assigned ? i18nc("shortcut row: action, keys", "%1, %2", shortcutRow.modelData.label, shortcutRow.modelData.triggers[0]) : i18nc("shortcut row: action unassigned", "%1, unassigned", shortcutRow.modelData.label)

                                    Label {
                                        text: shortcutRow.modelData.label
                                        elide: Text.ElideRight
                                        font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                                        Layout.fillWidth: true
                                    }

                                    // One chip per key token of the first
                                    // bound sequence. A trailing "+" means
                                    // the plus key itself is the final token.
                                    Row {
                                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)
                                        visible: shortcutRow.modelData.assigned

                                        Repeater {
                                            model: {
                                                if (!shortcutRow.modelData.assigned)
                                                    return [];

                                                var seq = shortcutRow.modelData.triggers[0];
                                                var parts = seq.split("+").filter(function (p) {
                                                    return p.length > 0;
                                                });
                                                if (seq.endsWith("+"))
                                                    parts.push("+");
                                                return parts;
                                            }

                                            delegate: KeyChip {
                                                required property var modelData

                                                text: modelData
                                            }
                                        }
                                    }

                                    Label {
                                        text: i18n("Unassigned")
                                        color: Kirigami.Theme.disabledTextColor
                                        font.italic: true
                                        visible: !shortcutRow.modelData.assigned
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
