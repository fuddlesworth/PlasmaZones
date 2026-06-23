// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief "Choose a starting point" tile grid for the new-rule wizard.
 *
 * Two sections: pre-fab templates (a fully-formed rule with actions seeded)
 * on top, then the bare subjects (an empty rule pre-shaped for one match
 * dimension). Activating a tile emits `chosen(ruleJson)`; the parent wizard
 * stages that rule in its step-2 working-rule.
 */
ColumnLayout {
    id: root

    /// The WindowRuleController — supplies `ruleTemplates()` and the
    /// `newEmptyRule(subject)` / `newRuleFromTemplate(id)` constructors.
    required property var controller
    // Cache once — `ruleTemplates()` allocates a fresh QVariantList on every
    // call; the Repeater would re-evaluate on every unrelated binding tick.
    readonly property var _templates: root.controller.ruleTemplates()
    // Static subject catalogue — kept in QML because the labels and
    // descriptions are pure UI text and the icons are freedesktop ids the
    // controller doesn't need to know about. Order matches AddRuleSheet's
    // pre-wizard convention: context-based subjects first (the "where" axis),
    // then window-based (the "what" axis).
    readonly property var _subjects: [
        {
            "id": "monitor",
            "label": i18n("Monitor"),
            "description": i18n("Assign a layout or engine to a monitor context."),
            "icon": "video-display"
        },
        {
            "id": "desktop",
            "label": i18n("Desktop"),
            "description": i18n("Assign a layout or engine to a virtual desktop."),
            "icon": "preferences-desktop-virtual"
        },
        {
            "id": "activity",
            "label": i18n("Activity"),
            "description": i18n("Assign a layout or engine to a Plasma activity."),
            "icon": "activities"
        },
        {
            "id": "application",
            "label": i18n("Application"),
            "description": i18n("Float, exclude or tweak windows of a specific app."),
            "icon": "application-x-executable"
        },
        {
            "id": "animation",
            "label": i18n("Animation"),
            "description": i18n("Override duration, curve or shader for an animation event."),
            "icon": "media-playback-start"
        },
        {
            "id": "custom",
            "label": i18n("Custom"),
            "description": i18n("Build a composite AND / OR / NOT match expression."),
            "icon": "configure"
        }
    ]
    readonly property int _tileHeight: Kirigami.Units.gridUnit * 9

    /// Emitted with the freshly-stamped rule JSON when a tile is activated.
    /// The parent stages the rule and advances to step 2 — the picker does
    /// not own working state.
    signal chosen(string kind, string id, var ruleJson)

    function _chooseTemplate(templateId) {
        var rule = root.controller.newRuleFromTemplate(templateId);
        root.chosen("template", templateId, rule);
    }

    function _chooseSubject(subjectId) {
        var rule = root.controller.newEmptyRule(subjectId);
        root.chosen("subject", subjectId, rule);
    }

    spacing: Kirigami.Units.largeSpacing

    // ── Quick-start templates ──
    Label {
        Layout.fillWidth: true
        font.bold: true
        font.capitalization: Font.AllUppercase
        opacity: 0.7
        text: i18n("Quick start")
        visible: root._templates.length > 0
    }

    GridLayout {
        Layout.fillWidth: true
        columnSpacing: Kirigami.Units.largeSpacing
        columns: 3
        rowSpacing: Kirigami.Units.largeSpacing
        visible: root._templates.length > 0

        Repeater {
            model: root._templates

            delegate: RuleStartTile {
                required property var modelData

                Layout.fillWidth: true
                description: modelData.description
                iconSource: modelData.icon
                label: modelData.label
                tileHeight: root._tileHeight
                onActivated: root._chooseTemplate(modelData.id)
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        visible: root._templates.length > 0
    }

    // ── Subjects (start from scratch) ──
    Label {
        Layout.fillWidth: true
        font.bold: true
        font.capitalization: Font.AllUppercase
        opacity: 0.7
        text: root._templates.length > 0 ? i18n("Or start from scratch") : i18n("What should this rule be about?")
    }

    GridLayout {
        Layout.fillWidth: true
        columnSpacing: Kirigami.Units.largeSpacing
        columns: 3
        rowSpacing: Kirigami.Units.largeSpacing

        Repeater {
            model: root._subjects

            delegate: RuleStartTile {
                required property var modelData

                Layout.fillWidth: true
                description: modelData.description
                iconSource: modelData.icon
                label: modelData.label
                tileHeight: root._tileHeight
                onActivated: root._chooseSubject(modelData.id)
            }
        }
    }
}
