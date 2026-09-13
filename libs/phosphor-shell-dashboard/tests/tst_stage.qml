// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Dashboard

TestCase {
    id: testCase
    name: "StageOverview"
    when: windowShown
    visible: true
    width: 1440
    height: 900
    Component {
        id: mapComponent
        QtObject {
            property var windows: [
                {
                    id: "a",
                    windowId: "a",
                    title: "Editor",
                    focused: true
                },
                {
                    id: "b",
                    windowId: "b",
                    title: "Browser"
                }
            ]
            property var cells: []
            property int mode: 1
            property var lens: ({})
            property int overflowLeft: 0
            property int overflowRight: 0
            property int stripExtentPx: 0
            property real aspect: 1.6
            property var menuModel: []
            property string activated: ""
            signal changed
            function refreshMenu() {
            }
            function activateNavigationWindow(id) {
                activated = id;
            }
        }
    }
    Component {
        id: stageComponent
        StageOverview {
            width: testCase.width
            height: testCase.height
        }
    }
    function test_selectionIsSeparateFromActivation() {
        const map = createTemporaryObject(mapComponent, testCase);
        const stage = createTemporaryObject(stageComponent, testCase, {
            mapFor: () => map,
            open: true
        });
        waitForRendering(stage);
        stage.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(stage.selectedId, "b");
        compare(map.activated, "");
        keyClick(Qt.Key_Return);
        compare(map.activated, "b");
    }
    function test_previewFitsAndPreservesAspect() {
        const stage = createTemporaryObject(stageComponent, testCase);
        for (const size of [[1440, 900], [800, 600], [900, 1440]]) {
            stage.width = size[0];
            stage.height = size[1];
            verify(stage.previewRect.x > 0);
            verify(stage.previewRect.y > 0);
            verify(stage.previewRect.x + stage.previewRect.width < stage.width);
            verify(stage.previewRect.y + stage.previewRect.height < stage.height - 80);
            fuzzyCompare(stage.previewRect.width / stage.previewRect.height, stage.width / stage.height, 0.001);
        }
    }
}
