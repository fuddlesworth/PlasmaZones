// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Widgets
import Phosphor.Theme

TestCase {
    id: testCase
    name: "SpectrumLifetime"
    when: windowShown
    width: 400
    height: 200
    visible: true
    QtObject {
        id: first
        property var samples: [0.2, 0.5, 0.3]
        property bool capturing: false
        function setActive(owner, enabled) {
            capturing = enabled;
        }
    }
    QtObject {
        id: second
        property var samples: [0.4, 0.7, 0.1]
        property bool capturing: false
        function setActive(owner, enabled) {
            capturing = enabled;
        }
    }
    Component {
        id: visualizer
        SpectrumVisualizer {
            width: 280
            height: 90
        }
    }
    function test_captureFollowsVisibilityPlaybackAndProviderLifetime() {
        AppearanceStore.setValue("motion", true);
        const view = visualizer.createObject(testCase, {
            spectrum: first,
            playing: true
        });
        verify(view);
        tryCompare(first, "capturing", true);
        view.spectrum = second;
        compare(first.capturing, false);
        compare(second.capturing, true);
        view.visible = false;
        compare(second.capturing, false);
        view.visible = true;
        compare(second.capturing, true);
        view.playing = false;
        compare(second.capturing, false);
        view.playing = true;
        view.destroy();
        tryCompare(second, "capturing", false);
    }
}
