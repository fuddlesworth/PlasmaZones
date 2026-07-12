// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Domain-agnostic "Sets" page, shared by Decoration Sets and Motion
 *        Sets (the ShaderBrowserPage pattern, one component + thin wrappers).
 *
 * A set bundles the user's pack chains into one shareable JSON file.
 * Applying a set MERGES: paths it covers are replaced, paths it does not
 * cover are left alone.
 *
 * The host supplies a `bridge` (a PlasmaZones::ShaderSetStore) implementing:
 *
 *   QVariantList availableSets()      // rows: name, description, slug,
 *                                     //   coverage[], coverageCount,
 *                                     //   hasBaseline, active, modified
 *   bool         applySet(name)
 *   bool         saveCurrentAsSet(name, description)
 *   bool         removeSet(name)
 *   bool         updateSet(oldName, newName, description)
 *   bool         exportSet(name, localPath)
 *   bool         importSet(pathOrUrl)
 *   void         openSetsDirectory()
 *
 *   signal setsChanged()
 *   signal pendingChangesChanged()
 *   signal toastRequested(text)
 *
 * plus the domain-worded copy below.
 */
SettingsFlickable {
    id: root

    required property var bridge

    // ── Domain-tuned copy. Every wrapper sets these, so the defaults are
    //    only the fallback for a host that forgets one.
    property string infoBannerText: ""
    property string saveDescription: ""
    property string importDescription: ""
    property string emptyStateText: i18n("No sets saved yet.")
    property string nameFieldAccessibleName: i18n("Set name")
    property string descriptionFieldAccessibleName: i18n("Set description")
    /// token (e.g. "window") → translated coverage-chip label.
    required property var coverageLabel
    /// count → translated "%n Surfaces" / "%n Overrides" badge label.
    required property var coverageCountLabel
    /// name → translated apply-confirmation subtitle.
    required property var applySubtitleFor
    /// Search deep-link anchors (registered in searchcatalog.cpp).
    property string saveAnchor: ""
    property string importAnchor: ""
    property string savedAnchor: ""

    // Loaded from a Q_INVOKABLE and refreshed manually on setsChanged —
    // Q_INVOKABLE results are not reactive across the QML binding boundary.
    // The store re-fires setsChanged on live-state edits too, so the `active`
    // badges stay honest while the user edits chains on other pages. The
    // bridge is null-guarded: `required` guarantees the property is assigned,
    // not that the assignment is non-null (same guard as ShaderBrowserPage).
    property var setsList: bridge ? bridge.availableSets() : []

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onSetsChanged() {
            root.setsList = root.bridge ? root.bridge.availableSets() : [];
        }

        // Surface controller-side diagnostics (a refused import, an
        // unwritable folder) through the shell toast. Without this the
        // reason vanishes and the user sees an unexplained no-op.
        function onToastRequested(text) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(text);
        }

        target: root.bridge
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: text.length > 0
            text: root.infoBannerText
        }

        // ── Save current state. Starts collapsed when the user already has
        //    sets, so the list (the thing they came for) is what greets them.
        //    Assigned once at load, NOT bound: as a binding it would snap the
        //    card shut under the cursor the moment the user's first save
        //    landed, and SettingsCard's header click writes `collapsed`
        //    imperatively anyway, which would break the binding on first use.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current state")
            searchAnchor: root.saveAnchor
            collapsible: true
            Component.onCompleted: collapsed = root.setsList.length > 0

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: root.saveDescription
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                // Stacked, not side-by-side: a 50/50 row made the description
                // column unreadably narrow at the typical settings width. The
                // Save button sits bottom-right so focus order matches
                // typeflow (name → description → save).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: nameField

                        Layout.fillWidth: true
                        placeholderText: i18n("Set name…")
                        Accessible.name: root.nameFieldAccessibleName
                    }

                    TextField {
                        id: descField

                        Layout.fillWidth: true
                        placeholderText: i18n("Description (optional)…")
                        Accessible.name: root.descriptionFieldAccessibleName
                    }

                    Kirigami.InlineMessage {
                        id: saveStatus

                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Error
                        showCloseButton: true
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: i18n("Save")
                        icon.name: "document-save"
                        Accessible.name: i18n("Save set")
                        enabled: nameField.text.trim().length > 0
                        onClicked: {
                            saveStatus.visible = false;
                            const ok = root.bridge.saveCurrentAsSet(nameField.text.trim(), descField.text.trim());
                            if (ok) {
                                nameField.text = "";
                                descField.text = "";
                            } else {
                                // A name collision and an unwritable folder both
                                // toast their own reason from the store. This
                                // banner covers the remaining case (an empty
                                // snapshot) so the form is never inert without
                                // an explanation.
                                saveStatus.text = i18n("Could not save the set. There must be something to capture, and the name must not already be taken.");
                                saveStatus.visible = true;
                            }
                        }
                    }
                }
            }
        }

        // ── Import card — the User shaders card's twin: description, drop
        //    zone, result banner, then the explicit sources right-aligned.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("User sets")
            searchAnchor: root.importAnchor
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: root.importDescription
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                FileDropZone {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    idleText: i18nc("@info drop-zone idle label", "Drop a set file here to import it")
                    hoverText: i18nc("@info drop-zone hover label", "Release to import the set")
                    // The store validates the payload against this domain's
                    // taxonomy and toasts the concrete refusal reason.
                    onFileDropped: function (url) {
                        importResult.show(root.bridge.importSet(url), url);
                    }
                }

                Kirigami.InlineMessage {
                    id: importResult

                    function show(ok, url) {
                        var basename = String(url).split("/").pop();
                        if (basename.length === 0)
                            basename = String(url);

                        if (ok) {
                            type = Kirigami.MessageType.Positive;
                            text = i18nc("@info set import success", "Imported set “%1”.", basename);
                        } else {
                            type = Kirigami.MessageType.Error;
                            text = i18nc("@info set import failure", "Could not import “%1”. The file must be a set saved from this page.", basename);
                        }
                        visible = true;
                        autoHideTimer.restart();
                    }

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: false
                    showCloseButton: true

                    Timer {
                        id: autoHideTimer

                        // Long enough to read the result without leaving a
                        // stale banner pinned to the card.
                        interval: Kirigami.Units.humanMoment * 3
                        onTriggered: importResult.visible = false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Import…")
                        icon.name: "document-import"
                        flat: true
                        Accessible.name: i18n("Import a set from a file")
                        onClicked: importDialog.open()
                    }

                    Button {
                        text: i18n("Open Folder")
                        icon.name: "folder-open"
                        flat: true
                        Accessible.name: i18n("Open the sets folder")
                        onClicked: root.bridge.openSetsDirectory()
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Saved sets (%1)", root.setsList.length)
            searchAnchor: root.savedAnchor

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.setsList.length === 0
                    text: root.emptyStateText
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                }

                Repeater {
                    model: root.setsList

                    // `modelData` is a required property on ShaderSetCard, so
                    // the Repeater injects each row into it.
                    delegate: ShaderSetCard {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        bridge: root.bridge
                        coverageLabel: root.coverageLabel
                        coverageCountLabel: root.coverageCountLabel
                        applySubtitleFor: root.applySubtitleFor
                    }
                }
            }
        }
    }

    FileDialog {
        id: importDialog

        title: i18n("Import Set")
        nameFilters: [i18n("PlasmaZones Set (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const path = settingsController.urlToLocalFile(selectedFile);
            importResult.show(root.bridge.importSet(path), path);
        }
    }
}
