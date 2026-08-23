// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Banner stack for AnimationEventCard: inheritance info, shadowing
 * warning, mirror-divergence warning, and the italic "Current:" line.
 *
 * Pure presentation split out of the card so the card stays within the
 * project file-size ceiling; every value is fed in by the card and the
 * one action (clear shadowing children) is emitted back. The component
 * hides itself entirely when no banner has anything to say, so it never
 * contributes a phantom spacing slot to the card's column.
 */
ColumnLayout {
    id: root

    /// Flips the info banner between the parent-node fan-out note and the
    /// leaf inheritance breadcrumb.
    required property bool isParentNode
    /// Whether the event holds ANY direct override (either timing field or
    /// the shader leg) — the card's derived `overrideEnabled`.
    required property bool overrideActive
    /// The card's single "the timing editor is showing" predicate
    /// (`_timingEditorOpen` = an override exists, or the editor was just
    /// latched open with nothing written yet). Taken WHOLE rather than
    /// respelled from its two inputs, because the card gates four other things
    /// on it and a copy here would drift.
    required property bool timingEditorOpen
    /// Descendant shader overrides that shadow a parent-node card.
    required property int shadowingChildrenCount
    /// How many events below this one kept parameter values authored against a
    /// pack this parent no longer resolves. Distinct from the shadowing count
    /// above: those override this parent's PACK, these still follow it.
    required property int staleParamChildrenCount
    /// Mirror-divergence state, precomputed by the card.
    required property bool mirrorsDiverged
    required property int divergentPathCount
    required property int writePathCount
    /// "window ← global" ancestor breadcrumb; empty at the taxonomy root.
    required property string parentChain
    /// Resolved curve + duration summary for the "Current:" line.
    required property string inheritSummary

    /// The shadowing warning's one-click remediation.
    signal clearShadowingRequested
    signal clearStaleParamsRequested

    // Parent nodes show the fan-out note whenever the timing editor is open;
    // leaves show the inheritance breadcrumb until a direct override exists.
    readonly property bool _infoVisible: isParentNode ? timingEditorOpen : !overrideActive
    readonly property bool _shadowingVisible: isParentNode && shadowingChildrenCount > 0
    readonly property bool _staleParamsVisible: isParentNode && staleParamChildrenCount > 0
    readonly property bool _currentVisible: !overrideActive

    spacing: Kirigami.Units.smallSpacing
    // An all-hidden ColumnLayout still occupies a spacing slot in the
    // card's column; collapse it outright when nothing is shown.
    visible: _infoVisible || _shadowingVisible || _staleParamsVisible || mirrorsDiverged || _currentVisible

    // ── Inheritance info ──────────────────────────────────────────────
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        type: Kirigami.MessageType.Information
        visible: root._infoVisible
        text: {
            if (root.isParentNode)
                return i18n("Settings here apply to all child events unless individually overridden.");

            if (root.parentChain.length > 0)
                return i18n("Inheriting from: %1", root.parentChain);

            return i18n("Using library defaults");
        }
    }

    // ── Shadowing-children warning (parent-node cards only) ───────────
    // ShaderProfileTree::resolve walks parent → leaf and overlays each
    // level's `effectId` if engaged; deeper leaves win. So a stale
    // per-leg override from an earlier session silently overrides the
    // parent at runtime — even though the parent card visually shows its
    // own value and the user never sees the shadowing leaf. Surface it
    // explicitly with one-click remediation; without the button, the
    // only fix is to find each shadowing leaf manually and clear it.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        type: Kirigami.MessageType.Warning
        visible: root._shadowingVisible
        text: i18np("%n descendant event has a shader override that shadows this parent.", "%n descendant events have shader overrides that shadow this parent.", root.shadowingChildrenCount)
        actions: [
            Kirigami.Action {
                text: i18n("Clear shadowing children")
                icon.name: "edit-clear-all"
                onTriggered: {
                    root.clearShadowingRequested();
                }
            }
        ]
    }

    // ── Orphaned parameter values (parent-node cards only) ────────────
    // Surfaced HERE, at the parent, because switching this row's pack is what
    // strands them, and the person who did that is standing at this card. They
    // may never open the child's own card — setting a pack at a category level
    // is precisely the act of not wanting to think about individual events.
    //
    // Deliberately NOT folded into the shadowing warning above. These events do
    // not shadow this parent's pack, they FOLLOW it; only their parameter
    // values are orphaned. Counting them there would make that warning's own
    // sentence untrue, which is the conflation the params-only work exists to
    // undo.
    //
    // Informational rather than a warning: nothing is broken, the events just
    // render this pack's defaults. The action DISCARDS — switching this row
    // back to the pack those values were authored against would revive them —
    // so it says discard rather than clear or tidy.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        type: Kirigami.MessageType.Information
        visible: root._staleParamsVisible
        text: i18np("%n event below this one kept settings from a different shader pack, so they no longer apply.", "%n events below this one kept settings from a different shader pack, so they no longer apply.", root.staleParamChildrenCount)
        actions: [
            Kirigami.Action {
                text: i18n("Discard those settings")
                icon.name: "edit-clear-all"
                onTriggered: {
                    root.clearStaleParamsRequested();
                }
            }
        ]
    }

    // ── Mirror divergence warning (mirrored cards only) ───────────────
    // The card writes every mirror path but reads only the primary, so a
    // mirror given its own value elsewhere is not shown by any control
    // here and the next edit on that setting replaces it. Warn before
    // that happens rather than after.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        type: Kirigami.MessageType.Warning
        visible: root.mirrorsDiverged
        // Names both axes `divergentPathCount` compares, because
        // both have a group writer. Writes are per setting: editing the
        // duration converges the duration everywhere but leaves a
        // divergent curve alone, so the sentence promises convergence
        // for the edited setting only.
        //
        // Two different counts, because the two clauses name two
        // different sets: the diverging events (mirrors out of step plus
        // the primary they differ from), and the full write reach of the
        // next edit (every path, including mirrors already in step).
        // Both counts are two or more whenever the banner is visible, so
        // a singular plural form here would never render.
        text: i18n("%1 of the events this card controls hold different values right now, and it shows only one of them. The next change you make to the timing or the shader here applies that setting to all %2 of them.", root.divergentPathCount, root.writePathCount)
    }

    // ── Italic "Current:" line (override off) ─────────────────────────
    Label {
        Layout.fillWidth: true
        // Inset to match the banners above instead of hugging the left
        // edge.
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        visible: root._currentVisible
        text: i18n("Current: %1", root.inheritSummary)
        font.italic: true
        color: Kirigami.Theme.disabledTextColor
    }
}
