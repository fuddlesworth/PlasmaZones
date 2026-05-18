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
    // Chip filter: "" = All, else a WindowRuleModel.Section enum value as int
    // serialized to a string ("0".."4"), or "" for All.
    property int chipFilter: -1
    property string monitorFilter: ""
    // WindowRuleModel.Section enum values (kept in sync with the C++ enum).
    readonly property int sectionMonitor: 0
    readonly property int sectionApplication: 1
    readonly property int sectionActivity: 2
    readonly property int sectionAnimation: 3
    readonly property int sectionAdvanced: 4
    // Section display order — Monitor & Layout first, Advanced last.
    readonly property var _sectionOrder: [sectionMonitor, sectionApplication, sectionActivity, sectionAnimation, sectionAdvanced]
    /// Build a `[ { section, label, rules: [...] } ]` array over the flat
    /// model, applying search / chip / monitor filters. Reactive: depends on
    /// searchText / chipFilter / monitorFilter and the model's count so an
    /// add/remove rebuilds it.
    readonly property var sectionModel: {
        // Touch the count so the binding re-evaluates on structural change.
        var n = page.ruleModel.count;
        var buckets = {
        };
        for (var s = 0; s < page._sectionOrder.length; ++s) buckets[page._sectionOrder[s]] = []
        var search = page.searchText.toLowerCase();
        for (var i = 0; i < n; ++i) {
            var idx = page.ruleModel.index(i, 0);
            var entry = {
                "ruleId": page.ruleModel.data(idx, Qt.UserRole + 1),
                "name": page.ruleModel.data(idx, Qt.UserRole + 2),
                "enabled": page.ruleModel.data(idx, Qt.UserRole + 3),
                "section": page.ruleModel.data(idx, Qt.UserRole + 5),
                "matchSummary": page.ruleModel.data(idx, Qt.UserRole + 7),
                "actionSummary": page.ruleModel.data(idx, Qt.UserRole + 8),
                "conditionCount": page.ruleModel.data(idx, Qt.UserRole + 9),
                "actionCount": page.ruleModel.data(idx, Qt.UserRole + 10),
                "isComposite": page.ruleModel.data(idx, Qt.UserRole + 11)
            };
            // Chip filter.
            if (page.chipFilter >= 0 && entry.section !== page.chipFilter)
                continue;

            // Search filter — match name / match summary / action summary.
            if (search.length > 0) {
                var hay = (entry.name + " " + entry.matchSummary + " " + entry.actionSummary).toLowerCase();
                if (hay.indexOf(search) < 0)
                    continue;

            }
            // Monitor filter — keep only rules whose match summary names the
            // monitor (the C++ summary renders `Monitor: <id>`).
            if (page.monitorFilter.length > 0) {
                if (entry.matchSummary.indexOf(page.monitorFilter) < 0)
                    continue;

            }
            buckets[entry.section].push(entry);
        }
        var out = [];
        for (var so = 0; so < page._sectionOrder.length; ++so) {
            var sec = page._sectionOrder[so];
            if (buckets[sec].length === 0)
                continue;

            out.push({
                "section": sec,
                "label": page._sectionLabel(sec),
                "rules": buckets[sec]
            });
        }
        return out;
    }

    function _sectionLabel(section) {
        switch (section) {
        case page.sectionMonitor:
            return i18n("Monitor & Layout");
        case page.sectionApplication:
            return i18n("Applications");
        case page.sectionActivity:
            return i18n("Activities");
        case page.sectionAnimation:
            return i18n("Animations");
        case page.sectionAdvanced:
            return i18n("Advanced / Custom");
        }
        return "";
    }

    contentHeight: mainCol.implicitHeight
    clip: true

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
                model: [{
                    "value": -1,
                    "label": i18n("All")
                }, {
                    "value": page.sectionMonitor,
                    "label": i18n("Monitor")
                }, {
                    "value": page.sectionApplication,
                    "label": i18n("Application")
                }, {
                    "value": page.sectionActivity,
                    "label": i18n("Activity")
                }, {
                    "value": page.sectionAnimation,
                    "label": i18n("Animation")
                }, {
                    "value": page.sectionAdvanced,
                    "label": i18n("Advanced")
                }]

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
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: page.sectionModel.length === 0
            icon.name: "view-list-details"
            text: page.ruleModel.count === 0 ? i18n("No window rules yet") : i18n("No rules match the current filter")
            explanation: page.ruleModel.count === 0 ? i18n("Add a rule to assign layouts to monitors, float application windows, or override animations.") : i18n("Try a different filter or search term.")
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
