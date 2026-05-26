// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Breadcrumb trail for the current page.
 *
 * Walks parentId links upward from the current page; each segment but
 * the last is clickable and navigates to that ancestor.
 *
 * Visual model matches the legacy PlasmaZones chrome: plain Labels at
 * 0.5 opacity by default, fading to 0.8 + an underline on hover for
 * clickable segments. Separator is a `›` (U+203A) Label between
 * segments, not an icon — keeps the trail lightweight and theme-
 * independent.
 */
RowLayout {
    //* Ordered ancestors → current. Each entry is a page-data dict.

    id: root

    required property ApplicationController controller
    //  Cycle guard mirrors ApplicationController::parentChainFor's
    //  kMaxParentChainHops — a misregistered page with `parentId ==
    //  own id` (or two pages mutually parenting each other) would
    //  otherwise freeze the UI thread on first render.
    readonly property int _maxParentChainHops: 32
    readonly property var segments: {
        const out = [];
        const seen = ({
        });
        let id = root.controller.currentPageId;
        let hops = 0;
        while (id && hops < root._maxParentChainHops) {
            if (seen[id])
                break;

            seen[id] = true;
            const data = root.controller.registry.pageData(id);
            if (!data || !data.id)
                break;

            out.unshift(data);
            id = data.parentId;
            hops++;
        }
        return out;
    }

    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: root.segments

        delegate: RowLayout {
            id: segmentRow

            required property int index
            required property var modelData
            readonly property bool isLast: index === root.segments.length - 1
            readonly property bool clickable: !isLast

            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: segmentLabel

                text: segmentRow.modelData.title
                opacity: segmentRow.clickable && segmentMouse.containsMouse ? 0.8 : 0.5
                font.underline: segmentRow.clickable && segmentMouse.containsMouse
                Accessible.name: text
                Accessible.role: segmentRow.clickable ? Accessible.Link : Accessible.StaticText

                MouseArea {
                    id: segmentMouse

                    anchors.fill: parent
                    hoverEnabled: segmentRow.clickable
                    enabled: segmentRow.clickable
                    cursorShape: segmentRow.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.controller.currentPageId = segmentRow.modelData.id
                }

            }

            QQC2.Label {
                visible: !segmentRow.isLast
                // U+203A SINGLE RIGHT-POINTING ANGLE QUOTATION MARK —
                // matches the legacy separator glyph. Lighter visual
                // weight than an icon and doesn't depend on the
                // freedesktop icon theme.
                text: "›"
                opacity: 0.5
            }

        }

    }

    // Spacer to push subsequent items in the parent layout to the right.
    Item {
        Layout.fillWidth: true
    }

}
