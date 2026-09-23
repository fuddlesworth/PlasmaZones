// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "LoaderHelpers.js" as PhosphorLoaderHelpers

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
 *       appName: "phosphor-control-minimal"
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

    // Page title — defaults to "About <appName>" when appName is set,
    // else plain "About". Consumers can override directly if a fully
    // custom title is desired.
    title: appName ? qsTr("About %1").arg(appName) : qsTr("About")

    ColumnLayout {
        // Width binding pattern. `root.width - root.leftPadding -
        // root.rightPadding` looked correct but fought
        // Kirigami.ScrollablePage's Flickable contentItem sizing: the
        // Flickable's contentWidth would inflate to children's
        // implicitWidth (long description / credit labels), while the
        // explicit width binding stayed at the smaller viewport math
        // — Layout.fillWidth children then sized to the inflated
        // implicit width and overflowed the clip boundary. Use the
        // legacy SettingsFlickable pattern instead: bind to
        // `parent.width - spacing*2` and centre horizontally so the
        // column is always strictly narrower than the viewport.
        // `parent` is null for a one-event-loop window during
        // ScrollablePage instantiation — guard so the binding doesn't
        // compute NaN and cascade through children's Layout.fillWidth.
        width: parent ? parent.width - Kirigami.Units.largeSpacing * 2 : 0
        anchors.horizontalCenter: parent.horizontalCenter
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
            // Helper documents the consumer-Layout.fillWidth override
            // caveat in one place.
            onLoaded: PhosphorLoaderHelpers.bindItemWidthToLoader(topLoader)
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
            // Layout.preferredWidth: 0 tells the Layout to ignore the
            // Label's implicitWidth (which a WordWrap label sets to its
            // full unwrapped text length — potentially MUCH wider than
            // the viewport). Without this the ColumnLayout's implicit
            // width inflates to the label's implicit, and other
            // Layout.fillWidth siblings (the topContent Loader, etc.)
            // size to the inflated width, overflowing the clip.
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            visible: root.description !== ""
            wrapMode: Text.WordWrap
            text: root.description
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.copyright !== "" || root.license !== "" || root.homepageUrl !== ""
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            visible: root.copyright !== ""
            text: root.copyright
            opacity: 0.7
            wrapMode: Text.WordWrap
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            visible: root.license !== ""
            text: root.license
            opacity: 0.7
            wrapMode: Text.WordWrap
        }

        Kirigami.UrlButton {
            // Restrict to http(s) / mailto schemes to keep the
            // consumer-supplied URL safe: this shell is exposed to
            // third-party apps that drive `homepageUrl` via Q_PROPERTY,
            // so a `javascript:` or `file:///etc/passwd` value would
            // otherwise be opened verbatim by Qt.openUrlExternally().
            //
            // Schemes plus a host-component length check: a stripped
            // value like `http://` or `https:///path` has scheme but
            // no host and Qt.openUrlExternally on it is at best a
            // no-op, at worst a confusing toast. We treat http(s)
            // values without at least one host character as
            // unsafe-ish — there's nothing useful to show the user.
            // mailto:foo@bar gets the same minimum-length treatment
            // (an empty mailto opens a blank composer).
            // Trim leading/trailing whitespace once so the safety check and
            // the rendered URL agree — a consumer who copies a value out of a
            // browser address bar often pastes ` https://example.com` with a
            // stray leading space, which would otherwise fail the prefix
            // check and silently hide the button.
            readonly property string _trimmedUrl: root.homepageUrl.trim()
            readonly property bool _safeUrl: {
                const u = _trimmedUrl;
                if (u === "")
                    return false;
                const lower = u.toLowerCase();
                if (lower.startsWith("http://")) {
                    // 7 = length of "http://" prefix; require at
                    // least one character beyond it.
                    return lower.length > 7;
                }
                if (lower.startsWith("https://")) {
                    // 8 = length of "https://".
                    return lower.length > 8;
                }
                if (lower.startsWith("mailto:")) {
                    // 7 = length of "mailto:".
                    return lower.length > 7;
                }
                return false;
            }
            visible: _safeUrl
            url: _trimmedUrl
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
