// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Widgets

TestCase {
    id: testCase
    name: "WorkspaceNavigator"
    when: windowShown
    visible: true
    width: 680
    height: 450
    Component {
        id: navigatorComponent
        WorkspaceNavigator {
            width: 620
            height: 300
        }
    }
    Component {
        id: mapComponent
        QtObject {
            property int mode: 2
            property var windows: []
            property var cells: []
            property var lens: ({
                    x: 0,
                    w: 0.2
                })
            property int overflowLeft: 0
            property int overflowRight: 17
            property int stripExtentPx: 10000
            property real aspect: 1.8
            property string activated: ""
            signal changed
            function activateNavigationWindow(id) {
                activated = id;
            }
        }
    }
    function populate(count) {
        const map = createTemporaryObject(mapComponent, testCase);
        let windows = [];
        for (let i = 0; i < count; i++)
            windows.push({
                id: "w" + i,
                windowId: "w" + i,
                title: "Document " + i,
                appId: "editor",
                focused: i === 0,
                occupied: true,
                t: i / count,
                x: i / 3,
                y: 0,
                w: 1 / 3,
                h: 1,
                stack: 1,
                offscreen: i > 2
            });
        map.windows = windows;
        map.cells = windows.slice(0, 3);
        return map;
    }
    function test_manyWindowsStayBoundedAndKeyboardReachesLast() {
        const map = populate(20);
        const nav = createTemporaryObject(navigatorComponent, testCase, {
            map: map
        });
        waitForRendering(nav);
        nav.forceActiveFocus();
        tryCompare(nav, "selectedIndex", 0);
        keyClick(Qt.Key_End);
        compare(nav.selectedId, "w19");
        const strip = findChild(nav, "windowStrip");
        const titles = findChild(nav, "windowTitles");
        verify(strip.contentX > 0);
        verify(titles.contentY > 0);
        verify(titles.height <= 250);
        compare(strip.count, 20);
        verify(strip.itemAtIndex(19).width >= 104);
        keyClick(Qt.Key_Return);
        compare(map.activated, "w19");
        keyClick(Qt.Key_Home);
        compare(nav.selectedId, "w0");
    }
    function test_selectionSurvivesRefreshAndRecoversFromClosedWindow() {
        const map = populate(10);
        const nav = createTemporaryObject(navigatorComponent, testCase, {
            map: map,
            selectOnly: true
        });
        nav.choose(8);
        compare(map.activated, "");
        compare(nav.selectedId, "w8");
        map.windows = map.windows.slice(1);
        tryCompare(nav, "selectedIndex", 7);
        map.windows = map.windows.slice(0, 4);
        tryCompare(nav, "selectedIndex", 0);
        map.windows = [];
        tryCompare(nav, "selectedId", "");
        nav.openSelection();
        compare(map.activated, "");
    }
}
