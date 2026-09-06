// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/*
 * Browsable release history. The bundled whatsnew.json carries every release
 * PlasmaZones has ever shipped (158 of them, 591 highlights), so this is a
 * two-pane browser rather than one scrolling list: a rail of version series on
 * the left and a reading pane on the right.
 *
 * The pane shows one of three views. A single release; the digest of
 * everything since the user's last version, grouped by kind; or, whenever a
 * search or kind filter is active, every matching highlight across the whole
 * history under a heading per release. That last one is why filtering pins
 * "All matches" to the top of the rail: a query is a question about the
 * history, not about whichever release happened to be selected when it was
 * typed.
 *
 * Both panes are ListViews over flat row arrays, so the delegate count stays
 * proportional to what is on screen even when a bare "Fixed" filter matches
 * 305 highlights.
 */
Kirigami.Dialog {
    id: root

    readonly property color subtleBg: Kirigami.Theme.alternateBackgroundColor
    readonly property color subtleBorder: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    readonly property int thinBorder: 1
    // De-emphasis levels for secondary text. Named so the three shades stay
    // consistent across the rail, the pane and the badges.
    readonly property real mutedOpacity: 0.6
    readonly property real countOpacity: 0.5
    readonly property real bulletOpacity: 0.4

    readonly property var entries: settingsController.whatsNewEntries
    readonly property int unseenCount: settingsController.unseenWhatsNewReleaseCount
    readonly property string baselineVersion: settingsController.whatsNewBaselineVersion

    // Live filter state. `query` matches highlight text and version strings,
    // `kindFilter` is "" (all) or one of the three highlight kinds.
    property string query: ""
    property string kindFilter: ""

    // What the reading pane shows: an index into `entries` for a single
    // release, or one of the two cross-release views below.
    readonly property int digestIndex: -1
    readonly property int resultsIndex: -2
    property int selectedIndex: root.digestIndex
    // Series names the rail has expanded. Seeded in reset() with the newest.
    // Always replaced wholesale, never mutated in place: a push() into the
    // held array emits no change and the rail would keep the stale expansion.
    // Stays `var` rather than `list<string>` because the copy-then-assign
    // idiom below relies on plain JS array semantics.
    property var expandedSeries: []

    readonly property bool filtering: root.query !== "" || root.kindFilter !== ""

    // One sweep of the history per filter change, shared by the rail, the
    // reading pane and every match count. Each of those used to sweep for
    // itself, which was four passes over 591 highlights per keystroke.
    // This has to stay a declarative binding: recomputing it from the
    // onTextChanged handler instead would leave the two ListView models with
    // no dependency on the filter at all, and neither would ever rebuild.
    readonly property var filterResult: {
        const q = root.query.toLowerCase();
        const kind = root.kindFilter;
        let perRelease = [];
        let total = 0;
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            // A query that names a version keeps that whole release, so
            // searching "3.4.9" reads as "show me that release".
            const versionMatches = q !== "" && release.version.toLowerCase().indexOf(q) !== -1;
            let hits = [];
            for (let j = 0; j < release.highlights.length; ++j) {
                const h = release.highlights[j];
                if (kind !== "" && h.kind !== kind)
                    continue;
                if (q !== "" && !versionMatches && h.text.toLowerCase().indexOf(q) === -1)
                    continue;
                hits.push(h);
            }
            perRelease.push(hits);
            total += hits.length;
        }
        return {
            "perRelease": perRelease,
            "total": total
        };
    }

    title: i18nc("@title:window the browsable release history", "What's New")
    preferredWidth: Kirigami.Units.gridUnit * 46
    // Use maximumHeight instead of preferredHeight to avoid Kirigami.Dialog
    // binding loop on "y" (its overlay centering feeds back into itself).
    maximumHeight: Kirigami.Units.gridUnit * 32
    standardButtons: Dialog.NoButton
    padding: Kirigami.Units.largeSpacing

    onOpened: {
        root.resetView();
        settingsController.markWhatsNewSeen();
        // Somewhere to type and somewhere to tab from. Kirigami.Dialog never
        // makes its popup an active focus scope, so a plain `focus: true`
        // inside it marks an intent that is never acted on and the dialog
        // opens with no focused control at all.
        Qt.callLater(function () {
            searchField.forceActiveFocus();
        });
    }

    // ── Model helpers ───────────────────────────────────────────────
    // All of these are plain JS over the CONSTANT `entries` list, recomputed
    // whenever the filter or expansion state changes. The rail delegates do
    // bind against `expandedSeries` and `selectedIndex` per row on top of it.

    function resetView() {
        // Clear the widget, not just the mirror. The dialog is a single
        // instance kept alive for the process (Main.qml), so a query left in
        // the field on close would still be showing on the next open while
        // the panes behind it had been reset to unfiltered.
        searchField.text = "";
        root.query = "";
        root.kindFilter = "";
        root.selectedIndex = root.unseenCount > 0 ? root.digestIndex : 0;
        root.expandedSeries = root.entries.length > 0 ? [root.entries[0].series] : [];
    }

    /// The highlights of the release at `index` that pass the current filter.
    function filteredHighlights(index) {
        return root.filterResult.perRelease[index];
    }

    /// Rail rows, as a flat array so one ListView renders headers and
    /// releases together. The series structure survives filtering: a series
    /// with matches force-expands and one without drops out entirely, so the
    /// user keeps their bearings instead of reading a bare list of numbers.
    function railRows() {
        const filtering = root.filtering;
        let rows = [];
        // While filtering, the pinned row is the cross-release result set;
        // the digest is itself a "since version X" slice and stacking the two
        // reads as two competing answers to the same question.
        if (filtering)
            rows.push({
                "type": "results",
                "hits": root.filterResult.total
            });
        else if (root.unseenCount > 0)
            rows.push({
                "type": "digest"
            });
        // Group first, so a series header can carry the count of what
        // actually survives the filter underneath it.
        let series = [];
        let bySeries = {};
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            const hits = root.filteredHighlights(i).length;
            if (filtering && hits === 0)
                continue;
            if (bySeries[release.series] === undefined) {
                bySeries[release.series] = [];
                series.push(release.series);
            }
            bySeries[release.series].push({
                "type": "release",
                "index": i,
                "release": release,
                "hits": hits
            });
        }
        for (let s = 0; s < series.length; ++s) {
            const name = series[s];
            rows.push({
                "type": "series",
                "series": name,
                "count": bySeries[name].length
            });
            if (filtering || root.expandedSeries.indexOf(name) !== -1)
                rows = rows.concat(bySeries[name]);
        }
        return rows;
    }

    function toggleSeries(series) {
        let next = root.expandedSeries.slice();
        const at = next.indexOf(series);
        if (at === -1) {
            next.push(series);
        } else {
            // Collapsing the series the reading pane is showing would leave
            // the selection with no row in the rail, so hand it back to a
            // view the user can still see.
            if (root.selectedIndex >= 0 && root.entries[root.selectedIndex].series === series)
                root.selectedIndex = root.unseenCount > 0 ? root.digestIndex : 0;
            next.splice(at, 1);
        }
        root.expandedSeries = next;
    }

    /// Select one release and guarantee it is reachable in the rail. Picking
    /// a release from the results view has to expand its series too: once the
    /// filter is cleared the rail stops force-expanding, and the selection
    /// would otherwise sit inside a collapsed series showing nothing.
    function selectRelease(index) {
        root.selectedIndex = index;
        const series = root.entries[index].series;
        if (root.expandedSeries.indexOf(series) === -1)
            root.expandedSeries = root.expandedSeries.concat([series]);
    }

    /// Every highlight from the releases the user has not seen yet, grouped by
    /// kind. This is the "what changed while I was away" view, so it reads by
    /// kind rather than by version.
    function digestGroups() {
        // The "" bucket collects a highlight the loader could not classify.
        // The schema keeps those out of the bundled file, but without the
        // bucket such a line would be dropped here while still rendering in
        // the single-release view, which is the one place the two views
        // would disagree about what the release contains.
        let byKind = {
            "new": [],
            "changed": [],
            "fixed": [],
            "": []
        };
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            if (!release.unseen)
                continue;
            const hs = root.filteredHighlights(i);
            for (let j = 0; j < hs.length; ++j)
                if (byKind[hs[j].kind] !== undefined)
                    byKind[hs[j].kind].push(hs[j].text);
        }
        let groups = [];
        const order = ["new", "changed", "fixed", ""];
        for (let k = 0; k < order.length; ++k)
            if (byKind[order[k]].length > 0)
                groups.push({
                    "kind": order[k],
                    "items": byKind[order[k]]
                });
        return groups;
    }

    /// The reading pane's rows, flattened for the ListView. All three views
    /// share one row vocabulary: title, kind, release, item, empty.
    function paneRows() {
        if (root.selectedIndex === root.resultsIndex)
            return root.resultRows();
        if (root.selectedIndex === root.digestIndex)
            return root.digestRows();
        return root.releaseRows(root.selectedIndex);
    }

    /// Every matching highlight in the whole file, under a heading per
    /// release. This is what a search or a kind chip lands on, so a query is
    /// answered across the history rather than inside one release.
    function resultRows() {
        let body = [];
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            const hs = root.filteredHighlights(i);
            if (hs.length === 0)
                continue;
            body.push({
                "type": "release",
                "index": i,
                "version": release.version,
                "date": release.date
            });
            for (let j = 0; j < hs.length; ++j)
                body.push({
                    "type": "item",
                    "kind": hs[j].kind,
                    "text": hs[j].text
                });
        }
        let rows = [
            {
                "type": "title",
                "text": i18nc("@title the reading pane is showing search results", "All matches"),
                "sub": i18np("%n highlight", "%n highlights", root.filterResult.total)
            }
        ];
        if (body.length === 0)
            rows.push({
                "type": "empty",
                "text": root.emptyFilterMessage()
            });
        return rows.concat(body);
    }

    /// Everything the user missed, grouped by kind rather than by version.
    function digestRows() {
        let rows = [
            {
                "type": "title",
                "text": i18n("Since %1", root.baselineVersion),
                "sub": i18np("%n release", "%n releases", root.unseenCount)
            }
        ];
        const groups = root.digestGroups();
        if (groups.length === 0)
            rows.push({
                "type": "empty",
                "text": i18n("Nothing to show for these releases.")
            });
        for (let g = 0; g < groups.length; ++g) {
            rows.push({
                "type": "kind",
                "kind": groups[g].kind
            });
            for (let i = 0; i < groups[g].items.length; ++i)
                rows.push({
                    "type": "item",
                    "kind": "",
                    "text": groups[g].items[i]
                });
        }
        return rows;
    }

    function releaseRows(index) {
        // No release to show at all. The loader leaves the list empty when
        // whatsnew.json is missing or fails validation, and an unexplained
        // blank pane looks identical to a release with nothing in it.
        if (index < 0 || index >= root.entries.length)
            return [
                {
                    "type": "empty",
                    "text": i18n("The release history could not be loaded.")
                }
            ];
        const release = root.entries[index];
        let rows = [
            {
                "type": "title",
                "text": release.version,
                "sub": release.date
            }
        ];
        const hs = root.filteredHighlights(index);
        for (let i = 0; i < hs.length; ++i)
            rows.push({
                "type": "item",
                "kind": hs[i].kind,
                "text": hs[i].text
            });
        return rows;
    }

    /// What clicking, or pressing Return on, a rail row does. Shared so the
    /// keyboard and the mouse cannot drift apart.
    function activateRailRow(row) {
        if (row === undefined)
            return;
        if (row.type === "series")
            root.toggleSeries(row.series);
        else if (row.type === "digest")
            root.selectedIndex = root.digestIndex;
        else if (row.type === "results")
            root.selectedIndex = root.resultsIndex;
        else
            root.selectRelease(row.index);
    }

    /// Why the current filter came back empty. Each arm is a whole sentence
    /// rather than a kind name pasted into a frame, so translators get a
    /// string they can inflect.
    function emptyFilterMessage() {
        if (root.query === "")
            return i18n("No release has highlights of this kind.");
        if (root.kindFilter === "new")
            return i18n("No new feature matches “%1”.", root.query);
        if (root.kindFilter === "changed")
            return i18n("No change matches “%1”.", root.query);
        if (root.kindFilter === "fixed")
            return i18n("No fix matches “%1”.", root.query);
        return i18n("Nothing matches “%1”.", root.query);
    }

    function kindLabel(kind) {
        if (kind === "new")
            return i18nc("@label a release highlight that adds a feature", "New");
        if (kind === "changed")
            return i18nc("@label a release highlight that changes existing behaviour", "Changed");
        if (kind === "fixed")
            return i18nc("@label a release highlight that fixes a bug", "Fixed");
        return "";
    }

    function kindColor(kind) {
        if (kind === "new")
            return Kirigami.Theme.positiveTextColor;
        if (kind === "changed")
            return Kirigami.Theme.neutralTextColor;
        if (kind === "fixed")
            return Kirigami.Theme.linkColor;
        return Kirigami.Theme.disabledTextColor;
    }

    // ── Reusable pieces ─────────────────────────────────────────────

    component KindBadge: Rectangle {
        id: badge

        required property string kind

        readonly property color tint: root.kindColor(badge.kind)

        implicitWidth: badgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
        implicitHeight: badgeLabel.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing * 1.5
        visible: badge.kind !== ""
        color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, badge.tint, 0.18)
        border.width: root.thinBorder
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, badge.tint, 0.45)

        Label {
            id: badgeLabel

            anchors.centerIn: parent
            text: root.kindLabel(badge.kind)
            color: badge.tint
            font: Kirigami.Theme.smallFont
        }
    }

    component HighlightRow: RowLayout {
        id: highlight

        required property string kind
        required property string text

        spacing: Kirigami.Units.smallSpacing

        KindBadge {
            kind: highlight.kind
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.round(Kirigami.Units.smallSpacing / 2)
        }

        Label {
            visible: highlight.kind === ""
            text: "\u2022"
            opacity: root.bulletOpacity
            Layout.alignment: Qt.AlignTop
            Layout.leftMargin: Kirigami.Units.smallSpacing
        }

        Label {
            Layout.fillWidth: true
            text: highlight.text
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }

    // ── Layout ──────────────────────────────────────────────────────

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Filter bar: free-text search plus the three kind chips.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                focus: true
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                placeholderText: i18n("Search releases…")
                Accessible.name: i18n("Search release highlights")
                onTextChanged: {
                    root.query = text;
                    // A query rebuilds the rail around matches, so the old
                    // selection index would point at an unlisted release.
                    root.selectSensibleRow();
                }
            }

            Repeater {
                model: [
                    {
                        "kind": "",
                        "label": i18nc("@option:radio show every kind of release highlight", "All"),
                        "a11y": i18n("Show every release highlight")
                    },
                    {
                        "kind": "new",
                        "label": root.kindLabel("new"),
                        "a11y": i18n("Show only new features")
                    },
                    {
                        "kind": "changed",
                        "label": root.kindLabel("changed"),
                        "a11y": i18n("Show only changed behaviour")
                    },
                    {
                        "kind": "fixed",
                        "label": root.kindLabel("fixed"),
                        "a11y": i18n("Show only bug fixes")
                    }
                ]

                Button {
                    required property var modelData

                    text: modelData.label
                    checkable: true
                    // The four chips are one choice, so clicking the active
                    // one must not untick it. Without this the click toggles
                    // `checked` off while `kindFilter` keeps its value, and
                    // since the assignment below changes nothing the binding
                    // never re-evaluates to put it back.
                    autoExclusive: true
                    checked: root.kindFilter === modelData.kind
                    Accessible.name: modelData.a11y
                    onClicked: {
                        root.kindFilter = modelData.kind;
                        root.selectSensibleRow();
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Fixed height. The reading pane's content height varies by an order
        // of magnitude between a one-line release and the full digest, and a
        // dialog that resizes under the user as they filter is unusable, so
        // the split gets a definite height and scrolls inside it.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 24
            spacing: Kirigami.Units.largeSpacing

            // ── Version rail ────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                // Without a floor the rail is a bare Rectangle with no
                // implicit width, so a narrow dialog shrinks it until the
                // elided version labels show nothing at all.
                Layout.minimumWidth: Kirigami.Units.gridUnit * 8
                Layout.fillHeight: true
                radius: Kirigami.Units.smallSpacing * 1.5
                color: root.subtleBg
                border.width: root.thinBorder
                border.color: root.subtleBorder

                ListView {
                    id: rail

                    anchors.fill: parent
                    anchors.margins: root.thinBorder
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    model: root.railRows()

                    // Arrow keys move `currentIndex`, so the rail needs a
                    // visible cursor for that to mean anything, and Return has
                    // to do what a click does or the keyboard can reach a row
                    // it cannot open. Focus starts in the search field, which
                    // is what the dialog is for, and Tab hands it to the rail.
                    activeFocusOnTab: true
                    highlightMoveDuration: Kirigami.Units.shortDuration
                    highlight: Rectangle {
                        color: Kirigami.Theme.highlightColor
                        opacity: root.bulletOpacity
                        radius: Kirigami.Units.smallSpacing
                    }
                    Keys.onReturnPressed: root.activateRailRow(rail.model[rail.currentIndex])
                    Keys.onEnterPressed: root.activateRailRow(rail.model[rail.currentIndex])

                    ScrollBar.vertical: ScrollBar {
                        id: railScrollBar
                    }

                    // One delegate covers all three row kinds. A Loader per
                    // row would be tidier to read, but a Loader does not
                    // inject its own properties into the component it loads,
                    // so each variant would need its own context object for
                    // no visual gain.
                    delegate: ItemDelegate {
                        id: railRow

                        required property var modelData
                        required property int index

                        readonly property bool isDigest: railRow.modelData.type === "digest"
                        readonly property bool isResults: railRow.modelData.type === "results"
                        readonly property bool isSeries: railRow.modelData.type === "series"
                        // The two cross-release views share the pinned slot at
                        // the top of the rail; only one is ever present.
                        readonly property bool isPinned: railRow.isDigest || railRow.isResults
                        readonly property bool seriesExpanded: railRow.isSeries && root.expandedSeries.indexOf(railRow.modelData.series) !== -1
                        readonly property bool filtering: root.filtering

                        width: rail.width
                        leftPadding: railRow.modelData.type === "release" ? Kirigami.Units.largeSpacing * 2 : Kirigami.Units.largeSpacing
                        // Keep the trailing count clear of the overlay
                        // scrollbar, which otherwise sits on top of it.
                        rightPadding: Kirigami.Units.largeSpacing + railScrollBar.width
                        highlighted: {
                            if (railRow.isDigest)
                                return root.selectedIndex === root.digestIndex;
                            if (railRow.isResults)
                                return root.selectedIndex === root.resultsIndex;
                            return !railRow.isSeries && root.selectedIndex === railRow.modelData.index;
                        }

                        Accessible.name: {
                            if (railRow.isDigest)
                                return i18n("Everything since version %1", root.baselineVersion);
                            if (railRow.isResults)
                                return i18n("Every matching highlight");
                            if (railRow.isSeries)
                                return railRow.seriesExpanded ? i18n("Collapse the %1 releases", railRow.modelData.series) : i18n("Expand the %1 releases", railRow.modelData.series);
                            return i18n("Version %1, released %2", railRow.modelData.release.version, railRow.modelData.release.date);
                        }

                        // Move the keyboard cursor to the clicked row too, or
                        // the highlight rectangle stays behind on whatever
                        // row it was on and the next arrow key jumps from
                        // there instead of from what the user just picked.
                        onClicked: {
                            rail.currentIndex = railRow.index;
                            root.activateRailRow(railRow.modelData);
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                visible: railRow.isSeries
                                source: railRow.seriesExpanded ? "go-down-symbolic" : "go-next-symbolic"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }

                            // Unseen marker, and the digest's own bullet.
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                visible: railRow.isPinned || (!railRow.isSeries && railRow.modelData.release.unseen)
                                implicitWidth: Kirigami.Units.smallSpacing
                                implicitHeight: Kirigami.Units.smallSpacing
                                radius: width / 2
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.Heading {
                                visible: railRow.isSeries
                                Layout.fillWidth: railRow.isSeries
                                level: 4
                                text: railRow.isSeries ? railRow.modelData.series : ""
                            }

                            Label {
                                visible: !railRow.isSeries
                                Layout.fillWidth: !railRow.isSeries
                                elide: Text.ElideRight
                                text: {
                                    if (railRow.isDigest)
                                        return i18n("Since %1", root.baselineVersion);
                                    if (railRow.isResults)
                                        return i18nc("@item the rail row that shows every match", "All matches");
                                    return railRow.isSeries ? "" : railRow.modelData.release.version;
                                }
                            }

                            // Release count on a series row, match count on
                            // the results row and on each release while a
                            // filter is narrowing it.
                            Label {
                                visible: railRow.isSeries || railRow.isResults || (!railRow.isDigest && railRow.filtering)
                                Layout.rightMargin: Kirigami.Units.smallSpacing
                                opacity: root.countOpacity
                                font: Kirigami.Theme.smallFont
                                text: {
                                    if (railRow.isSeries)
                                        return railRow.modelData.count;
                                    if (railRow.isResults)
                                        return railRow.modelData.hits;
                                    return railRow.isDigest ? "" : railRow.modelData.hits;
                                }
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: root.mutedOpacity
                    // The rail only empties when there is no history at all:
                    // a filter always keeps its own pinned "All matches" row,
                    // so a no-match message here could never be reached. That
                    // message belongs in the reading pane, where it is.
                    visible: rail.count === 0
                    text: i18n("The release history could not be loaded.")
                }
            }

            // ── Reading pane ────────────────────────────────────────
            // A ListView rather than a Column in a ScrollView: the results
            // view can run to every highlight in the file (a bare "Fixed"
            // filter is 305 of them across 158 releases), and those rows have
            // to be virtualised rather than all instantiated at once.
            ListView {
                id: readingPane

                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 12
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: Kirigami.Units.smallSpacing
                model: root.paneRows()

                ScrollBar.vertical: ScrollBar {
                    id: paneScrollBar
                }

                // Selection and filter changes rebuild the model; the old
                // scroll offset means nothing against new content, so every
                // view opens at its top. Deferred, because items for the new
                // model have not been created yet at the point the handler
                // runs.
                onModelChanged: Qt.callLater(readingPane.positionViewAtBeginning)

                delegate: Column {
                    id: paneRow

                    required property var modelData

                    readonly property real rowWidth: readingPane.width - (paneScrollBar.visible ? paneScrollBar.width : 0) - Kirigami.Units.smallSpacing

                    width: readingPane.width
                    spacing: 0

                    // Pane title: which view this is.
                    ColumnLayout {
                        visible: paneRow.modelData.type === "title"
                        width: paneRow.rowWidth
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 2
                                text: paneRow.modelData.text || ""
                                textFormat: Text.PlainText
                            }

                            Label {
                                Layout.alignment: Qt.AlignBaseline
                                opacity: root.countOpacity
                                font: Kirigami.Theme.smallFont
                                text: paneRow.modelData.sub || ""
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.smallSpacing
                        }
                    }

                    // Kind heading for a digest group. Setting `visible` on
                    // the instance overrides the component's own empty-kind
                    // guard, so the unclassified group is excluded here and
                    // gets the plain heading below instead. Every digest item
                    // renders as a bare bullet, so a group with no heading at
                    // all would read as part of the group above it.
                    KindBadge {
                        visible: paneRow.modelData.type === "kind" && paneRow.modelData.kind !== ""
                        kind: paneRow.modelData.type === "kind" ? paneRow.modelData.kind : ""
                    }

                    Label {
                        visible: paneRow.modelData.type === "kind" && paneRow.modelData.kind === ""
                        text: i18nc("@title a digest group of highlights with no New, Changed or Fixed kind", "Other")
                        textFormat: Text.PlainText
                        opacity: root.mutedOpacity
                        font: Kirigami.Theme.smallFont
                    }

                    // Release heading in the results view. Clicking it drops
                    // the results down to that one release.
                    ItemDelegate {
                        visible: paneRow.modelData.type === "release"
                        width: paneRow.rowWidth
                        horizontalPadding: 0
                        Accessible.name: paneRow.modelData.type === "release" ? i18n("Show only version %1", paneRow.modelData.version) : ""
                        onClicked: root.selectRelease(paneRow.modelData.index)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 4
                                text: paneRow.modelData.type === "release" ? paneRow.modelData.version : ""
                            }

                            Label {
                                Layout.alignment: Qt.AlignBaseline
                                opacity: root.countOpacity
                                font: Kirigami.Theme.smallFont
                                text: paneRow.modelData.type === "release" ? paneRow.modelData.date : ""
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }

                    HighlightRow {
                        visible: paneRow.modelData.type === "item"
                        width: paneRow.rowWidth
                        kind: paneRow.modelData.kind || ""
                        text: paneRow.modelData.text || ""
                    }

                    Label {
                        visible: paneRow.modelData.type === "empty"
                        width: paneRow.rowWidth
                        wrapMode: Text.WordWrap
                        opacity: root.mutedOpacity
                        text: paneRow.modelData.text || ""
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }

    // Where the selection goes when the filter state changes. Typing a query
    // is a question about the whole history, so it lands on the results view
    // rather than leaving the user on one release wondering why the other
    // matches the rail is counting are nowhere to be seen. Clearing the
    // filter hands them back the digest, or the newest release.
    function selectSensibleRow() {
        if (root.query !== "" || root.kindFilter !== "") {
            root.selectedIndex = root.resultsIndex;
            return;
        }
        if (root.selectedIndex === root.resultsIndex)
            root.selectedIndex = root.unseenCount > 0 ? root.digestIndex : 0;
    }
}
