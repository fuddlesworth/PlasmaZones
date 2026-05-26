// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dialog for picking from currently running windows
 *
 * Shows a filterable list of running windows with their window class and caption.
 * Emits picked(value) when a window is selected. Used by the Exclusions tab
 * and App Rules card to let users pick window classes without needing
 * to manually type them.
 */
Kirigami.Dialog {
    id: dialog

    // The SettingsController — supplies cachedRunningWindows() /
    // requestRunningWindows(). Named `controller` (not `appSettings`) because
    // it is the controller, not a Settings object.
    required property var controller
    /// Picker mode — `"apps"` returns the app's short / desktop-file name,
    /// `"classes"` the X11/Wayland window class, `"desktopFiles"` the full
    /// `.desktop` basename (filtered to rows that have one), `"titles"`
    /// the window caption. Drives the title, the row's primary text, and
    /// the value passed to `picked`.
    property string mode: "apps"
    /// Convenience boolean read by older callers (ExclusionsPage,
    /// AnimationsGeneralPage) inside their `onPicked` handlers to branch
    /// between the "application" and "window class" lists. Derived from
    /// `mode` so the open* helpers don't have to keep two properties in
    /// sync; new callers should consult `mode` directly.
    readonly property bool forApps: mode === "apps"
    property var windowList: []
    // Set by the Connections block below when the controller signals a
    // timeout (KWin effect unloaded / unresponsive). Cleared on every
    // successful reply. Drives the placeholder-message error state.
    property bool requestTimedOut: false

    signal picked(string value)

    // Derive a short app name from windowClass when appName is not available
    // X11: "resourceName resourceClass" → first part (e.g., "dolphin")
    // Wayland app_id: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
    function deriveAppName(windowClass) {
        if (!windowClass || windowClass.length === 0)
            return "";

        let spaceIdx = windowClass.indexOf(' ');
        if (spaceIdx > 0)
            return windowClass.substring(0, spaceIdx);

        let dotIdx = windowClass.lastIndexOf('.');
        if (dotIdx >= 0 && dotIdx < windowClass.length - 1)
            return windowClass.substring(dotIdx + 1);

        return windowClass;
    }

    function openForApps() {
        mode = "apps";
        refresh();
        open();
        searchField.forceActiveFocus();
    }

    function openForClasses() {
        mode = "classes";
        refresh();
        open();
        searchField.forceActiveFocus();
    }

    function openForDesktopFiles() {
        mode = "desktopFiles";
        refresh();
        open();
        searchField.forceActiveFocus();
    }

    function openForTitles() {
        mode = "titles";
        refresh();
        open();
        searchField.forceActiveFocus();
    }

    /// User-facing title for the current mode. Extracted from the quadruple-
    /// nested ternary so the dialog header reads as a function call instead
    /// of a wall of `?:`.
    function titleForMode() {
        if (mode === "classes")
            return i18n("Pick Window Class from Running Windows");

        if (mode === "desktopFiles")
            return i18n("Pick Desktop File from Running Windows");

        if (mode === "titles")
            return i18n("Pick Window Title from Running Windows");

        return i18n("Pick Application from Running Windows");
    }

    /// Extract the picker value for @p row according to the current mode.
    /// Centralised here so the title, the filter predicate, the delegate,
    /// and the `picked` emit all stay in sync.
    function primaryFor(row) {
        if (!row)
            return "";

        if (mode === "classes")
            return row.windowClass || "";

        if (mode === "desktopFiles")
            return row.desktopFile || "";

        if (mode === "titles")
            return row.caption || "";

        return row.appName && row.appName.length > 0 ? row.appName : deriveAppName(row.windowClass);
    }

    // Async refresh: show whatever we already have cached from a previous
    // fetch so the list paints instantly, then kick off a fresh request.
    // The `Connections` block below replaces `windowList` when the
    // daemon signals runningWindowsAvailable — the dialog never blocks
    // on the D-Bus call even if the KWin effect is unloaded or slow.
    // A client-side timeout in SettingsController surfaces
    // runningWindowsTimedOut() if no reply arrives, which flips
    // requestTimedOut so the placeholder can switch to an error state.
    function refresh() {
        searchField.text = "";
        requestTimedOut = false;
        windowList = controller.cachedRunningWindows();
        controller.requestRunningWindows();
    }

    title: titleForMode()
    preferredWidth: Kirigami.Units.gridUnit * 25
    preferredHeight: Kirigami.Units.gridUnit * 20
    customFooterActions: [
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            onTriggered: dialog.refresh()
        }
    ]

    Connections {
        function onRunningWindowsAvailable(windows) {
            dialog.requestTimedOut = false;
            dialog.windowList = windows;
        }

        function onRunningWindowsTimedOut() {
            dialog.requestTimedOut = true;
        }

        // Declare `target` BEFORE the signal-handler functions — strict
        // QML resolves signal names against the target's metaobject at
        // parse time, so a target listed after the handlers fails the
        // lookup ("Detected function … but no signal of the target
        // matches the name").
        target: dialog.controller
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.SearchField {
            id: searchField

            Layout.fillWidth: true
            placeholderText: i18n("Filter...")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        ListView {
            id: windowListView

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            clip: true
            model: {
                if (!dialog.windowList || dialog.windowList.length === 0)
                    return [];

                let base = dialog.windowList;
                // Titles mode still prunes captionless rows — clicking one
                // would fill the leaf with an empty string and there is no
                // sensible fallback. DesktopFile mode does NOT prune: many
                // X11 apps have no registered .desktop file, and older
                // KWin-effect builds don't populate the field at all, so
                // an aggressive prune produces an empty picker. The
                // delegate flags rows that would yield an empty value
                // (greyed primary text + "(no desktop file)" subtext) so
                // the user can still see and search the running windows.
                if (dialog.mode === "titles")
                    base = base.filter(function(w) {
                        return (w.caption || "").length > 0;
                    });

                let filter = searchField.text.toLowerCase();
                if (filter.length === 0)
                    return base;

                return base.filter(function(w) {
                    let primary = dialog.primaryFor(w);
                    if (primary.length === 0)
                        primary = w.appName || dialog.deriveAppName(w.windowClass);

                    return primary.toLowerCase().includes(filter) || (w.caption || "").toLowerCase().includes(filter);
                });
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: windowListView.count === 0
                text: dialog.requestTimedOut ? i18n("No response from KWin effect") : dialog.windowList.length === 0 ? i18n("No windows found") : i18n("No matching windows")
                explanation: dialog.requestTimedOut ? i18n("The KWin effect did not respond. Make sure the PlasmaZones daemon is running, then click Refresh.") : dialog.windowList.length === 0 ? i18n("Make sure the PlasmaZones daemon and KWin effect are running") : i18n("Try a different search term")
            }

            delegate: ItemDelegate {
                // Strict QML requires the delegate's context property to be
                // declared before it's read in bindings.
                required property var modelData
                readonly property string rawValue: dialog.primaryFor(modelData)
                readonly property bool valueAvailable: rawValue.length > 0
                // For empty-value rows (e.g. a desktop-file pick where the
                // window has no registered .desktop), fall back to the
                // app name so the user can still tell which window the
                // row represents — but disable selection so they don't
                // commit an empty leaf value.
                readonly property string primaryText: valueAvailable ? rawValue : (modelData.appName && modelData.appName.length > 0 ? modelData.appName : dialog.deriveAppName(modelData.windowClass))
                // Subtext gives the user some disambiguation context — for
                // a Title pick we show the windowClass so two windows with
                // the same caption are distinguishable; for the other
                // modes we surface the caption. When the value is
                // unavailable we replace it with an explanatory hint.
                readonly property string secondaryText: !valueAvailable ? (dialog.mode === "desktopFiles" ? i18n("(no desktop file)") : dialog.mode === "titles" ? i18n("(no title)") : "") : dialog.mode === "titles" ? (modelData.windowClass || "") : (modelData.caption || "")

                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                enabled: valueAvailable
                opacity: valueAvailable ? 1 : 0.5
                Accessible.name: primaryText + (secondaryText.length > 0 ? " — " + secondaryText : "")
                onClicked: {
                    dialog.picked(rawValue);
                    dialog.close();
                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: dialog.mode === "classes" ? "window" : dialog.mode === "desktopFiles" ? "application-x-desktop" : dialog.mode === "titles" ? "draw-text" : "application-x-executable"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true

                        Label {
                            text: primaryText
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            text: secondaryText
                            Layout.fillWidth: true
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            elide: Text.ElideRight
                            visible: secondaryText.length > 0
                        }

                    }

                }

            }

        }

    }

}
