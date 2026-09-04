// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Shortcut cheatsheet content — Item-rooted body hosted in
 * PassiveOverlayShell's cheatsheetSlot. A centered card listing every global
 * shortcut grouped by category, filtered by the tiling mode and the layout
 * capability of the screen the sheet opened on.
 *
 * The search field EMPHASISES rather than filters: a query dims the rows that
 * do not answer it and leaves them in place, so the card's size and the
 * position of every row are fixed for as long as the sheet is open.
 *
 * Data arrives via the host slot's bindings (C++ pushes `shortcuts`,
 * `currentMode`, `autotileAvailable`, `scrollingAvailable`,
 * `layoutsAvailable` and `layoutsAreTemplates` onto cheatsheetSlot; live mode
 * switches re-push and the group filter re-evaluates reactively).
 *
 * Keyboard: unlike the shell's other slots, this one HOLDS keyboard focus for
 * its lifetime so the search field can be typed into. OverlayService flips the
 * shared surface to Exclusive keyboard interactivity on show and back to None
 * on the first edge of dismissal (shellhost_bridge.cpp, cheatsheet.cpp).
 * Escape still arrives daemon-side through its own ad-hoc global grab rather
 * than through this surface, because KWin routes global shortcuts ahead of
 * surface delivery, so a QML Shortcut here would still never fire. The
 * backdrop MouseArea remains the pointer dismiss path.
 *
 * Rows and category blocks live in CheatsheetRow / CheatsheetGroup; this file
 * owns the filter, the column packing and the card chrome.
 */
