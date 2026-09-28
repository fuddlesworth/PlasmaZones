// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar
import Phosphor.Theme

TestCase {
    id: testCase
    name: "AppearanceEditor"
    width: 400
    height: 1000
    visible: true
    when: windowShown
    Component {
        id: editorComponent
        BarLayoutEditor {
            width: 380
            availableWidgets: ["clock", "power"]
        }
    }
    function init() {
        AppearanceStore.resetBarLayout();
    }
    function cleanup() {
        AppearanceStore.resetBarLayout();
    }
    function test_movingAWidgetUpdatesTheSavedLayoutAndEditor() {
        const editor = createTemporaryObject(editorComponent, testCase);
        verify(editor !== null);
        verify(waitForRendering(editor));
        const move = findChild(editor, "move-clock-left");
        verify(move !== null);
        verify(move.visible);
        verify(move.enabled);
        mouseClick(move);
        tryVerify(() => [].concat(...Appearance.settings.barLayout.left).indexOf("clock") >= 0);
        compare([].concat(...Appearance.settings.barLayout.center).indexOf("clock"), -1);
        const returnButton = findChild(editor, "move-clock-center");
        verify(returnButton !== null);
        verify(returnButton.enabled);
    }
}
