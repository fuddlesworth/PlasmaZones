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
            property rect workArea: Qt.rect(0, 76, 1440, 824)
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
    function test_workAreaMapsToPreviewAcrossOutputShapes() {
        const map = createTemporaryObject(mapComponent, testCase);
        const stage = createTemporaryObject(stageComponent, testCase, {
            mapFor: () => map
        });
        for (const size of [[1440, 900], [800, 600], [900, 1440]]) {
            stage.width = size[0];
            stage.height = size[1];
            map.workArea = Qt.rect(0, 76, stage.width, stage.height - 76);
            verify(stage.previewRect.x > 0);
            verify(stage.previewRect.y > 0);
            verify(stage.previewRect.x + stage.previewRect.width < stage.width);
            verify(stage.previewRect.y + stage.previewRect.height < stage.height - 80);
            const sx = stage.nativeRect.width / stage.width, sy = stage.nativeRect.height / stage.height;
            fuzzyCompare(stage.nativeRect.x + map.workArea.x * sx, stage.previewRect.x, 0.001);
            fuzzyCompare(stage.nativeRect.y + map.workArea.y * sy, stage.previewRect.y, 0.001);
            fuzzyCompare(map.workArea.width * sx, stage.previewRect.width, 0.001);
            fuzzyCompare(map.workArea.height * sy, stage.previewRect.height, 0.001);
        }
    }
}
