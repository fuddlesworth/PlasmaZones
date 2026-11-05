// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// The Launcher surface's keyboard contract, against a fake model.
//
// Everything the user does happens from the search field: Up/Down move
// the selection, Return and Alt+Return activate, Tab cycles the provider
// filter, Escape dismisses. These pin that each key reaches the model the
// way the header promises, and that a refused activation keeps the
// surface open (no `activated`). Rendering is not asserted.
//
// The fake is a ListModel (so the ListView can bind it) carrying the
// LauncherModel properties and invokables on top; the surface reads
// nothing beyond that contract.

import QtQuick
import QtTest
import Phosphor.Launcher

TestCase {
    id: testCase

    name: "PhosphorLauncherKeys"
    when: windowShown
    visible: true
    width: 700
    height: 600

    Component {
        id: fakeResults

        ListModel {
            id: fake

            property string query: ""
            property string providerFilter: ""
            property var providers: [
                {
                    "id": "apps",
                    "name": "Apps",
                    "iconName": "applications-all",
                    "count": 2
                }
            ]
            // Recorded calls.
            property var activations: []
            property var cycles: []
            property bool acceptActivation: true

            // Mirrors the real model. The surface asks this before a
            // destructive alternate action, so that it can decide whether to
            // stay open without reading an index the action just invalidated.
            property bool repeatableAlternate: false

            function alternateIsRepeatable(row: int): bool {
                return fake.repeatableAlternate;
            }

            function activate(row: int, alternate: bool): bool {
                activations.push({
                    "row": row,
                    "alternate": alternate
                });
                activationsChanged();
                return acceptActivation;
            }

            function cycleProviderFilter(direction: int): void {
                cycles.push(direction);
                cyclesChanged();
            }

            ListElement {
                title: "Firefox"
                subtitle: "Web Browser"
                iconName: "firefox"
                providerId: "apps"
                providerName: "Apps"
                providerIcon: "applications-all"
                resultId: "firefox"
                primaryActionLabel: "Open"
                alternateActionLabel: ""
                hasAlternateAction: false
                score: 100
            }

            ListElement {
                title: "sh -c true"
                subtitle: "Run as a shell command"
                iconName: "utilities-terminal"
                providerId: "command"
                providerName: "Run Command"
                providerIcon: "utilities-terminal"
                resultId: "run"
                primaryActionLabel: "Run"
                alternateActionLabel: "Run in terminal"
                hasAlternateAction: true
                score: 1
            }
        }
    }

    Component {
        id: launcherComp

        Launcher {}
    }

    function makeLauncher() {
        const results = createTemporaryObject(fakeResults, testCase);
        const launcher = createTemporaryObject(launcherComp, testCase, {
            "results": results,
            "width": 640
        });
        verify(launcher, "Launcher instantiates");
        launcher.reset();
        waitForRendering(launcher);
        return {
            "launcher": launcher,
            "results": results
        };
    }

    function test_reset_clears_and_focuses() {
        const t = makeLauncher();
        // Type it, so the FIELD holds the stale text too. Setting only the
        // model's query left the field empty, which is why a reset that
        // cleared the model and not the field passed this case: the next
        // keystroke then re-sent the stale prefix.
        keyClick(Qt.Key_S);
        keyClick(Qt.Key_T);
        compare(t.launcher.queryText, "st", "the field holds what was typed");
        t.results.providerFilter = "apps";

        t.launcher.reset();
        compare(t.launcher.queryText, "", "reset clears the field, not just the model");
        compare(t.results.query, "", "reset clears the query");
        compare(t.results.providerFilter, "", "reset clears the filter");
        // Typed, not inspected. reset() promises the SEARCH FIELD has the
        // keyboard, and asserting the surface root's focus passed even when
        // the field itself was left dead. A keystroke reaching the query is
        // the only proof that matters.
        keyClick(Qt.Key_G);
        compare(t.launcher.queryText, "g", "the field takes the next keystroke");
    }

    function test_typing_pushes_the_query() {
        const t = makeLauncher();
        keyClick(Qt.Key_F);
        keyClick(Qt.Key_I);
        compare(t.results.query, "fi", "each keystroke updates the model's query");
    }

    function test_return_activates_the_selection_and_emits_activated() {
        const t = makeLauncher();
        const activated = createTemporaryObject(spyComp, testCase, {
            "target": t.launcher,
            "signalName": "activated"
        });
        keyClick(Qt.Key_Return);
        compare(t.results.activations.length, 1, "one activation");
        compare(t.results.activations[0].row, 0, "the top row");
        compare(t.results.activations[0].alternate, false, "primary action");
        compare(activated.count, 1, "activated emitted for the host to close");
    }

    function test_down_then_alt_return_takes_the_alternate() {
        const t = makeLauncher();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return, Qt.AltModifier);
        compare(t.results.activations.length, 1);
        compare(t.results.activations[0].row, 1, "moved to the second row");
        compare(t.results.activations[0].alternate, true, "Alt+Return is the alternate");
    }

    // The clipboard's remove is repeatable: pruning history is something a
    // user does several times in a row, and closing after the first meant
    // reopening and retyping to remove the second.
    function test_a_repeatable_alternate_action_leaves_the_surface_open() {
        const t = makeLauncher();
        const activated = createTemporaryObject(spyComp, testCase, {
            "target": t.launcher,
            "signalName": "activated"
        });
        t.results.repeatableAlternate = true;

        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return, Qt.AltModifier);
        compare(t.results.activations.length, 1, "the action still happened");
        compare(activated.count, 0, "but the surface stays open for the next one");

        // The PRIMARY action always finishes the interaction, repeatable
        // alternate or not.
        keyClick(Qt.Key_Return);
        compare(activated.count, 1, "the primary action still closes");
    }

    function test_up_never_goes_above_the_first_row() {
        const t = makeLauncher();
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Return);
        compare(t.results.activations[0].row, 0, "clamped at the top");
        // And Down clamps at the bottom.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(t.results.activations[1].row, 1, "clamped at the last row");
    }

    function test_refused_activation_keeps_the_surface_open() {
        const t = makeLauncher();
        const activated = createTemporaryObject(spyComp, testCase, {
            "target": t.launcher,
            "signalName": "activated"
        });
        t.results.acceptActivation = false;
        keyClick(Qt.Key_Return);
        compare(t.results.activations.length, 1, "the model was asked");
        compare(activated.count, 0, "but a refusal does not close the surface");
    }

    function test_tab_and_backtab_cycle_the_filter() {
        const t = makeLauncher();
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Backtab);
        compare(t.results.cycles, [1, -1], "Tab forward, Shift+Tab back");
    }

    // The pointer paths, which the keyboard cases above never reach. Both
    // move state the keyboard also moves, so a regression in either is
    // invisible to a suite that only types.
    function test_clicking_a_row_selects_and_activates_it() {
        const t = makeLauncher();
        const activated = createTemporaryObject(spyComp, testCase, {
            "target": t.launcher,
            "signalName": "activated"
        });
        const list = findChild(t.launcher, "resultList");
        verify(list, "the result list is reachable");
        const second = list.itemAtIndex(1);
        verify(second, "the second row exists");

        mouseClick(second);
        compare(t.results.activations.length, 1, "the click activated");
        compare(t.results.activations[0].row, 1, "the row that was clicked, not the selected one");
        compare(t.results.activations[0].alternate, false, "a click is the primary action");
        compare(activated.count, 1, "and the host is told to close");
    }

    function test_clicking_a_provider_pill_filters() {
        const t = makeLauncher();
        const pills = findChild(t.launcher, "providerPills");
        verify(pills, "the pill strip is reachable");
        // The first pill is All, the second is the one provider the fake
        // reports.
        const providerPill = pills.children[1];
        verify(providerPill, "a provider pill exists");

        mouseClick(providerPill);
        compare(t.results.providerFilter, "apps", "the pill set the filter");

        mouseClick(pills.children[0]);
        compare(t.results.providerFilter, "", "All clears it again");
    }

    function test_escape_dismisses() {
        const t = makeLauncher();
        const dismissed = createTemporaryObject(spyComp, testCase, {
            "target": t.launcher,
            "signalName": "dismissed"
        });
        keyClick(Qt.Key_Escape);
        compare(dismissed.count, 1, "Escape emits dismissed");
    }

    Component {
        id: spyComp

        SignalSpy {}
    }
}
