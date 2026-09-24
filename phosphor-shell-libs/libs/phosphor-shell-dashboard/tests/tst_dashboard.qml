// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Dashboard over fakes: the grid holds one cell per desktop plus the
// fixed cells (and the `+` only when a create verb exists), Escape asks
// to close, and a desktop click switches through the cell's map.

import QtQuick
import QtTest
import Phosphor.Dashboard

TestCase {
    id: testCase

    name: "Dashboard"
    width: 1000
    height: 500
    when: windowShown
    // TestCase is `visible: false`; the dashboard visibility test reads a
    // child's effective visibility, so the harness must be shown.
    visible: true

    // The Workspaces contract the dashboard reads.
    Component {
        id: workspacesComp

        QtObject {
            property int count: 3
            property int activeIndex: 2
            property var model: null
            property string switchedTo: ""
            function switchTo(id) {
                switchedTo = id;
            }
        }
    }

    // With a create verb, for the `+` cell.
    Component {
        id: creatingWorkspacesComp

        QtObject {
            property int count: 1
            property int activeIndex: 1
            property var model: null
            property int created: 0
            function switchTo(id) {
            }
            function create() {
                created++;
            }
        }
    }

    // A PlacementMapScreen stand-in per desktop.
    Component {
        id: fakeMapComp

        QtObject {
            property int index: -1
            property int mode: 0
            property real aspect: 2
            property var cells: []
            property var lens: ({})
            property int overflowLeft: 0
            property int overflowRight: 0
            property int stripExtentPx: 0
            property int desktopCount: 3
            property int currentDesktop: 1
            property bool urgent: false
            property rect workArea: Qt.rect(0, 0, 1000, 500)
            property int switchedTo: -1

            signal changed

            function focusedCellId() {
                for (let i = 0; i < cells.length; ++i) {
                    if (cells[i].focused)
                        return cells[i].id;
                }
                return "";
            }
            function switchDesktop(i) {
                switchedTo = i;
            }
        }
    }

    Component {
        id: dashboardComp

        Dashboard {
            width: 1000
            height: 500
        }
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    property var maps: ({})

    function mapFor(index) {
        if (!maps[index])
            maps[index] = fakeMapComp.createObject(testCase, {
                "index": index
            });
        return maps[index];
    }

    function init() {
        maps = {};
    }

    function test_grid_count_is_desktops_plus_fixed_cells() {
        const ws = createTemporaryObject(workspacesComp, testCase);
        const d = createTemporaryObject(dashboardComp, testCase, {
            "workspaces": ws,
            "mapFor": mapFor,
            "open": true
        });
        compare(d.desktopCount, 3);
        compare(d.fixedCount, 2, "calendar and media; no `+` without a create verb");
        compare(d.gridCount, 5);
        compare(d.currentDesktop, 1);
        // A desktop change follows.
        ws.count = 5;
        compare(d.gridCount, 7);
    }

    function test_plus_cell_only_with_a_create_verb() {
        const ws = createTemporaryObject(creatingWorkspacesComp, testCase);
        const d = createTemporaryObject(dashboardComp, testCase, {
            "workspaces": ws,
            "mapFor": mapFor,
            "open": true
        });
        verify(d.canCreate);
        compare(d.fixedCount, 3);
        compare(d.gridCount, 4);
    }

    function test_escape_asks_to_close_and_goto_switches_through_the_map() {
        const ws = createTemporaryObject(workspacesComp, testCase);
        const d = createTemporaryObject(dashboardComp, testCase, {
            "workspaces": ws,
            "mapFor": mapFor,
            "open": true
        });
        const spy = createTemporaryObject(spyComp, testCase, {
            "target": d,
            "signalName": "closeRequested"
        });
        d.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        compare(spy.count, 1, "Escape asks the host to close");

        d.goTo(2);
        compare(mapFor(2).switchedTo, 2, "the switch goes through the cell's map");
        compare(spy.count, 2, "and closes");

        // Out of range: closes without switching.
        d.goTo(9);
        compare(spy.count, 3);
        verify(maps[9] === undefined);
    }

    function test_open_drives_progress_to_one_and_back() {
        const ws = createTemporaryObject(workspacesComp, testCase);
        const d = createTemporaryObject(dashboardComp, testCase, {
            "workspaces": ws,
            "mapFor": mapFor
        });
        compare(d.progress, 0);
        verify(!d.visible);
        d.open = true;
        tryCompare(d, "progress", 1);
        verify(d.visible);
        d.open = false;
        tryCompare(d, "progress", 0);
        tryCompare(d, "visible", false);
    }
}
