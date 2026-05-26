// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Standard "About this application" page scaffold.
 *
 * The shell renders the conventional layout (icon + name + version on top,
 * description / copyright / license / homepage below) and exposes two
 * named slots for app-injected content:
 *
 *   - `topContent`  — a Component rendered ABOVE the icon header
 *                     (e.g. a daemon enable/disable toggle).
 *   - `extraContent` — a list of items rendered BELOW the homepage URL
 *                      (license / credits / build-info cards).
 *
 * Both slots are EXPLICIT-assignment only (not default property), because
 * Kirigami.ScrollablePage already has its own default property (the
 * scrollable content area) and shadowing it breaks the shell's layout.
 *
 * Usage:
 *
 *   AboutPageShell {
 *       appName: "phosphor-settings-ui-minimal"
 *       appIcon: "preferences-system"
 *       appVersion: "0.1.0"
 *       description: qsTr("A reusable Qt6/Kirigami settings framework.")
 *       homepageUrl: "https://example.com/phosphor"
 *
 *       topContent: Component {
 *           RowLayout { Label { text: qsTr("Enable daemon") } ; Switch {} }
 *       }
 *
 *       extraContent: [
 *           Kirigami.Heading { text: qsTr("Build info") },
 *           QQC2.Label  { text: qsTr("Compiled against Qt %1").arg(qVersion) }
 *       ]
 *   }
 */
Kirigami.ScrollablePage {
    // Named property aliases for the two consumer slots. NEITHER is the
    // shell's `default` — making `extraContent` the default property
    // hijacks Kirigami.ScrollablePage's own default (the scrollable
    // contentItem), which would route the shell's own internal
    // ColumnLayout into `extraColumn.data` (an alias to a child that
    // doesn't exist yet at that point). Without the `default` keyword,
    // the inner ColumnLayout below becomes the ScrollablePage's
    // contentItem as intended.

    id: root

    /** Content rendered below the homepage URL, inside the standard
     *  about-page scroll. Accepts list-assignment:
     *      AboutPageShell {
     *          extraContent: [ Card {...}, Card {...} ]
     *      } */
    property alias extraContent: extraColumn.data
    /** Optional content rendered ABOVE the icon/name/version header.
     *  Accepts a `Component` (instantiated by an internal Loader) —
     *  Component slots are bulletproof for single-root injection in
     *  scrollable pages. */
    property Component topContent: null
    property string appName: ""
    property string appIcon: ""
    property string appVersion: ""
    property string description: ""
    property string copyright: ""
    property string license: ""
    property string homepageUrl: ""

    title: qsTr("About")

    ColumnLayout {
        // ── Top content slot ──────────────────────────────────────
        // Component-based injection — the consumer supplies a
        // Component, this Loader instantiates it. The loader is
        // visible (and the separator below shows) only when a
        // Component has been provided.

        width: root.width - root.leftPadding - root.rightPadding
        spacing: Kirigami.Units.largeSpacing

        // Critical: the Loader fills the layout's width AND adopts
        // the loaded item's implicit height. Without
        // `Layout.preferredHeight: item ? item.implicitHeight : 0`,
        // the Loader collapses to zero because Layout sizing on a
        // bare Loader doesn't propagate the child's implicitHeight.
        Loader {
            id: topLoader

            Layout.fillWidth: true
            Layout.preferredHeight: item ? item.implicitHeight : 0
            active: root.topContent !== null
            visible: active
            sourceComponent: root.topContent
            // The loaded item's parent is the Loader; bind its width
            // to the Loader's width so child layouts can stretch.
            onLoaded: {
                if (item)
                    item.width = Qt.binding(() => {
                    return topLoader.width;
                });

            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: topLoader.active
        }

        // Header: icon + name + version
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                visible: root.appIcon !== ""
                source: root.appIcon
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    visible: root.appName !== ""
                    text: root.appName
                    level: 1
                }

                QQC2.Label {
                    visible: root.appVersion !== ""
                    text: root.appVersion
                    opacity: 0.7
                }

            }

        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.description !== ""
            wrapMode: Text.WordWrap
            text: root.description
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.copyright !== "" || root.license !== "" || root.homepageUrl !== ""
        }

        QQC2.Label {
            visible: root.copyright !== ""
            text: root.copyright
            opacity: 0.7
        }

        QQC2.Label {
            visible: root.license !== ""
            text: root.license
            opacity: 0.7
        }

        Kirigami.UrlButton {
            visible: root.homepageUrl !== ""
            url: root.homepageUrl
            text: root.homepageUrl
        }

        // Injection slot — apps drop extra cards/buttons here.
        ColumnLayout {
            id: extraColumn

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
        }

        Item {
            Layout.fillHeight: true
        }

    }

}
