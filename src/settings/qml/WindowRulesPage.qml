// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The unified Window Rules page.
 *
 * One surface for every window-matching rule — monitors, applications,
 * activities, animations. Backed by a single `WindowRuleModel` exposed by
 * `SettingsController.windowRulesPage`; section grouping, search and chip
 * filtering are all derived here in QML over that one model — there are no
 * per-section proxy models.
 *
 * The monitor overview strip is read-only; clicking a tile filters the list.
 */
SettingsFlickable {
    id: page

    readonly property var controller: settingsController.windowRulesPage
    readonly property var ruleModel: controller.model
    // ── Filter state ──
    property string searchText: ""
    // Chip filter: -1 = All, else a WindowRuleModel.Section enum value as int.
    property int chipFilter: -1
    property string monitorFilter: ""
    // Ordered section descriptors — `[{ value: int, label }]` straight from
    // the controller. The C++ Section enum order is never duplicated in QML.
    readonly property var sectionDescriptors: page.controller.sections()
    // Bumped whenever the underlying model changes so sectionModel re-evaluates
    // without QML hardcoding the model's role layout.
    property int modelRevision: 0
    /// Build a `[ { section, label, rules: [...] } ]` array over the flat
    /// model, applying search / chip / monitor filters. Reactive: depends on
    /// searchText / chipFilter / monitorFilter and `modelRevision`.
    readonly property var sectionModel: {
        // Touch the revision so the binding re-evaluates on any model change.
        var rev = page.modelRevision;
        var snapshot = page.controller.rulesSnapshot();
        var buckets = {
        };
        for (var s = 0; s < page.sectionDescriptors.length; ++s) buckets[page.sectionDescriptors[s].value] = []
        var search = page.searchText.toLowerCase();
        for (var i = 0; i < snapshot.length; ++i) {
            var entry = snapshot[i];
            // Chip filter.
            if (page.chipFilter >= 0 && entry.section !== page.chipFilter)
                continue;

            // Search filter — match name / match summary / action summary.
            if (search.length > 0) {
                var hay = (entry.name + " " + entry.matchSummary + " " + entry.actionSummary).toLowerCase();
                if (hay.indexOf(search) < 0)
                    continue;

            }
            // Monitor filter — keep only rules whose ScreenId predicate(s)
            // name the selected monitor (an exact id match, not a substring
            // scan of the localized summary).
            if (page.monitorFilter.length > 0) {
                if (!entry.screenIds || entry.screenIds.indexOf(page.monitorFilter) < 0)
                    continue;

            }
            if (buckets[entry.section] !== undefined)
                buckets[entry.section].push(entry);

        }
        var out = [];
        for (var so = 0; so < page.sectionDescriptors.length; ++so) {
            var sec = page.sectionDescriptors[so];
            if (buckets[sec.value].length === 0)
                continue;

            out.push({
                "section": sec.value,
                "label": sec.label,
                "rules": buckets[sec.value]
            });
        }
        return out;
    }

    contentHeight: mainCol.implicitHeight
    clip: true

    // Re-derive the section model on every structural or per-rule change so a
    // rule that changes section is re-bucketed.
    Connections {
        function onCountChanged() {
            page.modelRevision++;
        }

        function onRuleSectionChanged() {
            page.modelRevision++;
        }

        function onDataChanged() {
            page.modelRevision++;
        }

        target: page.ruleModel
    }

    AddRuleSheet {
        id: addRuleSheet

        controller: page.controller
        onSubjectChosen: function(ruleJson) {
            ruleEditorSheet.openFor(ruleJson, false);
        }
    }

    RuleEditorSheet {
        id: ruleEditorSheet

        controller: page.controller
        onRuleSaved: function(ruleJson) {
            if (editing)
                page.controller.updateRuleFromJson(ruleJson);
            else
                page.controller.addRuleFromJson(ruleJson);
        }
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: !page.controller.daemonReachable
            text: i18n("The PlasmaZones daemon is not running — window rules cannot be loaded or saved until it starts.")
        }

        // ── Monitor overview strip ──
        SettingsCard {
            Layout.fillWidth: true

            contentItem: MonitorOverview {
                tiles: page.controller.monitorOverview(settingsController.screens)
                selectedScreenId: page.monitorFilter
                onMonitorSelected: function(screenId) {
                    page.monitorFilter = screenId;
                }
            }

        }

        // ── Search + Add rule ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                Layout.fillWidth: true
                placeholderText: i18n("Search rules…")
                Accessible.name: i18n("Search window rules")
                onTextChanged: page.searchText = text
            }

            Button {
                text: i18n("Add rule")
                icon.name: "list-add"
                Accessible.name: i18n("Add a new window rule")
                onClicked: addRuleSheet.open()
            }

        }

        // ── Filter chips ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Filter:")
                opacity: 0.7
            }

            Repeater {
                // "All" chip prepended to the controller's section list — the
                // section labels and order come from C++, not hardcoded here.
                model: [{
                    "value": -1,
                    "label": i18n("All")
                }].concat(page.sectionDescriptors)

                delegate: Button {
                    required property var modelData

                    text: modelData.label
                    checkable: true
                    checked: page.chipFilter === modelData.value
                    Accessible.name: i18n("Filter rules: %1", modelData.label)
                    onClicked: page.chipFilter = modelData.value
                }

            }

            Item {
                Layout.fillWidth: true
            }

        }

        // ── Empty state ──
        Kirigami.PlaceholderMessage {
            // When the daemon is unreachable the model is empty because it
            // could not be loaded — not because the user has no rules. Gate
            // the "No window rules yet" copy on daemonReachable so the inline
            // warning above is the only thing shown in that case.
            readonly property bool _daemonDown: !page.controller.daemonReachable
            readonly property bool _trulyEmpty: page.ruleModel.count === 0 && !_daemonDown

            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: page.sectionModel.length === 0 && !_daemonDown
            icon.name: "view-list-details"
            text: _trulyEmpty ? i18n("No window rules yet") : i18n("No rules match the current filter")
            explanation: _trulyEmpty ? i18n("Add a rule to assign layouts to monitors, float application windows, or override animations.") : i18n("Try a different filter or search term.")
        }

        // ── Grouped rule list ──
        Repeater {
            model: page.sectionModel

            delegate: ColumnLayout {
                required property var modelData

                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing

                    Label {
                        text: modelData.label
                        font.bold: true
                        opacity: 0.6
                        font.capitalization: Font.AllUppercase
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Label {
                        text: i18np("%1 rule", "%1 rules", modelData.rules.length)
                        opacity: 0.5
                    }

                }

                SettingsCard {
                    Layout.fillWidth: true

                    contentItem: ColumnLayout {
                        spacing: 0

                        Repeater {
                            model: modelData.rules

                            delegate: WindowRuleRow {
                                required property var modelData

                                Layout.fillWidth: true
                                ruleId: modelData.ruleId
                                ruleName: modelData.name
                                enabled: modelData.enabled
                                matchSummary: modelData.matchSummary
                                actionSummary: modelData.actionSummary
                                conditionCount: modelData.conditionCount
                                actionCount: modelData.actionCount
                                isComposite: modelData.isComposite
                                onToggleRequested: function(en) {
                                    page.controller.setRuleEnabled(ruleId, en);
                                }
                                onEditRequested: {
                                    ruleEditorSheet.openFor(page.controller.ruleJson(ruleId), true);
                                }
                                onDeleteRequested: {
                                    page.controller.removeRule(ruleId);
                                }
                            }

                        }

                    }

                }

            }

        }

    }

}
