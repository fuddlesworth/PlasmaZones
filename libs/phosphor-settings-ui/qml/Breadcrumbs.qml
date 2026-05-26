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
 * Walks parentId links upward from the current page; each segment but the
 * last is clickable and navigates to that ancestor.
 */
RowLayout {
    id: root

    required property ApplicationController controller
    //* Ordered ancestors → current. Each entry is a page-data dict.
    readonly property var segments: {
        const out = [];
        let id = root.controller.currentPageId;
        while (id) {
            const data = root.controller.registry.pageData(id);
            if (!data || !data.id)
                break;

            out.unshift(data);
            id = data.parentId;
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

            spacing: Kirigami.Units.smallSpacing

            QQC2.AbstractButton {
                enabled: !segmentRow.isLast
                onClicked: root.controller.currentPageId = segmentRow.modelData.id

                contentItem: QQC2.Label {
                    text: segmentRow.modelData.title
                    font.bold: segmentRow.isLast
                    color: segmentRow.isLast ? Kirigami.Theme.textColor : Kirigami.Theme.linkColor
                }

            }

            Kirigami.Icon {
                visible: !segmentRow.isLast
                source: "go-next-symbolic"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                opacity: 0.5
            }

        }

    }

    // Spacer to push subsequent items in the parent layout to the right.
    Item {
        Layout.fillWidth: true
    }

}
