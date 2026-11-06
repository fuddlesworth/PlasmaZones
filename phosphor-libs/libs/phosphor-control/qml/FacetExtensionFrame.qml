// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.phosphor.control
import "LoaderHelpers.js" as PhosphorLoaderHelpers

/**
 * One section another module adds to a page, as FacetSlot lists it.
 *
 * Shows a caption naming the module the section comes from, then the
 * section's QML, created with `domain` (the extension's staging domain) and
 * `settingsApp` already set: a section's root declares both, and may declare
 * them required. When the module failed to
 * load, or the QML does not load, an error card takes the section's place so
 * the rest of the page still works.
 */
ColumnLayout {
    id: root

    /** The map ApplicationController.extensionsFor() returns for one extension. */
    required property var extension
    required property ApplicationController settingsApp

    readonly property string errorText: {
        if (root.extension.error !== undefined)
            return root.extension.error;
        if (sectionLoader.status === Loader.Error)
            return qsTr("The section failed to load.");
        return "";
    }

    objectName: "facet-extension:" + root.extension.id
    spacing: Kirigami.Units.smallSpacing

    QQC2.Label {
        Layout.fillWidth: true
        text: qsTr("From %1").arg(root.extension.moduleName)
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        elide: Text.ElideRight
    }

    Loader {
        id: sectionLoader

        Layout.fillWidth: true
        visible: root.errorText.length === 0
        active: root.extension.error === undefined
        Component.onCompleted: {
            if (root.extension.qmlSource !== undefined) {
                sectionLoader.setSource(root.extension.qmlSource, {
                    "domain": root.extension.domain,
                    "settingsApp": root.settingsApp
                });
            }
        }
        onLoaded: PhosphorLoaderHelpers.bindItemWidthToLoader(sectionLoader)
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.errorText.length > 0
        type: Kirigami.MessageType.Error
        text: qsTr("%1 could not be shown. %2").arg(root.extension.name).arg(root.errorText)
    }
}
