// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls

/**
 * @brief The routing-target pickers for ActionRow, split out of
 *        ActionParamEditors.
 *
 * The three actions that route a window somewhere (RouteToScreen,
 * RouteToDesktop, RouteToWorkspace) each need a combo showing friendly labels
 * over a wire value the daemon resolves, and all three keep a stored value
 * legible once it no longer matches anything live. They moved here together
 * when ActionParamEditors reached the file-size ceiling; the split is a home
 * for the Components, not a behavioural boundary.
 *
 * Each Component body is UNCHANGED from when it lived next door: it still
 * reads `row.*` (the ActionRow instance, injected below) and
 * `parent.modelData` (the hosting Loader's param descriptor, reached at
 * runtime). ActionRow resolves these through `paramEditors.pickers._…Editor`.
 */
QtObject {
    id: pickers

    /// The ActionRow these pickers belong to. Named `row` for the same reason
    /// ActionParamEditors names it that: the moved Component bodies resolve
    /// `row.action` / `row.actionEdited` / `row._withParam` against it exactly
    /// as they did before, with no rewrites.
    required property var row

    // Monitor picker for RouteToScreen. Mirrors the ScreenId match-condition
    // editor (MatchLeafEditor's screenValueEditor): the user picks a friendly
    // label while the wire value stays the canonical screen id. A stored id whose
    // monitor is offline still surfaces (so the user sees what the rule pins to).
    property Component _screenIdEditor: Component {
        WideComboBox {
            id: screenCombo

            readonly property var _param: parent.modelData
            readonly property var _screens: (row.appSettings && row.appSettings.screens) || []
            model: _screens.map(function (s) {
                var label = s.displayLabel || s.name || "";
                if (s.isPrimary)
                    // Composed inside one i18nc so translators control the
                    // order and the separator survives RTL bidi runs.
                    label = i18nc("monitor name, then the primary-monitor marker", "%1 · %2", label, i18n("Primary"));
                return {
                    "label": label,
                    "name": s.name
                };
            })
            textRole: "label"
            valueRole: "name"
            currentIndex: {
                var target = row.action[_param.key] || "";
                for (var i = 0; i < screenCombo._screens.length; ++i) {
                    if (screenCombo._screens[i].name === target)
                        return i;
                }
                return -1;
            }
            displayText: currentIndex >= 0 ? currentText : (row.action[_param.key] || i18n("Choose a monitor…"))
            Accessible.name: _param.label
            onActivated: function (index) {
                if (currentValue !== row.action[_param.key])
                    row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Virtual-desktop picker for RouteToDesktop. Lists 1..virtualDesktopCount,
    // labelled with the desktop name when KWin reports one. The wire value is the
    // 1-based desktop number. A stored desktop beyond the current count still
    // surfaces its number so the rule's target stays legible.
    property Component _virtualDesktopEditor: Component {
        WideComboBox {
            id: desktopCombo

            readonly property var _param: parent.modelData
            readonly property int _count: row.appSettings && row.appSettings.virtualDesktopCount > 0 ? row.appSettings.virtualDesktopCount : 1
            readonly property var _names: (row.appSettings && row.appSettings.virtualDesktopNames) || []
            model: {
                var items = [];
                for (var i = 1; i <= desktopCombo._count; ++i) {
                    var name = desktopCombo._names.length >= i ? desktopCombo._names[i - 1] : "";
                    items.push({
                        // Composed inside one i18nc so translators control the
                        // order and the separator, matching the monitor picker
                        // above and the match-side desktop picker.
                        "label": name && name.length > 0 ? i18nc("virtual desktop number, then its name", "%1: %2", i, name) : ("" + i),
                        "value": i
                    });
                }
                return items;
            }
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = Number(row.action[_param.key]);
                for (var i = 0; i < desktopCombo.model.length; ++i) {
                    if (desktopCombo.model[i].value === target)
                        return i;
                }
                return -1;
            }
            displayText: currentIndex >= 0 ? currentText : (row.action[_param.key] ? ("" + row.action[_param.key]) : i18n("Choose a desktop…"))
            Accessible.name: _param.label
            onActivated: function (index) {
                if (currentValue !== row.action[_param.key])
                    row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Named-workspace picker for RouteToWorkspace. Lists the DECLARED names
    // (Workspaces → Named Workspaces); the wire value is the name itself. A
    // stored name no longer declared still surfaces as the display text so
    // the rule's target stays legible — the rule is dormant, not broken.
    property Component _workspaceNameEditor: Component {
        WideComboBox {
            id: workspaceCombo

            readonly property var _param: parent.modelData
            readonly property var _names: (row.appSettings && row.appSettings.workspaceNames) || []

            model: {
                var items = [];
                for (var i = 0; i < workspaceCombo._names.length; ++i)
                    items.push({
                        "label": workspaceCombo._names[i],
                        "value": workspaceCombo._names[i]
                    });
                return items;
            }
            textRole: "label"
            valueRole: "value"
            // Nothing to choose until a workspace is declared, and the action
            // validator refuses a blank name (hasNonBlankStringWithin, in
            // ruleaction_builtins_engine.cpp), so there is no empty value to
            // offer as a fallback either. The combo stays ENABLED with an
            // empty model all the same: disabling it puts an already-stored
            // name out of reach, and a disabled QQC2 Control receives no hover
            // events, so the tooltip that says where to declare one would
            // never appear. The display text carries the same guidance for
            // anyone who does not hover.
            // Whether workspaces are on at all comes first. With the feature
            // off the daemon never installs the route resolver, so the rule is
            // inert whatever it names, and pointing the user at the Named
            // Workspaces page would send them somewhere that cannot help.
            readonly property bool _workspacesOff: row.appSettings && row.appSettings.workspacesEnabled === false

            ToolTip.visible: hovered && (workspaceCombo._workspacesOff || workspaceCombo._names.length === 0)
            ToolTip.text: workspaceCombo._workspacesOff ? i18n("Dynamic workspaces are turned off, so this action does nothing. Turn them on under Settings → Workspaces.") : i18n("Add a named workspace under Settings → Workspaces → Named Workspaces first.")
            currentIndex: {
                var target = row.action[_param.key];
                for (var i = 0; i < workspaceCombo.model.length; ++i) {
                    if (workspaceCombo.model[i].value === target)
                        return i;
                }
                return -1;
            }
            displayText: {
                if (currentIndex >= 0)
                    return currentText;
                if (row.action[_param.key])
                    return "" + row.action[_param.key];
                if (workspaceCombo._workspacesOff)
                    return i18n("Workspaces are turned off");
                if (workspaceCombo._names.length === 0)
                    return i18n("Add a named workspace first");
                return i18n("Choose a workspace…");
            }
            Accessible.name: _param.label
            onActivated: function (index) {
                if (currentValue !== row.action[_param.key])
                    row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }
}
