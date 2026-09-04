// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

/**
 * One shortcut row on the cheatsheet: an action label on the left, its bound
 * key sequences as chip runs on the right, or an "Unassigned" marker when the
 * action has no binding.
 *
 * Split out of CheatsheetContent so the sheet's packing and filtering logic
 * is readable without four levels of delegate nesting under it. The row owns
 * only its own presentation; the tooltip LATCH lives on the sheet, because a
 * long-press on a second row has to close the first one's tooltip and no row
 * can see its siblings.
 */
RowLayout {
    id: root

    /// One catalog row from ShortcutManager::cheatsheetModel(). See the
    /// `shortcuts` docs on CheatsheetContent for the field list.
    required property var modelData
    /// Lowercased query terms currently filtering the sheet, used to bold the
    /// matched runs of the label. Empty when no filter is active.
    property list<string> queryTerms: []
    /// False when a filter is active and this row does not answer it. The row
    /// still occupies its place, so the card's geometry never changes as the
    /// user types; it simply recedes. Non-matching rows stay legible enough to
    /// read if the eye lands on one, rather than being greyed to nothing.
    property bool matched: true
    /// True when this row's tooltip is the one the sheet has latched open
    /// from a touch long-press.
    property bool latched: false
    /// Suppresses the tooltip while the column strip is being flicked.
    property bool scrollerMoving: false
    /// True when the bound screen consumes layouts as sizing templates, which
    /// changes the wording of some rows' tooltips.
    property bool layoutsAreTemplates: false
    property string fontFamily: ""
    property real fontSizeScale: 1

    /// Emitted on a touch long-press / tap so the sheet can move its latch.
    signal latchRequested
    signal latchCleared

    readonly property string effectiveFamily: fontFamily.length > 0 ? fontFamily : Kirigami.Theme.defaultFont.family
    readonly property int rowFontSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * fontSizeScale)

    /// Template-capability rows swap their tooltip wording;
    /// templatesDescription falls back to description in the model, so this
    /// stays a two-way pick.
    readonly property string effectiveDescription: root.layoutsAreTemplates ? (root.modelData.templatesDescription || root.modelData.description || "") : (root.modelData.description || "")

    /// The label with every run matching a query term wrapped in <b>, as
    /// Text.StyledText markup. Bold rather than a colour: the markup would
    /// have to carry a literal hex value, and a theme colour serialised into
    /// HTML is exactly the kind of hardcoded colour the sheet otherwise
    /// avoids. Weight reads as well and costs nothing in either theme.
    function markUpLabel(text, terms) {
        function escaped(s) {
            return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        }
        if (terms.length === 0)
            return escaped(text);

        const lower = text.toLocaleLowerCase();
        // Case folding is not always length-preserving (Turkish dotted I is
        // the standard example), and the index arithmetic below assumes it
        // is. Rather than mark the wrong characters, drop the emphasis and
        // keep the label truthful; the row still matched and still shows.
        if (lower.length !== text.length)
            return escaped(text);

        let marks = [];
        for (let i = 0; i < text.length; ++i)
            marks.push(false);
        for (let t = 0; t < terms.length; ++t) {
            const term = terms[t];
            if (term.length === 0)
                continue;

            // Advance by one, not by the term length: overlapping occurrences
            // of a repeated term must all light up.
            let from = 0;
            let idx = lower.indexOf(term, from);
            while (idx >= 0) {
                for (let k = idx; k < idx + term.length; ++k)
                    marks[k] = true;
                from = idx + 1;
                idx = lower.indexOf(term, from);
            }
        }

        let out = "";
        let pos = 0;
        while (pos < text.length) {
            const start = pos;
            const on = marks[pos];
            while (pos < text.length && marks[pos] === on)
                ++pos;
            const chunk = escaped(text.substring(start, pos));
            out += on ? "<b>" + chunk + "</b>" : chunk;
        }
        return out;
    }

    /// Key tokens of one display sequence, for the chip row. A trailing "+"
    /// means the plus key itself is the final token, and a multi-step
    /// sequence ("Ctrl+X, Ctrl+S" — unreachable via KGlobalAccel, defensive
    /// only) flattens to the tokens of every step rather than producing a
    /// garbled "X, Ctrl" token.
    function keyTokens(seq) {
        let tokens = [];
        const steps = seq.split(", ");
        for (let s = 0; s < steps.length; ++s) {
            const step = steps[s];
            let parts = step.split("+").filter(function (p) {
                return p.length > 0;
            });
            if (step.endsWith("+"))
                parts.push("+");
            tokens = tokens.concat(parts);
        }
        return tokens;
    }

    spacing: Kirigami.Units.smallSpacing
    // The row's opacity composites over its whole subtree, so it MULTIPLIES
    // with the chips' own alphas rather than replacing them: a modifier cap
    // already drawn at 0.6 text alpha lands at 0.6 x this. At 0.32 that put
    // modifier text near 0.19 effective alpha, which is under any usable
    // contrast floor. Raising the floor here rather than flattening the chip
    // alphas keeps the recede-the-modifiers relationship exactly as designed
    // and fixes the composition instead.
    opacity: root.matched ? 1 : 0.5
    // Short enough not to lag the typing it responds to, long enough that the
    // sheet reads as re-weighting itself rather than flickering.
    //
    // A Behavior animates on CHANGE, not on re-evaluation, so this does not
    // cost an animation per row per keystroke. Measured on a 94-row sheet: the
    // first keystroke starts 84 legs as most rows drop out together, and a
    // further keystroke that does not change which rows match starts none.
    // The burst is the transition into a query, and it happens once.
    Behavior on opacity {
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.OutCubic
        }
    }

    Accessible.role: Accessible.StaticText
    // Announces every binding, and composes the unassigned state from the
    // SAME translated token the visible label shows, so a translator cannot
    // make the two diverge.
    readonly property string accessibleRowText: root.modelData.assigned ? i18nc("shortcut row: action, keys", "%1, %2", root.modelData.label, root.modelData.triggers.join(", ")) : i18nc("shortcut row: action, state", "%1, %2", root.modelData.label, unassignedLabel.text)
    // While a query is up, the row's answer to it is carried in the name
    // rather than in opacity alone. Dimming and the bold runs inside the label
    // are the sighted signal, and neither reaches the accessibility tree: the
    // label is deliberately ignored below, and opacity is not announced. So
    // without this a screen-reader user typing a query hears the whole sheet
    // read back unchanged. Only matching rows are marked, so the common case
    // adds nothing to what is spoken.
    Accessible.name: (root.queryTerms.length > 0 && root.matched) ? i18nc("a shortcut row that answers the current search, then the row itself", "Match, %1", root.accessibleRowText) : root.accessibleRowText
    Accessible.description: root.effectiveDescription

    // Plain-prose explanation from the catalog, on hover (long-press on
    // touch). Rows without one (empty description) show no tooltip.
    HoverHandler {
        id: rowHover

        enabled: root.effectiveDescription.length > 0
    }

    // Touch path: the sheet-level latch is folded into the SAME visible
    // binding, never an imperative open() — a C++ open over a declarative
    // binding would leave the tooltip stuck on touch devices, where no hover
    // change ever re-runs the binding. A tap on the row, a long-press on
    // another row, or a flick clears it.
    TapHandler {
        enabled: rowHover.enabled
        onLongPressed: root.latchRequested()
        onTapped: root.latchCleared()
    }

    // An explicit per-row instance, not the attached ToolTip: the attached
    // form shares one engine-wide popup (row-to-row moves can cancel the
    // tooltip just shown) and cannot pin popupType, and on this layer-shell
    // surface a style-driven promotion to a native popup window would hit the
    // QPA's unreachable xdg_popup path. Popup.Item keeps it in-scene, the same
    // pin the settings combos carry. A per-row instance also dies with its
    // delegate, so a rebuild cannot strand an open tooltip.
    ToolTip {
        popupType: T.Popup.Item
        visible: (rowHover.hovered || root.latched) && !root.scrollerMoving
        onClosed: {
            if (root.latched)
                root.latchCleared();
        }
        text: root.effectiveDescription
        delay: Kirigami.Units.toolTipDelay
        font.family: root.effectiveFamily
        font.pixelSize: root.rowFontSize
    }

    Label {
        // Only matching rows are marked up. A dimmed row has already failed at
        // least one term and is not what the reader is being pointed at, so
        // bolding whatever fragments it does contain adds noise rather than
        // signal — and it is also the bulk of the per-keystroke work, since the
        // dimmed rows are the majority on any narrow query.
        text: root.matched ? root.markUpLabel(root.modelData.label, root.queryTerms) : root.markUpLabel(root.modelData.label, [])
        // StyledText unconditionally, not "rich only while filtering": the
        // markUpLabel escape pass runs in both cases, so a label containing a
        // literal ampersand or angle bracket renders identically whether or
        // not a query is active. Switching textFormat with the query would
        // make that one label change shape as the user types. The trade is
        // that StyledText also collapses whitespace runs, so a translated
        // label carrying a double space or a newline sets differently than it
        // would as PlainText. No shipped label does, and one that did would be
        // a catalog bug rather than something to work around here.
        textFormat: Text.StyledText
        // The row announces a composed "action, keys" Accessible.name; keep
        // the visible children out of the a11y tree so screen readers don't
        // announce them twice.
        Accessible.ignored: true
        // Wrap, never elide: the model ships group-contextual short labels
        // sized to fit, and a pathological case (translation, custom font)
        // grows a second line instead of losing text.
        wrapMode: Text.Wrap
        font.family: root.effectiveFamily
        font.pixelSize: root.rowFontSize
        Layout.fillWidth: true
        // Never crushed to nothing by a long chip run in a narrow column; an
        // overlong run overflows the row's width (clipped only at the card
        // edge by the Flickable) rather than eating the label.
        Layout.minimumWidth: Kirigami.Units.gridUnit * 3
    }

    // One chip row per BOUND SEQUENCE, so an alternate binding is visible
    // instead of silently dropped (the C++ compression already declines to
    // merge rows carrying alternates for the same reason).
    Column {
        spacing: Math.round(Kirigami.Units.smallSpacing / 2)
        visible: root.modelData.assigned

        Repeater {
            model: root.modelData.assigned ? root.modelData.triggers : []

            delegate: Row {
                id: chipRow

                required property string modelData

                readonly property var tokens: root.keyTokens(chipRow.modelData)

                spacing: Math.round(Kirigami.Units.smallSpacing / 2)
                anchors.right: parent.right

                Repeater {
                    model: chipRow.tokens

                    delegate: KeyChip {
                        required property string modelData
                        required property int index

                        text: modelData
                        // The key text is part of the search haystack, so a
                        // query like "meta alt" matches on chips alone. The
                        // label's bold runs cannot show that, and without this
                        // such a query left its matched rows at full contrast
                        // with nothing marked on them.
                        highlighted: root.queryTerms.length > 0 && root.matched && root.queryTerms.some(term => modelData.toLocaleLowerCase().indexOf(term) >= 0)
                        // Positional, never a name match against a modifier
                        // list: these tokens come from QKeySequence in NATIVE
                        // text, so "Meta" is localised and may render as a
                        // glyph. In a key sequence the last token is always
                        // the key and everything before it is a modifier, so
                        // position answers the question without knowing any
                        // of the names. (A multi-step sequence flattens to
                        // one run and would dim its interior key too; that
                        // path is unreachable through KGlobalAccel.)
                        dimmed: index < chipRow.tokens.length - 1
                        fontFamily: root.fontFamily
                        fontSizeScale: root.fontSizeScale
                    }
                }
            }
        }
    }

    Label {
        id: unassignedLabel

        text: i18n("Unassigned")
        color: Kirigami.Theme.disabledTextColor
        font.italic: true
        font.family: root.effectiveFamily
        font.pixelSize: root.rowFontSize
        visible: !root.modelData.assigned
        // Covered by the row's composed "%1, unassigned" Accessible.name.
        Accessible.ignored: true
    }
}
