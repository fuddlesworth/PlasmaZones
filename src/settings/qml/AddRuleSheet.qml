// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Guided "Add rule" subject chooser — 3-column tile grid.
 *
 * Two sections: pre-fab templates (a fully-formed rule with actions seeded)
 * on top, then the bare subjects (an empty rule pre-shaped for one match
 * dimension). Both hand a rule JSON to the RuleEditorSheet via
 * `subjectChosen`. Priority is never surfaced here — it is derived per
 * subject/template and only exposed in the Advanced editor.
 *
 * Tiles use a 3-column GridLayout so the picker stays compact and scannable
 * regardless of how many quick-starts or subjects are added. Each tile shows
 * a large icon, title, and a small description — the description elides at
 * three lines so a long entry never blows up the row height.
 */
Kirigami.OverlaySheet {
    id: sheet

    /// The WindowRuleController.
    required property var controller
    // Cache once — `ruleTemplates()` allocates a fresh QVariantList on every
    // call; the Repeater would re-evaluate on every unrelated binding tick.
    readonly property var _templates: sheet.controller.ruleTemplates()
    // Static subject catalogue — kept in QML rather than the controller
    // because the labels/descriptions are pure UI text and the icons are
    // freedesktop ids that the controller doesn't need to know about.
    // Ordered: context-based subjects first (Monitor / Desktop / Activity),
    // then window-based (Application / Animation / Custom). This pairs the
    // first row with the "where" axis and the second row with the "what"
    // axis, which matches how users tend to reach for rules.
    readonly property var _subjects: [{
        "id": "monitor",
        "label": i18n("Monitor"),
        "description": i18n("Assign a layout or engine to a monitor context."),
        "icon": "video-display"
    }, {
        "id": "desktop",
        "label": i18n("Desktop"),
        "description": i18n("Assign a layout or engine to a virtual desktop."),
        "icon": "preferences-desktop-virtual"
    }, {
        "id": "activity",
        "label": i18n("Activity"),
        "description": i18n("Assign a layout or engine to a Plasma activity."),
        "icon": "activities"
    }, {
        "id": "application",
        "label": i18n("Application"),
        "description": i18n("Float, exclude or tweak windows of a specific app."),
        "icon": "application-x-executable"
    }, {
        "id": "animation",
        "label": i18n("Animation"),
        "description": i18n("Override duration, curve or shader for an animation event."),
        "icon": "media-playback-start"
    }, {
        "id": "custom",
        "label": i18n("Custom"),
        "description": i18n("Build a composite AND / OR / NOT match expression."),
        "icon": "configure"
    }]
    // Tile HEIGHT is fixed (so tiles don't grow tall when one description is
    // shorter); tile WIDTH flexes via `Layout.fillWidth` on each tile so the
    // GridLayout distributes the column space evenly. This stops the
    // rightmost tile from overflowing the sheet on HiDPI displays where
    // gridUnit is larger and a hard-coded `tileWidth * 3` would exceed the
    // sheet's available width.
    readonly property int _tileHeight: Kirigami.Units.gridUnit * 9

    /// Emitted with a fresh rule JSON map once the user picks a starting
    /// point. The page opens the RuleEditorSheet on this.
    signal subjectChosen(var ruleJson)

    function _choose(subject) {
        // Close FIRST, then defer the emit one event-loop tick so the
        // hosting RuleEditorSheet doesn't open while this sheet's close
        // animation is still running — some Kirigami versions suppress
        // an OverlaySheet.open() that lands during another sheet's
        // close transition.
        var rule = sheet.controller.newEmptyRule(subject);
        sheet.close();
        Qt.callLater(function() {
            sheet.subjectChosen(rule);
        });
    }

    function _chooseTemplate(templateId) {
        var rule = sheet.controller.newRuleFromTemplate(templateId);
        sheet.close();
        Qt.callLater(function() {
            sheet.subjectChosen(rule);
        });
    }

    title: i18n("Add Window Rule")

    ColumnLayout {
        // No explicit `implicitWidth`: the GridLayout's columns get their
        // implicit width from RuleStartTile's own `implicitWidth`, the
        // ColumnLayout inherits that, and the OverlaySheet sizes itself
        // accordingly. A hard-coded width here previously fought the
        // sheet's internal sizing pass and either collapsed the content or
        // overflowed it on HiDPI.
        spacing: Kirigami.Units.largeSpacing

        // ── Quick-start templates ──
        Label {
            Layout.fillWidth: true
            font.bold: true
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            text: i18n("Quick start")
            visible: sheet._templates.length > 0
        }

        GridLayout {
            Layout.fillWidth: true
            columnSpacing: Kirigami.Units.largeSpacing
            columns: 3
            rowSpacing: Kirigami.Units.largeSpacing
            visible: sheet._templates.length > 0

            Repeater {
                model: sheet._templates

                delegate: RuleStartTile {
                    // Repeater exposes `modelData` as a context property —
                    // strict QML compilation requires the delegate to declare
                    // it explicitly before referencing it in bindings.
                    required property var modelData

                    Layout.fillWidth: true
                    description: modelData.description
                    iconSource: modelData.icon
                    label: modelData.label
                    tileHeight: sheet._tileHeight
                    onActivated: sheet._chooseTemplate(modelData.id)
                }

            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: sheet._templates.length > 0
        }

        // ── Subjects (start from scratch) ──
        Label {
            Layout.fillWidth: true
            font.bold: true
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            text: sheet._templates.length > 0 ? i18n("Or start from scratch") : i18n("What should this rule be about?")
        }

        GridLayout {
            Layout.fillWidth: true
            columnSpacing: Kirigami.Units.largeSpacing
            columns: 3
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: sheet._subjects

                delegate: RuleStartTile {
                    required property var modelData

                    Layout.fillWidth: true
                    description: modelData.description
                    iconSource: modelData.icon
                    label: modelData.label
                    tileHeight: sheet._tileHeight
                    onActivated: sheet._choose(modelData.id)
                }

            }

        }

    }

}