Item {
    id: root

    /// Catalog rows from ShortcutManager::cheatsheetModel(): one object per
    /// shortcut with id, label, category, categoryOrder, rowOrder (the
    /// in-category sort position, already applied to the model), triggers (list of
    /// display strings), assigned (bool), mode
    /// ("all"|"snapping"|"autotile"|"scrolling"|"layouts"|"managed"), description (translated
    /// plain-prose explanation for the row tooltip; empty when the action
    /// needs none), and templatesDescription (the same, worded for a screen
    /// that consumes layouts as sizing templates; present only on rows whose
    /// meaning changes there, and falling back to description otherwise). "layouts" is a capability tag rather than a fourth
    /// tiling mode: currentMode can never equal it, and rows carrying it
    /// are gated purely by layoutsAvailable, independent of currentMode.
    property var shortcuts: []
    /// Tiling mode of the screen the sheet opened on:
    /// "snapping" | "autotile" | "scrolling".
    property string currentMode: "snapping"
    /// Global tiling feature gate. Required alongside currentMode because the
    /// mode string is re-derived from the ENGINE's live per-screen set.
    property bool autotileAvailable: true
    /// Global scrolling feature gate, same contract as autotileAvailable.
    /// Required alongside currentMode because the mode string is re-derived
    /// from the ENGINE's live per-screen set, which is torn down by the
    /// consolidated settingsChanged handler AFTER the scrollingEnabledChanged
    /// refresh has already re-pushed the model — gating on the setting keeps
    /// the Scrolling group from surviving its own master switch.
    property bool scrollingAvailable: true
    /// Whether the bound screen's engine has any layout concept
    /// (IPlacementEngine::layoutSupport is not None, pushed by the daemon).
    /// Gates the rows tagged mode === "layouts": scrolling screens now
    /// consume layouts as sizing templates, so their rows show too; only a
    /// capability-less engine answers those shortcuts with a "not
    /// available" OSD, and advertising them there would be noise.
    property bool layoutsAvailable: true
    /// True when the bound screen's engine consumes layouts as sizing
    /// TEMPLATES (a scrolling screen): rows carrying a templatesDescription
    /// swap their tooltip for the template wording, since the same key picks
    /// a template there rather than a placement layout.
    property bool layoutsAreTemplates: false
    property string fontFamily: ""
    property real fontSizeScale: 1

    /// Idempotency latch for `dismissRequested` — same contract as
    /// LayoutPickerContent's: rapid backdrop clicks during the fade-out
    /// window collapse into one dismiss per show cycle. No writer resets
    /// it; the Loader re-instantiates this component on every show.
    property bool _dismissed: false

    /// Catalog id of the one row whose tooltip is latched open by a touch
    /// long-press, or empty. Sheet-level so a second long-press REPLACES the
    /// open tooltip instead of stacking a second one that nothing on a touch
    /// device would close. Keyed on the catalog id rather than on delegate
    /// identity because a live mode switch re-pushes the model and rebuilds
    /// every delegate, and an Item reference would strand the latch on a
    /// destroyed row.
    property string latchedRowId: ""

    // A re-push can drop the latched row's category outright (a live mode
    // switch does exactly that). Its delegate goes with it, and the latch then
    // has no one left to clear it: the writers are a tap on some surviving
    // row, a flick, or that delegate's own tooltip closing. Harmless while it
    // sits there, but it never returns to empty for the rest of the sheet's
    // life, so clear it at the edge that can strand it.
    onShortcutsChanged: root.latchedRowId = ""

    /// Live filter text from the search field.
    property string query: ""

    /// Per-category disclosure state for the unassigned rollup, keyed on
    /// categoryOrder. Replaced wholesale rather than mutated so the bindings
    /// reading it actually re-evaluate.
    ///
    /// This survives a live re-push, which is correct: categoryOrder is a
    /// compile-time sort key on the catalog's category table, so one value
    /// always denotes the same category whatever the mode. A category still
    /// present after a mode switch keeps the disclosure the user left it in,
    /// and one that is gone leaves an entry nothing reads. A show does reset
    /// it, since the Loader re-instantiates this component.
    property var expandedCategories: ({})

    signal dismissRequested

    function _requestDismiss() {
        if (_dismissed)
            return;

        _dismissed = true;
        root.dismissRequested();
    }

    /// Whether a key's text is something a filter field should receive.
    ///
    /// `event.text` is non-empty for far more than typing: Escape carries
    /// \x1b, Return \r and Backspace \b. Treating any non-empty text as
    /// typing would insert control characters into the query and, worse,
    /// swallow Escape — which is the sheet's dismiss key, and must reach the
    /// daemon's global grab rather than being eaten here. Space is printable
    /// and belongs in a multi-term query; a focused disclosure consumes it
    /// first for its own toggle, so the two do not collide.
    function _isPrintable(text) {
        if (text.length === 0)
            return false;
        const code = text.charCodeAt(0);
        return code > 0x1f && code !== 0x7f;
    }

    function _toggleCategory(categoryOrder) {
        let next = {};
        for (const k in root.expandedCategories)
            next[k] = root.expandedCategories[k];
        next[categoryOrder] = !next[categoryOrder];
        root.expandedCategories = next;
    }

    /// Query split into lowercased terms. Every term must appear somewhere in
    /// a row for it to survive, so "col width" finds "Increase Column Width"
    /// without the user having to type the words in order.
    readonly property var queryTerms: {
        const trimmed = root.query.trim();
        if (trimmed.length === 0)
            return [];
        return trimmed.toLocaleLowerCase().split(/\s+/).filter(function (t) {
            return t.length > 0;
        });
    }

    /// True when the given catalog row applies in the current mode.
    function rowVisible(row) {
        if (row.mode === "autotile")
            return root.autotileAvailable && root.currentMode === "autotile";
        if (row.mode === "snapping")
            return root.currentMode === "snapping";
        if (row.mode === "scrolling")
            return root.scrollingAvailable && root.currentMode === "scrolling";
        if (row.mode === "layouts")
            return root.layoutsAvailable;
        // The engine-managed modes, autotile or scrolling: the row does
        // something on either and nothing on snapping, so neither "all" nor a
        // single mode tag would tell the truth.
        if (row.mode === "managed")
            return (root.autotileAvailable && root.currentMode === "autotile") || (root.scrollingAvailable && root.currentMode === "scrolling");
        return true;
    }

    /// True when the row answers the current filter. The key sequences are
    /// part of the haystack, so "meta alt" answers "what is on this chord?"
    /// as well as the action names do.
    ///
    /// This does NOT remove anything. The filter emphasises rather than
    /// subtracts: dropping rows as the user typed repacked the columns and
    /// resized the card on every keystroke, so the sheet flinched under the
    /// cursor and a row could move out from under the eye that was reading
    /// it. The card's geometry is fixed by the mode filter alone, and the
    /// query only decides which rows stay at full contrast.
    /// Lowercased search text per catalog id, built once per model push rather
    /// than per keystroke. rowMatches is called for every row several times
    /// over on each key (once for the counter, once for each row's own dim
    /// state, once inside its group's heading state), and rebuilding and
    /// case-folding the same strings each time was the bulk of the typing
    /// cost. The catalog only changes when C++ re-pushes it.
    readonly property var haystacks: {
        // Prototype-free, matching the grouping map below: a catalog id equal
        // to an Object.prototype member ("constructor", "toString") would
        // otherwise resolve to a function, and the miss check in rowMatches
        // would sail past it and throw on indexOf.
        let map = Object.create(null);
        for (let i = 0; i < root.shortcuts.length; ++i) {
            const row = root.shortcuts[i];
            let hay = row.label + " " + row.category;
            if (row.assigned)
                hay += " " + row.triggers.join(" ");
            map[row.id] = hay.toLocaleLowerCase();
        }
        return map;
    }

    function rowMatches(row) {
        if (root.queryTerms.length === 0)
            return true;

        const hay = root.haystacks[row.id];
        if (hay === undefined)
            return false;
        for (let i = 0; i < root.queryTerms.length; ++i) {
            if (hay.indexOf(root.queryTerms[i]) < 0)
                return false;
        }
        return true;
    }

    /// Rows regrouped into [{name, categoryOrder, assigned, unassigned}]
    /// preserving the model's category order, with mode-inapplicable and
    /// filtered-out rows dropped, and each category's bound rows separated
    /// from its unbound ones. Recomputes reactively on shortcuts /
    /// currentMode / availability / query changes.
    readonly property var groups: {
        let byCat = [];
        // Keyed on categoryOrder (identity), never on the translated display
        // string: two categories whose translations collide in some locale
        // must not fuse into one group. Object.create(null): a plain {} would
        // let `in` walk the prototype chain.
        let index = Object.create(null);
        for (let i = 0; i < root.shortcuts.length; ++i) {
            const row = root.shortcuts[i];
            // Mode only. The query is applied per row at paint time (see
            // rowMatches) so that typing never changes what the packer sees.
            if (!root.rowVisible(row))
                continue;

            if (!(row.categoryOrder in index)) {
                index[row.categoryOrder] = byCat.length;
                byCat.push({
                    name: row.category,
                    categoryOrder: row.categoryOrder,
                    assigned: [],
                    unassigned: []
                });
            }
            const bucket = byCat[index[row.categoryOrder]];
            if (row.assigned)
                bucket.assigned.push(row);
            else
                bucket.unassigned.push(row);
        }
        return byCat;
    }

    /// Rows the current mode offers at all, ignoring the query. The
    /// denominator of the search field's counter, so a query that narrows the
    /// sheet still says what it narrowed from. An over-narrow query does not
    /// reach this: the field substitutes its own no-matches wording once the
    /// numerator hits zero, rather than reading "0 of 94".
    readonly property int modeRowCount: {
        let n = 0;
        for (let i = 0; i < root.shortcuts.length; ++i) {
            if (root.rowVisible(root.shortcuts[i]))
                ++n;
        }
        return n;
    }

    /// Rows answering the query, counted across the whole mode-filtered
    /// catalog. Deliberately includes rows sitting behind a collapsed rollup:
    /// the counter reports what the query found, not what is currently on
    /// screen, and the rollup line says separately how many of the hidden ones
    /// are its own. The two numbers come from different paths on purpose, so
    /// do not "reconcile" them by subtracting one from the other.
    readonly property int matchedRowCount: {
        if (root.queryTerms.length === 0)
            return root.modeRowCount;

        let n = 0;
        for (let i = 0; i < root.shortcuts.length; ++i) {
            const row = root.shortcuts[i];
            if (root.rowVisible(row) && root.rowMatches(row))
                ++n;
        }
        return n;
    }

    /// True when the given category's unassigned rows are disclosed.
    ///
    /// A query never opens one on its own, for the same reason it never
    /// removes a row: that would resize the card as the user typed. The
    /// disclosure line reports its own hidden matches instead, so a match
    /// behind it is still announced and opening it stays a deliberate click.
    function categoryExpanded(group) {
        return root.expandedCategories[group.categoryOrder] === true;
    }

    /// How many of a category's unassigned rows answer the query. Counts them
    /// whether or not the rollup is open, since it is also what tells an open
    /// rollup's line that it still has something to say. Zero while no query
    /// is active.
    function unassignedMatchCount(group) {
        if (root.queryTerms.length === 0)
            return 0;

        let n = 0;
        for (let i = 0; i < group.unassigned.length; ++i) {
            if (root.rowMatches(group.unassigned[i]))
                ++n;
        }
        return n;
    }

    /// Packing cost of one group in row units: its bound rows plus a fixed
    /// heading allowance, plus the unassigned rollup (one line collapsed, one
    /// line per row when open). Row units are all roughly the same height, so
    /// counting them is an honest proxy for pixels.
    function groupUnits(group) {
        let units = group.assigned.length + 2;
        if (group.unassigned.length > 0)
            units += root.categoryExpanded(group) ? group.unassigned.length + 1 : 1;
        return units;
    }

    readonly property int totalUnits: {
        let t = 0;
        for (let g = 0; g < root.groups.length; ++g)
            t += root.groupUnits(root.groups[g]);
        return t;
    }

    /// Groups flowed into `metrics.columns` buckets in display order, each
    /// group kept WHOLE.
    ///
    /// The previous packer split a group across a column boundary and
    /// reprinted its heading as "(continued)", which meant reading order ran
    /// vertically and then horizontally through severed sections, and on a
    /// typical scrolling screen two of the three headings were continuations
    /// rather than headings. Balance is worth less here than a block a reader
    /// can trust, so a group now moves to the next column only when keeping it
    /// in the current one would overshoot the target by more than half its own
    /// height. A single oversized group (Scrolling) simply owns a tall column,
    /// which the search field above is what actually makes tractable.
    readonly property var columnBuckets: {
        const n = metrics.columns;
        let buckets = [];
        for (let c = 0; c < n; ++c)
            buckets.push([]);
        if (root.groups.length === 0)
            return buckets;

        const target = root.totalUnits / n;
        let col = 0;
        let used = 0;
        for (let g = 0; g < root.groups.length; ++g) {
            const units = root.groupUnits(root.groups[g]);
            // `used > 0` keeps a group taller than the whole target from
            // being bounced out of an empty column forever.
            if (col < n - 1 && used > 0 && used + units - target > units / 2) {
                ++col;
                used = 0;
            }
            buckets[col].push(root.groups[g]);
            used += units;
        }
        // Drop trailing empty buckets: some group-size shapes fill fewer
        // columns than the clamp allowed, and an empty bucket would still
        // reserve a column width plus spacing in metrics.contentWidth.
        while (buckets.length > 1 && buckets[buckets.length - 1].length === 0)
            buckets.pop();
        return buckets;
    }

    /// The tiling mode's display name, for the subtitle. Kept in sync with the
    /// settings pages' own headings ("Snapping" / "Tiling" / "Scrolling") —
    /// note the catalog's internal tag for tiling is "autotile", which is not
    /// a name any user-facing string uses.
    readonly property string modeDisplayName: {
        if (root.currentMode === "autotile")
            return i18n("Tiling");
        if (root.currentMode === "scrolling")
            return i18n("Scrolling");
        return i18n("Snapping");
    }

    // Metrics mirror LayoutPickerContent's card chrome exactly (paddingSide
    // side/bottom padding, title one paddingSide down) so the two popups
    // read as siblings.
    QtObject {
        id: metrics

        readonly property int paddingSide: Kirigami.Units.gridUnit
        // Preferred width, shrunk to the available screen width when even a
        // single column at the preferred size would push the card (content +
        // side padding) past the screen edge — narrow screens get a
        // narrower, still fully visible column instead of clipping.
        readonly property int columnWidth: Math.min(Kirigami.Units.gridUnit * 18, Math.max(Kirigami.Units.gridUnit * 6, Math.floor(root.width * 0.9) - paddingSide * 2))
        readonly property int columnSpacing: Kirigami.Units.gridUnit * 2
        readonly property int maxColumns: 3
        readonly property int columns: {
            const avail = root.width * 0.9 - paddingSide * 2;
            const fit = Math.floor((avail + columnSpacing) / (columnWidth + columnSpacing));
            // Bound by content volume, not group count alone: one column per
            // started ~8 units of content keeps short sheets from spreading
            // into slivers. Groups are no longer split, so the column count
            // can also never usefully exceed the number of groups.
            const worthwhile = Math.max(1, Math.ceil(root.totalUnits / 8));
            return Math.max(1, Math.min(maxColumns, Math.min(fit, worthwhile), Math.max(1, root.groups.length)));
        }
        // Sized from the buckets the packer actually FILLED, not the column
        // clamp: the packer may leave the last allowed column empty.
        readonly property int renderedColumns: Math.max(1, root.columnBuckets.length)
        readonly property int contentWidth: renderedColumns * columnWidth + (renderedColumns - 1) * columnSpacing
        // Height budget left for the scrolling column strip once the card's
        // fixed chrome is accounted for. Floored at three grid units rather
        // than 0: on an extremely short screen a zero budget would collapse
        // the scroller and leave a bare title with no hint that content
        // exists.
        // Four gaps, not three: the column runs title, mode, field, scroller,
        // footer. The mode label's negative top margin claws one gap most of
        // the way back, so it is added here rather than left out — counting
        // four gaps without it would over-correct by nearly a whole gap in the
        // other direction.
        readonly property int chromeHeight: titleLabel.implicitHeight + modeLabel.implicitHeight + searchField.implicitHeight + footerLabel.implicitHeight + cardLayout.spacing * 4 + modeLabel.Layout.topMargin
        readonly property int maxContentHeight: Math.max(Kirigami.Units.gridUnit * 3, Math.round(root.height * 0.85) - paddingSide * 2 - chromeHeight)
    }

    // Backdrop — click outside to dismiss, same bare click-only backdrop
    // as LayoutPickerContent (no scrim; popup surfaces don't dim the
    // desktop).
    MouseArea {
        anchors.fill: parent
        onClicked: root._requestDismiss()
        Accessible.name: i18n("Dismiss shortcut cheatsheet")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: root._requestDismiss()
    }

    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        // Clamped to the screen as well as to the content. columnWidth carries
        // a minimum so a column never becomes unreadably narrow, and on an
        // output too small to honour it the card would otherwise be centred
        // while overhanging both edges, taking the search field's counter off
        // screen with it. Clipping the card is the better failure.
        width: Math.min(root.width, metrics.contentWidth + metrics.paddingSide * 2)
        height: Math.min(root.height, cardLayout.implicitHeight + metrics.paddingSide * 2)

        // No container Accessible.name: the title label below is the single
        // announcement, matching LayoutPickerContent's card.

        // Scrolling keys live on the CARD, not on the search field, so they
        // keep working once Tab has moved focus to a disclosure line. Key
        // events bubble up the item parent chain from whichever descendant
        // holds focus, so one handler here covers every focus position on the
        // sheet. Page keys rather than arrows: the field is single-line, so
        // they have nothing to do in the editor and are unambiguous here.
        //
        // The printable fall-through is the other half of that. A disclosure
        // is a tab stop but not a text sink, so without this a user who tabbed
        // to one and started typing would have their keystrokes go nowhere,
        // with the filter field visibly unchanged.
        Keys.onPressed: event => {
            if (event.key === Qt.Key_PageDown) {
                scroller.scrollByPage(1);
                event.accepted = true;
            } else if (event.key === Qt.Key_PageUp) {
                scroller.scrollByPage(-1);
                event.accepted = true;
            } else if (event.key === Qt.Key_Home && (event.modifiers & Qt.ControlModifier)) {
                scroller.scrollToEnd(-1);
                event.accepted = true;
            } else if (event.key === Qt.Key_End && (event.modifiers & Qt.ControlModifier)) {
                scroller.scrollToEnd(1);
                event.accepted = true;
            } else if (!searchField.fieldHasFocus && root._isPrintable(event.text)) {
                // Typing anywhere on the sheet returns to the filter and keeps
                // the character, rather than dropping it.
                searchField.takeFocus();
                searchField.appendText(event.text);
                event.accepted = true;
            }
        }

        // Absorb clicks inside the card so they never reach the backdrop —
        // same sibling z-order contract as LayoutPickerContent. Declared
        // FIRST so the interactive content below it (search field, disclosure
        // lines) hit-tests ahead of it.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
            onClicked: function (mouse) {
                mouse.accepted = true;
            }
        }

        ColumnLayout {
            id: cardLayout

            anchors.fill: parent
            anchors.margins: metrics.paddingSide
            spacing: Kirigami.Units.largeSpacing

            // Title — shared popup-card typography (PopupCardTitle), matching
            // the picker's "Choose Layout".
            PopupCardTitle {
                id: titleLabel

                Layout.alignment: Qt.AlignHCenter
                text: i18n("Keyboard Shortcuts")
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
            }

            // The sheet has always been filtered by the bound screen's mode
            // and never said so, which made an absent group look like a bug.
            Label {
                id: modeLabel

                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: -Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                text: i18nc("which placement mode the listed shortcuts belong to", "Filtered for %1 mode on this screen", root.modeDisplayName)
                color: Kirigami.Theme.disabledTextColor
                font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.9 * root.fontSizeScale)
            }

            CheatsheetSearchField {
                id: searchField

                Layout.fillWidth: true
                shownCount: root.matchedRowCount
                totalCount: root.modeRowCount
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                onTextChanged: root.query = searchField.text

                // The surface's keyboard grab is applied by OverlayService in
                // the same show, but the compositor's focus-in arrives later.
                // forceActiveFocus is a scene-local claim, so it survives that
                // wait: the field is already the focus item when the window
                // becomes active, and the first keystroke lands in it.
                Component.onCompleted: takeFocus()

                // The field is the only focusable item on the card, so it is
                // also the only place these keys can be caught.
            }

            // Empty state. Reachable two ways now: a query that matches
            // nothing (common), or every catalog row filtered out by mode
            // (unreachable with the shipped taxonomy, since the General group
            // is mode-independent, but a data-driven guarantee is not a
            // structural one).
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                Layout.bottomMargin: Kirigami.Units.gridUnit
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                // Only the mode-filtered-everything-out case now. A query
                // that matches nothing leaves the full sheet in place, dimmed,
                // with the search field's counter reporting no matches.
                text: i18n("No shortcuts apply in the current mode.")
                color: Kirigami.Theme.disabledTextColor
                visible: root.groups.length === 0
                font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * root.fontSizeScale)
            }

            Flickable {
                id: scroller

                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: metrics.contentWidth
                Layout.preferredHeight: Math.min(bucketsRow.implicitHeight, metrics.maxContentHeight)
                // Empty state: the fallback label above occupies the slot
                // instead — hide the (empty) scroller so exactly one item owns
                // it.
                visible: root.groups.length > 0
                // Normally the columns are sized to fit, so contentWidth is the
                // viewport and there is nothing to pan. The exception is an
                // output too narrow to honour the column minimum, where the
                // card is clamped to the screen: taking the content width from
                // the row there keeps the overflowing columns reachable by
                // flick instead of clipping them away permanently.
                contentWidth: Math.max(width, bucketsRow.implicitWidth)
                contentHeight: bucketsRow.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Column order carries meaning, so mirror the flow for
                // right-to-left locales.
                LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
                LayoutMirroring.childrenInherit: true

                // Non-interactive affordance: the sheet can still clip on a
                // short screen and nothing else tells the reader rows are cut
                // off.
                ScrollIndicator.vertical: ScrollIndicator {}

                // Driven from the search field's key handler, which owns focus
                // for the sheet's lifetime. Clamped here rather than at the
                // call site so both entry points get the same bounds.
                function scrollByPage(direction: int) {
                    if (contentHeight <= height) {
                        return;
                    }
                    const step = height * 0.9;
                    contentY = Math.max(0, Math.min(contentHeight - height, contentY + direction * step));
                }

                function scrollToEnd(direction: int) {
                    if (contentHeight <= height) {
                        return;
                    }
                    contentY = direction < 0 ? 0 : contentHeight - height;
                }

                // Bring a keyboard-focused item into view. A Flickable does
                // not do this for its descendants, so a tab stop inside the
                // clipped strip would otherwise be reachable but invisible.
                // Only scrolls when the item is actually outside the viewport,
                // so tabbing through what is already on screen does not jump.
                function ensureVisible(target: Item) {
                    if (!target || contentHeight <= height) {
                        return;
                    }
                    const top = target.mapToItem(bucketsRow, 0, 0).y;
                    const bottom = top + target.height;
                    const margin = Kirigami.Units.largeSpacing;
                    if (top < contentY) {
                        contentY = Math.max(0, top - margin);
                    } else if (bottom > contentY + height) {
                        contentY = Math.min(contentHeight - height, bottom - height + margin);
                    }
                }

                Row {
                    id: bucketsRow

                    spacing: metrics.columnSpacing

                    Repeater {
                        model: root.columnBuckets

                        delegate: Column {
                            id: bucketColumn

                            required property var modelData

                            width: metrics.columnWidth
                            spacing: Kirigami.Units.gridUnit

                            Repeater {
                                model: bucketColumn.modelData

                                delegate: CheatsheetGroup {
                                    required property var modelData

                                    width: metrics.columnWidth
                                    name: modelData.name
                                    assignedRows: modelData.assigned
                                    unassignedRows: modelData.unassigned
                                    expanded: root.categoryExpanded(modelData)
                                    latchedRowId: root.latchedRowId
                                    queryTerms: root.queryTerms
                                    unassignedMatches: root.unassignedMatchCount(modelData)
                                    matcher: root.rowMatches
                                    scrollerMoving: scroller.moving
                                    layoutsAreTemplates: root.layoutsAreTemplates
                                    fontFamily: root.fontFamily
                                    fontSizeScale: root.fontSizeScale
                                    onExpandToggled: root._toggleCategory(modelData.categoryOrder)
                                    onFocusScrollRequested: target => scroller.ensureVisible(target)
                                    onLatchRequested: rowId => root.latchedRowId = rowId
                                    onLatchCleared: root.latchedRowId = ""
                                }
                            }
                        }
                    }
                }
            }

            // Escape is a daemon-side global grab, so it closes the sheet
            // outright rather than first clearing the filter; say so plainly
            // instead of letting the user discover it.
            Label {
                id: footerLabel

                Layout.alignment: Qt.AlignHCenter
                text: i18n("Press Escape to close")
                color: Kirigami.Theme.disabledTextColor
                font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.85 * root.fontSizeScale)
            }
        }
    }
}
