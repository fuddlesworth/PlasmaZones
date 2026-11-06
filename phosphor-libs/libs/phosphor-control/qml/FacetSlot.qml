// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control

/**
 * A place on a page where other modules' sections appear.
 *
 * A page puts one at the top and the bottom, and one before or after each
 * anchor it lists in its metadata, or at a named extension point:
 *
 *     FacetSlot { settingsApp: page.settingsApp; pageId: "placement:snapping"; slot: "after:drag-modifier" }
 *
 * Each section the host resolved for that slot is shown in a
 * FacetExtensionFrame, in the order the extensions ask for. A slot with no
 * sections takes no space.
 */
ColumnLayout {
    id: root

    required property ApplicationController settingsApp
    /** The page's scoped id, `<module>:<page>`. */
    required property string pageId
    /** `top`, `bottom`, `before:<anchor>`, `after:<anchor>`, or an extension point id. */
    required property string slot

    /** The sections shown, as ApplicationController.extensionsFor() lists them. */
    property var extensions: []

    // extensionsFor() does not notify, so the list is fetched again whenever
    // the host reports a change for this page. Assigned rather than bound:
    // a binding would need a dummy read of a revision counter to depend on
    // the signal, and the AOT compiler drops such a read.
    // Initial properties arrive one by one, so pageId can change before
    // settingsApp is set.
    function _refresh() {
        root.extensions = root.settingsApp ? root.settingsApp.extensionsFor(root.pageId, root.slot) : [];
    }

    visible: root.extensions.length > 0
    spacing: Kirigami.Units.largeSpacing
    Component.onCompleted: root._refresh()
    onPageIdChanged: root._refresh()
    onSlotChanged: root._refresh()

    Connections {
        function onExtensionsChanged(changedPageId) {
            if (changedPageId === root.pageId)
                root._refresh();
        }

        target: root.settingsApp
    }

    Repeater {
        model: root.extensions

        delegate: FacetExtensionFrame {
            id: frame

            required property var modelData

            Layout.fillWidth: true
            extension: frame.modelData
            settingsApp: root.settingsApp
        }
    }
}
