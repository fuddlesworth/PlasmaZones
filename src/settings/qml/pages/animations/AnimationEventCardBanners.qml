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
    /// The card's session-local "timing editor opened, nothing written yet"
    /// latch. Parent nodes show the fan-out note while it is set.
    required property bool editingTiming
    /// Descendant shader overrides that shadow a parent-node card.
    required property int shadowingChildrenCount
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

    // Parent nodes show the fan-out note whenever the timing editor is
    // open (override present or just latched for editing); leaves show
    // the inheritance breadcrumb until a direct override exists.
    readonly property bool _infoVisible: isParentNode ? (overrideActive || editingTiming) : !overrideActive
    readonly property bool _shadowingVisible: isParentNode && shadowingChildrenCount > 0
    readonly property bool _currentVisible: !overrideActive

    spacing: Kirigami.Units.smallSpacing
    // An all-hidden ColumnLayout still occupies a spacing slot in the
    // card's column; collapse it outright when nothing is shown.
    visible: _infoVisible || _shadowingVisible || mirrorsDiverged || _currentVisible

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
        // Names both axes the card's _storedStateKey compares, because
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
