// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One profile row in the Profiles page tree list.
 *
 * Collapsed: depth indentation · identicon · name + parent subtitle · status
 * badge (Active / Modified) · a trailing strip of visible action buttons
 * (activate, update-when-modified, rename, duplicate, set parent, export,
 * delete). Every action stays on the row — no overflow menu.
 *
 * Expanded (the rule row's pattern): a read-only diff of what this profile
 * overrides relative to its parent, split into SETTINGS and RULES sections
 * under the shared SectionHeaderPill capsules.
 */
ExpandableRowDelegate {
    id: row

    /// One row map from ProfileStore::availableProfiles().
    required property var modelData

    /// The ProfileStore, for the on-demand diff the expansion shows. Null
    /// disables expansion entirely (the shell keeps the row collapsed).
    property var bridge: null

    readonly property string profileId: modelData.id
    readonly property string profileName: modelData.name
    readonly property string profileDescription: modelData.description
    readonly property int depth: modelData.depth
    readonly property bool isRoot: modelData.isRoot
    readonly property string parentName: modelData.parentName
    readonly property bool isActive: modelData.active
    readonly property bool isModified: modelData.modified

    signal activateRequested
    signal updateRequested
    signal renameRequested
    signal duplicateRequested
    signal setParentRequested
    signal exportRequested
    signal deleteRequested

    // The header fits on one row; the body is the read-only diff below.
    expansionContent: row.bridge ? diffComponent : null

    // Depth indent: a leading guide so nesting reads at a glance.
    Item {
        Layout.preferredWidth: row.depth * Kirigami.Units.gridUnit * 1.5
        Layout.fillHeight: true
        visible: row.depth > 0

        Kirigami.Separator {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            opacity: 0.4
        }
    }

    // Identicon derived from the profile's resolved settings — two profiles
    // that cascade to the same values draw the same mark.
    ProfileSignature {
        signature: row.modelData.signature
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
        Layout.preferredHeight: Kirigami.Units.iconSizes.medium

        HoverHandler {
            id: signatureHover
        }

        ToolTip.text: i18n("A visual fingerprint of everything this profile resolves to, including what it inherits.")
        ToolTip.visible: signatureHover.hovered
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 0

        Label {
            Layout.fillWidth: true
            text: row.profileName
            elide: Text.ElideRight
            font.bold: row.isActive
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: {
                const base = row.isRoot ? i18n("Based on defaults") : i18n("Inherits from “%1”", row.parentName);
                return row.profileDescription.length > 0 ? (base + " · " + row.profileDescription) : base;
            }
            elide: Text.ElideRight
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }
    }

    // Status badge — Active (clean) or Modified (edited away from the profile).
    RowLayout {
        visible: row.isActive
        Layout.alignment: Qt.AlignVCenter
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: row.isModified ? "documentinfo" : "dialog-ok-apply"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            color: row.isModified ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
        }

        Label {
            text: row.isModified ? i18n("Modified") : i18n("Active")
            color: row.isModified ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
            font: Kirigami.Theme.smallFont
        }
    }

    // ── Trailing action strip — all visible, RuleRow-style ──
    ToolButton {
        icon.name: "dialog-ok-apply"
        Layout.alignment: Qt.AlignVCenter
        // Already exactly on this profile → nothing to do. A modified active
        // profile stays enabled so the user can re-apply (revert their edits).
        enabled: !row.isActive || row.isModified
        ToolTip.text: row.isActive && row.isModified ? i18n("Re-apply this profile (discards changes since)") : i18n("Activate this profile")
        ToolTip.visible: hovered
        Accessible.name: i18n("Activate profile %1", row.profileName)
        onClicked: row.activateRequested()
    }

    ToolButton {
        icon.name: "document-save"
        Layout.alignment: Qt.AlignVCenter
        // Shown but disabled off the modified-active state, so the strip keeps
        // its column alignment across rows (the RuleRow managed-delete idiom).
        enabled: row.isActive && row.isModified
        ToolTip.text: i18n("Update this profile from the current settings")
        ToolTip.visible: hovered
        Accessible.name: i18n("Update profile %1 from current settings", row.profileName)
        onClicked: row.updateRequested()
    }

    ToolButton {
        icon.name: "edit-rename"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Rename")
        ToolTip.visible: hovered
        Accessible.name: i18n("Rename profile %1", row.profileName)
        onClicked: row.renameRequested()
    }

    ToolButton {
        icon.name: "edit-copy"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Duplicate")
        ToolTip.visible: hovered
        Accessible.name: i18n("Duplicate profile %1", row.profileName)
        onClicked: row.duplicateRequested()
    }

    ToolButton {
        icon.name: "document-import"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Set parent")
        ToolTip.visible: hovered
        Accessible.name: i18n("Set the parent of profile %1", row.profileName)
        onClicked: row.setParentRequested()
    }

    ToolButton {
        icon.name: "document-export"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Export")
        ToolTip.visible: hovered
        Accessible.name: i18n("Export profile %1", row.profileName)
        onClicked: row.exportRequested()
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Delete")
        ToolTip.visible: hovered
        Accessible.name: i18n("Delete profile %1", row.profileName)
        onClicked: row.deleteRequested()
    }

    // ── Expanded body: what this profile overrides, before → after ──
    // Structured like the rule preview: a SectionHeaderPill over a tree view
    // per half, so the two previews read as one design.
    Component {
        id: diffComponent

        ColumnLayout {
            id: diffColumn

            readonly property var configRows: row.bridge ? row.bridge.configChanges(row.profileId) : []
            readonly property var ruleRows: row.bridge ? row.bridge.ruleChanges(row.profileId) : []

            /// Display text for one side of a change.
            ///
            /// The store resolves what it can without live state (an enum's
            /// word, a number's unit) and passes it as @p resolved. Everything
            /// else is resolved here by @p kind, because it depends on what is
            /// plugged in or installed right now — the same split, and the same
            /// resolution sources, that ActionListView uses for rule params.
            ///
            /// Every branch falls back to the raw value rather than a blank: a
            /// deleted layout or an unplugged monitor should still show the id
            /// it refers to.
            function formatValue(value, kind, resolved) {
                if (resolved !== undefined && resolved.length > 0)
                    return resolved;
                if (value === undefined || value === null)
                    return i18nc("a setting with no value", "Unset");
                if (typeof value === "boolean")
                    return value ? i18nc("a boolean setting that is on", "On") : i18nc("a boolean setting that is off", "Off");

                // Id kinds only ever claim STRINGS.
                //
                // A leaf nested in a structured key inherits that key's kind, and
                // a shader or decoration tree holds both pack ids and numeric
                // parameters. Without this guard a parameter took the pack
                // branch, missed the catalogue, and fell back to its raw value —
                // which is how 0.9500000000000001 reached the pill, having
                // skipped the number formatting further down.
                if (typeof value === "string") {
                    const layouts = settingsController.layouts ? settingsController.layouts : [];
                    if (kind === "layoutId")
                        return diffColumn.resolveById(layouts, value, "id", "displayName");
                    // Algorithms live in the layout list under an "autotile:"
                    // prefix, matching how the rule preview resolves them.
                    if (kind === "tilingAlgorithm")
                        return diffColumn.resolveById(layouts, "autotile:" + value, "id", "displayName", value);
                    if (kind === "screenId")
                        return diffColumn.resolveById(settingsController.screens ? settingsController.screens : [], value, "name", "displayLabel");
                    // The catalogues hang off settingsController, not appSettings
                    // — the rule preview reaches them through a local alias bag
                    // (RulesPage._editorAppSettings), which is why its code reads
                    // as appSettings.*. They are separate registries: an
                    // animation pack and an overlay pack sharing an id are
                    // different things, so the kind picks the source.
                    if (kind === "shaderPack")
                        return diffColumn.resolvePack(settingsController.animationsPage, value);
                    if (kind === "decorationPack")
                        return diffColumn.resolvePack(settingsController.decorationPage, value);
                    if (kind === "overlayShader")
                        return diffColumn.resolvePack(settingsController.snappingShadersPage, value);
                }
                // A desktop is a number, so it sits outside the string guard.
                if (kind === "virtualDesktop")
                    return diffColumn.resolveDesktop(value);

                if (typeof value === "string")
                    return value.length > 0 ? value : i18nc("an empty text setting", "Empty");
                if (typeof value === "object")
                    return diffColumn.summarizeStructured(value);
                // A double that came out of JSON carries its binary
                // representation with it: 0.95 round-trips as
                // 0.9500000000000001. Nobody set that, and showing it suggests a
                // precision the setting does not have. Four decimals is past
                // anything the sliders can express, so this only ever trims
                // noise, and parseFloat drops the zeros it leaves behind.
                if (typeof value === "number" && !Number.isInteger(value))
                    return String(parseFloat(value.toFixed(4)));
                return String(value);
            }

            /// Scan @p list for the entry whose @p idField matches @p value and
            /// return its @p labelField. Falls back to @p fallback (or the raw
            /// value) when the list has no such entry, which is the normal case
            /// for anything deleted, uninstalled, or unplugged.
            function resolveById(list, value, idField, labelField, fallback) {
                const raw = fallback !== undefined ? fallback : String(value);
                if (!list)
                    return raw;

                for (let i = 0; i < list.length; ++i) {
                    if (list[i][idField] === value) {
                        const label = list[i][labelField];
                        return label !== undefined && label.length > 0 ? label : raw;
                    }
                }
                return raw;
            }

            /// Virtual desktops are numbered from 1 and named by KWin at
            /// runtime, so an out-of-range number keeps its digits.
            function resolveDesktop(value) {
                const index = Number(value);
                const names = settingsController.virtualDesktopNames;
                if (!names || !isFinite(index) || index < 1 || index > names.length)
                    return String(value);

                return i18nc("@item a virtual desktop, by number and name", "%1: %2", index, names[index - 1]);
            }

            /// Shader, decoration, and overlay packs each come from their own
            /// catalogue, so the controller to ask is passed in.
            function resolvePack(source, value) {
                if (!source || typeof source.availableShaderEffects !== "function")
                    return String(value);

                return diffColumn.resolveById(source.availableShaderEffects(), value, "id", "name");
            }

            /// The store enumerates a structured setting into one row per changed
            /// leaf, so almost everything arriving here is already a scalar. The
            /// exception is a trigger, which the store keeps whole because its
            /// two halves only mean something together: `{ modifier: 134217728,
            /// mouseButton: 2 }` reads as "Alt + Right".
            function summarizeStructured(value) {
                if (value.modifier !== undefined || value.mouseButton !== undefined) {
                    return TriggerLabels.label(value.modifier || 0, value.mouseButton || 0, i18nc("a trigger with no modifier or button set", "None"));
                }
                // A shape the store could not descend into (it caps nesting
                // depth). Report the size so the row still says something, and
                // leave the payload on the tooltip.
                const keys = Object.keys(value);
                return keys.length === 0 ? i18nc("a structured setting holding nothing", "None") : i18np("%n entry", "%n entries", keys.length);
            }

            /// Full payload for a structured value, shown on the pill's tooltip
            /// so summarising costs the reader nothing. Empty for plain values.
            function detailFor(value) {
                if (typeof value !== "object" || value === null)
                    return "";

                try {
                    return JSON.stringify(value, null, 2);
                } catch (error) {
                    return "";
                }
            }

            /// Config deltas as ProfileDiffView rows: the key, then FROM / TO.
            readonly property var settingsRows: {
                const out = [];
                for (let i = 0; i < configRows.length; ++i) {
                    const change = configRows[i];
                    out.push({
                        "segments": change.segments,
                        "entries": [
                            {
                                "caption": i18nc("@label the value a setting had before this profile", "From"),
                                "value": diffColumn.formatValue(change.before, change.kind, change.beforeText),
                                "detail": diffColumn.detailFor(change.before)
                            },
                            {
                                "caption": i18nc("@label the value this profile sets", "To"),
                                "value": diffColumn.formatValue(change.after, change.kind, change.afterText),
                                "detail": diffColumn.detailFor(change.after)
                            }
                        ]
                    });
                }
                return out;
            }

            /// Rule deltas as ProfileDiffView rows: the rule, then its change.
            readonly property var rulesRows: {
                const out = [];
                for (let i = 0; i < ruleRows.length; ++i) {
                    const change = ruleRows[i];
                    let label = i18nc("a rule this profile alters", "Changed");
                    let tint = Kirigami.Theme.neutralTextColor;
                    if (change.change === "added") {
                        label = i18nc("a rule this profile adds", "Added");
                        tint = Kirigami.Theme.positiveTextColor;
                    } else if (change.change === "removed") {
                        label = i18nc("a rule this profile drops", "Removed");
                        tint = Kirigami.Theme.negativeTextColor;
                    }
                    out.push({
                        "label": change.name,
                        "entries": [
                            {
                                "caption": i18nc("@label how a rule differs from the parent profile", "Change"),
                                "value": label,
                                "emphasis": tint
                            }
                        ]
                    });
                }
                return out;
            }

            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                visible: diffColumn.configRows.length === 0 && diffColumn.ruleRows.length === 0
                text: row.isRoot ? i18n("Nothing overridden — this profile matches the defaults.") : i18n("Nothing overridden — this profile matches “%1”.", row.parentName)
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                visible: diffColumn.settingsRows.length > 0
                text: i18nc("@title diff section listing changed settings", "Settings")
            }

            ProfileDiffView {
                Layout.fillWidth: true
                visible: diffColumn.settingsRows.length > 0
                rows: diffColumn.settingsRows
            }

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: Kirigami.Units.smallSpacing
                visible: diffColumn.rulesRows.length > 0
                text: i18nc("@title diff section listing changed rules", "Rules")
            }

            ProfileDiffView {
                Layout.fillWidth: true
                visible: diffColumn.rulesRows.length > 0
                rows: diffColumn.rulesRows
            }
        }
    }
}
