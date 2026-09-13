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
    Component {
        id: workspacesComponent
        QtObject {
            property int count: 2
            property int activeIndex: 1
            property string activeName: "Develop"
            property var model: [
                {
                    name: "Develop",
                    workspaceId: "develop",
                    isActive: activeIndex === 1
                },
                {
                    name: "Build",
                    workspaceId: "build",
                    isActive: activeIndex === 2
                }
            ]
        }
    }
    function test_workspaceCardsResolveAgainAfterDesktopSwitch() {
        const live = createTemporaryObject(mapComponent, testCase);
        const first = createTemporaryObject(mapComponent, testCase);
        const second = createTemporaryObject(mapComponent, testCase);
        const workspaces = createTemporaryObject(workspacesComponent, testCase);
        // Plain JS state mirrors the C++ factory: it cannot establish a QML
        // dependency on the active desktop inside the mapFor callback.
        const context = {
            current: 0
        };
        const stage = createTemporaryObject(stageComponent, testCase, {
            workspaces: workspaces,
            mapFor: index => index === context.current ? live : (index === 0 ? first : second),
            open: true
        });
        waitForRendering(stage);
        compare(findChild(stage, "stage-workspace-0").map, live);
        compare(findChild(stage, "stage-workspace-1").map, second);
        context.current = 1;
        workspaces.activeIndex = 2;
        compare(findChild(stage, "stage-workspace-0").map, first);
        compare(findChild(stage, "stage-workspace-1").map, live);
    }
    function test_nativePreviewGeometryDoesNotUseStripCoordinates() {
        const map = createTemporaryObject(mapComponent, testCase);
        const stage = createTemporaryObject(stageComponent, testCase, {
            mapFor: () => map
        });
        const native = Qt.rect(0.03, 0.025, 0.46, 0.9);
        const strip = {
            x: 0,
            y: 0,
            w: 0.5,
            h: 1,
            nativeRect: native
        };
        const frame = stage.windowRect(strip);
        fuzzyCompare(frame.x, stage.previewRect.x + (native.x * stage.workArea.width - stage.canvasLeft) * stage.previewRect.width / stage.canvas.width, 0.001);
        fuzzyCompare(frame.height, native.height * stage.workArea.height * stage.previewRect.height / stage.canvas.height, 0.001);
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
    function test_desktopCanvasMapsToPreviewAcrossOutputShapes() {
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
            fuzzyCompare(stage.nativeRect.x + stage.canvas.x * sx, stage.previewRect.x, 0.001);
            fuzzyCompare(stage.nativeRect.y + stage.canvas.y * sy, stage.previewRect.y, 0.001);
            fuzzyCompare(stage.canvas.width * sx, stage.previewRect.width, 0.001);
            fuzzyCompare(stage.canvas.height * sy, stage.previewRect.height, 0.001);
        }
    }
}
